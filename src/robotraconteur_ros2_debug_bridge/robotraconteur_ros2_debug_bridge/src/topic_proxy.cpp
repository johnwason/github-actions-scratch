// Copyright 2026 Wason Technology, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "topic_proxy.hpp"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/expand_topic_or_service_name.hpp"
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
#include "rmw_intraprocess_cpp/extension.hpp"
#endif
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "proxy_executor.hpp"
#include "action_proxy.hpp"
#include "component_proxy.hpp"
#include "parameter_proxy.hpp"
#include "service_proxy.hpp"
#include "value_converter.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

namespace
{

// Packets an endpoint may have in flight before it skips (slow client).
constexpr size_t kPipeBacklog = 32;

// Match the publishers where all agree, else the defaults; depth 10, as
// upstream (PLAN.md 2.3).
rclcpp::QoS subscription_qos(rclcpp::Node & node, const std::string & topic)
{
  rclcpp::QoS qos(10);
  const auto pubs = node.get_publishers_info_by_topic(topic);
  if (pubs.empty()) {
    return qos;
  }
  bool all_reliable = true;
  bool all_transient_local = true;
  for (const auto & p : pubs) {
    const auto & profile = p.qos_profile();
    all_reliable = all_reliable && profile.reliability() == rclcpp::ReliabilityPolicy::Reliable;
    all_transient_local = all_transient_local &&
      profile.durability() == rclcpp::DurabilityPolicy::TransientLocal;
  }
  if (!all_reliable) {
    qos.best_effort();
  }
  if (all_transient_local) {
    qos.transient_local();
  }
  return qos;
}

// Base of the subscriber and publisher objects.
class TopicObject : public ProxyObject
{
public:
  TopicObject(std::string type, std::string topic, std::string ros_type, const void * members)
  : ProxyObject(std::move(type)), topic_(std::move(topic)), ros_type_(std::move(ros_type)),
    members_(members), struct_type_(message_struct_type(Flavor::Cpp, members)) {}

protected:
  const std::string topic_;
  const std::string ros_type_;
  const void * const members_;  // C++ introspection
  const std::string struct_type_;
};

class SubscriberObject : public TopicObject
{
public:
  SubscriberObject(
    const std::string & def, const std::string & topic, const std::string & ros_type,
    const void * members, const rosidl_message_type_support_t * handle, rclcpp::Logger logger)
  : TopicObject(def + ".subscriber", topic, ros_type, members), handle_(handle),
    logger_(std::move(logger)), pipe_(std::make_shared<PipeFanout>(kPipeBacklog)),
    wire_(std::make_shared<WireFanout>()) {}

  ~SubscriberObject() override {close();}

  const rclcpp::Logger & logger() const {return logger_;}

  // On the proxy thread (proxy_executor.hpp), as is the subscription's
  // destruction and every message.
  void start(
    rclcpp::Node & node, const rclcpp::QoS & qos, RR_WEAK_PTR<SubscriberObject> self,
    const std::shared_ptr<ProxyExecutor> & proxy)
  {
    proxy_ = proxy;
    rclcpp::SubscriptionOptions options;
    options.callback_group = proxy->group();
    auto sub = proxy->run(
      [&]() {
        return node.create_generic_subscription(
          topic_, ros_type_, qos,
          [self](std::shared_ptr<const rclcpp::SerializedMessage> msg) {
            if (auto s = self.lock()) {
              try {
                s->on_message(*msg);
              } catch (const std::exception & e) {
                RCLCPP_WARN_THROTTLE(
                  s->logger(), *rclcpp::Clock::make_shared(), 5000, "message dropped: %s",
                  e.what());
              }
            }
          }, options);
      });
    std::lock_guard<std::mutex> lock(mutex_);
    sub_ = sub;
  }


  // The fast path (B12): with rmw_intraprocess_cpp, its tap gives each
  // publication's own message (no deserialization). The subscription stays,
  // for the graph and publishers that count subscribers, but its copies are
  // then ignored. True if the tap is on.
  bool start_tap(RR_WEAK_PTR<SubscriberObject> self)
  {
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
    const auto * api = rmw_intraprocess_cpp::extension::find_api();
    if (api == nullptr) {
      return false;
    }
    auto tap = std::make_unique<rmw_intraprocess_cpp::extension::Tap>(
      api, topic_, [self](const rmw_intraprocess_tap_sample_t & sample) {
        if (auto s = self.lock()) {
          try {
            s->on_sample(sample);
          } catch (const std::exception & e) {
            RCLCPP_WARN_THROTTLE(
              s->logger(), *rclcpp::Clock::make_shared(), 5000, "message dropped: %s",
              e.what());
          }
        }
      });
    std::lock_guard<std::mutex> lock(mutex_);
    tap_ = std::move(tap);
    tapping_ = true;
    return true;
#else
    (void)self;
    return false;
#endif
  }
  void attach(GenericSkel & skel) override
  {
    if (auto p = skel.pipe("subscriberpipe")) {
      pipe_->attach(p);
    }
    if (auto w = skel.wire("subscriberwire")) {
      wire_->attach(w);
    }
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> &, Completion done) override
  {
    if (name != "unsubscribe") {
      GenericObject::call_function(name, {}, done);
      return;
    }
    if (auto m = manager_.lock()) {
      m->remove(handle_id_);
    }
    done(ElementPtr(), RRErrorPtr());
  }

  void close() override
  {
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
    std::unique_ptr<rmw_intraprocess_cpp::extension::Tap> tap;
#endif
    {
      std::lock_guard<std::mutex> lock(mutex_);
      destroy_on_proxy_thread(proxy_, std::move(sub_));
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
      tap = std::move(tap_);
#endif
    }
    // Removing the tap waits for a call in progress: outside the lock.
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
    tap.reset();
#endif
  }

  bool fast() const {return tapping_;}

private:
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
  // On the publishing thread (rmw_publish): must be quick.
  void on_sample(const rmw_intraprocess_tap_sample_t & sample)
  {
    if (sample.type_name == nullptr || ros_type_ != sample.type_name) {
      return;
    }
    if (sample.ros_message != nullptr && sample.introspection_members != nullptr) {
      const Flavor fl =
        std::strstr(sample.introspection_identifier, "introspection_cpp") != nullptr ?
        Flavor::Cpp : Flavor::C;
      deliver(ros_to_rr(fl, sample.introspection_members, sample.ros_message, struct_type_));
      return;
    }
    // Published serialized: from its CDR.
    rmw_serialized_message_t cdr = rmw_get_zero_initialized_serialized_message();
    cdr.buffer = const_cast<uint8_t *>(sample.cdr);
    cdr.buffer_length = sample.cdr_length;
    cdr.buffer_capacity = sample.cdr_length;
    DynamicMessage m(Flavor::Cpp, members_);
    if (rmw_deserialize(&cdr, handle_, m.data()) != RMW_RET_OK) {
      rmw_reset_error();
      return;
    }
    deliver(ros_to_rr(Flavor::Cpp, members_, m.data(), struct_type_));
  }
#endif

  void deliver(const StructurePtr & s)
  {
    const ElementPtr e = RR::CreateMessageElement("value", s);
    pipe_->send(e);
    wire_->set(e);
  }

  void on_message(const rclcpp::SerializedMessage & msg)
  {
    if (tapping_) {
      return;  // the tap delivers
    }
    on_serialized(msg);
  }

  void on_serialized(const rclcpp::SerializedMessage & msg)
  {
    DynamicMessage m(Flavor::Cpp, members_);
    const rmw_ret_t ret = rmw_deserialize(&msg.get_rcl_serialized_message(), handle_, m.data());
    if (ret != RMW_RET_OK) {
      rmw_reset_error();
      RCLCPP_WARN_THROTTLE(
        logger_, *rclcpp::Clock::make_shared(), 5000, "%s: cannot deserialize a message",
        topic_.c_str());
      return;
    }
    deliver(ros_to_rr(Flavor::Cpp, members_, m.data(), struct_type_));
  }

  const rosidl_message_type_support_t * const handle_;
  rclcpp::Logger logger_;
  std::shared_ptr<PipeFanout> pipe_;
  std::shared_ptr<WireFanout> wire_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::mutex mutex_;
  rclcpp::GenericSubscription::SharedPtr sub_;
  std::atomic<bool> tapping_{false};
#ifdef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP
  std::unique_ptr<rmw_intraprocess_cpp::extension::Tap> tap_;
#endif
};

class PublisherObject : public TopicObject, public RR_ENABLE_SHARED_FROM_THIS<PublisherObject>
{
public:
  PublisherObject(
    const std::string & def, const std::string & topic, const std::string & ros_type,
    const void * members, const rosidl_message_type_support_t * handle, rclcpp::Logger logger)
  : TopicObject(def + ".publisher", topic, ros_type, members), handle_(handle),
    logger_(std::move(logger)) {}

  ~PublisherObject() override {close();}

  // Created and destroyed on the proxy thread (proxy_executor.hpp).
  void start(
    rclcpp::Node & node, const rclcpp::QoS & qos, const std::shared_ptr<ProxyExecutor> & proxy)
  {
    proxy_ = proxy;
    auto pub = proxy->run([&]() {return node.create_generic_publisher(topic_, ros_type_, qos);});
    std::lock_guard<std::mutex> lock(mutex_);
    pub_ = pub;
  }

  void attach(GenericSkel & skel) override
  {
    RR_WEAK_PTR<PublisherObject> weak = shared_from_this();
    if (auto p = skel.pipe("publisherpipe")) {
      p->set_connect_callback(
        [weak](const RR_SHARED_PTR<GenericPipeEndpoint> & ep) {
          ep->set_packet_callback(
            [weak](const ElementPtr & e) {
              if (auto s = weak.lock()) {
                s->publish_logged(e);
              }
            });
        });
    }
    if (auto w = skel.wire("publisherwire")) {
      w->set_connect_callback(
        [weak](const RR_SHARED_PTR<GenericWireConnection> & c) {
          c->set_value_callback(
            [weak](const ElementPtr & e) {
              if (auto s = weak.lock()) {
                s->publish_logged(e);
              }
            });
        });
      GenericWireServer::PeekPoke pp;
      pp.peek_in = []() -> ElementPtr {
          throw RR::WriteOnlyMemberException("Write only member");
        };
      pp.peek_out = [weak]() -> ElementPtr {
          auto s = weak.lock();
          if (!s) {
            throw RR::ValueNotSetException("Value not set");
          }
          std::lock_guard<std::mutex> lock(s->mutex_);
          if (!s->last_) {
            throw RR::ValueNotSetException("Value not set");
          }
          return copy_element(s->last_);
        };
      pp.poke_out = [weak](const ElementPtr & e) {
          if (auto s = weak.lock()) {
            s->publish(e);  // errors go to the client
          }
        };
      w->set_peek_poke(std::move(pp));
    }
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done) override
  {
    if (name != "publish") {
      GenericObject::call_function(name, params, done);
      return;
    }
    ElementPtr m;
    for (const auto & p : params) {
      if (p->ElementName.str() == "m") {
        m = p;
      }
    }
    if (!m) {
      throw RR::InvalidArgumentException("publish: parameter m missing");
    }
    publish(m);
    done(ElementPtr(), RRErrorPtr());
  }

  void close() override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroy_on_proxy_thread(proxy_, std::move(pub_));
  }

  void publish(const ElementPtr & e)
  {
    const auto s = element_structure(e);
    if (!s) {
      throw RR::InvalidArgumentException(topic_ + ": expected a " + struct_type_ + " structure");
    }
    DynamicMessage m(Flavor::Cpp, members_);
    try {
      rr_to_ros(Flavor::Cpp, members_, s, m.data(), struct_type_);
    } catch (const ConversionError & err) {
      throw RR::InvalidArgumentException(topic_ + ": " + err.what());
    }
    rclcpp::SerializedMessage serialized;
    if (rmw_serialize(m.data(), handle_, &serialized.get_rcl_serialized_message()) != RMW_RET_OK) {
      const std::string why = rmw_get_error_string().str;
      rmw_reset_error();
      throw RR::OperationFailedException(topic_ + ": cannot serialize: " + why);
    }
    // Under the lock: no reference to the publisher leaves it, so close()
    // hands the last one to the proxy thread.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pub_) {
      throw RR::InvalidOperationException(topic_ + ": publisher closed");
    }
    last_ = e;
    pub_->publish(serialized);
  }

private:
  void publish_logged(const ElementPtr & e)
  {
    try {
      publish(e);
    } catch (const std::exception & err) {
      RCLCPP_WARN_THROTTLE(
        logger_, *rclcpp::Clock::make_shared(), 5000, "%s", err.what());
    }
  }

  const rosidl_message_type_support_t * const handle_;
  rclcpp::Logger logger_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::mutex mutex_;
  rclcpp::GenericPublisher::SharedPtr pub_;
  ElementPtr last_;
};

}  // namespace

// ---------------------------------------------------------------------------

ProxyManager::ProxyManager(
  rclcpp::Node & node, RR_SHARED_PTR<RR::RobotRaconteurNode> rr_node,
  std::shared_ptr<const ExposurePolicy> policy, bool fast_path)
: node_(node), rr_node_(rr_node), policy_(std::move(policy)),
  proxy_(std::make_shared<ProxyExecutor>(node)), fast_path_(fast_path) {}

ProxyManager::~ProxyManager() {shutdown();}

namespace
{

const char * kind_member(ProxyKind kind)
{
  switch (kind) {
    case ProxyKind::Subscriber: return "subscribers";
    case ProxyKind::Publisher: return "publishers";
    case ProxyKind::Client: return "clients";
    case ProxyKind::Service: return "services";
    case ProxyKind::ActionClient: return "action_clients";
  }
  return "";
}

const char * kind_noun(ProxyKind kind)
{
  switch (kind) {
    case ProxyKind::Subscriber: return "subscriber";
    case ProxyKind::Publisher: return "publisher";
    case ProxyKind::Client: return "client";
    case ProxyKind::Service: return "service";
    case ProxyKind::ActionClient: return "action client";
  }
  return "";
}

}  // namespace

std::string ProxyManager::expand(const std::string & name, bool is_service) const
{
  try {
    return rclcpp::expand_topic_or_service_name(
      name, node_.get_name(), node_.get_namespace(), is_service);
  } catch (const std::exception &) {
    throw RR::InvalidArgumentException(
            std::string("Invalid ") + (is_service ? "service" : "topic") + " name '" + name + "'");
  }
}

ProxyManager::Resolved ProxyManager::resolve_topic(
  const std::string & fq, const std::string & msgtype) const
{
  Resolved r;
  r.name = fq;
  std::string type = msgtype;
  if (type.empty()) {
    const auto all = node_.get_topic_names_and_types();
    const auto it = all.find(fq);
    if (it == all.end() || it->second.size() != 1) {
      throw RR::InvalidArgumentException(
              "Topic '" + fq + "': give msgtype (no single type in the graph)");
    }
    type = it->second.front();
  }
  try {
    r.type = parse_interface_type(type, InterfaceKind::Message).full_name();
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  return r;
}

ProxyManager::Resolved ProxyManager::resolve_service(
  const std::string & fq, const std::string & srvtype) const
{
  Resolved r;
  r.name = fq;
  std::string type = srvtype;
  if (type.empty()) {
    const auto all = node_.get_service_names_and_types();
    const auto it = all.find(fq);
    if (it == all.end() || it->second.size() != 1) {
      throw RR::InvalidArgumentException(
              "Service '" + fq + "': give srvtype (no single type in the graph)");
    }
    type = it->second.front();
  }
  try {
    r.type = parse_interface_type(type, InterfaceKind::Service).full_name();
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  return r;
}

void ProxyManager::register_definitions(const std::string & root)
{
  auto rr = rr_node_.lock();
  if (!rr) {
    throw RR::InvalidOperationException("Bridge shut down");
  }
  static std::mutex registration;  // check-then-register is not atomic in the node
  std::lock_guard<std::mutex> lock(registration);
  for (const auto & name : mapper_.closure(root)) {
    if (!rr->IsServiceTypeRegistered(name)) {
      rr->RegisterServiceType(RR_MAKE_SHARED<GenericServiceFactory>(mapper_.definition(name)));
    }
  }
}

int32_t ProxyManager::subscribe(
  const std::string & topic, const std::string & msgtype, uint32_t client)
{
  const std::string fq = expand(topic, false);
  // Checked before anything that looks at the graph: a denied topic is
  // indistinguishable from one that does not exist.
  if (!policy_->topic_subscribable(fq)) {
    throw RR::InvalidArgumentException("Topic '" + fq + "' is not available");
  }
  const Resolved r = resolve_topic(fq, msgtype);
  RR_SHARED_PTR<SubscriberObject> obj;
  try {
    const std::string def = mapper_.topic(r.type);
    register_definitions(def);
    obj = RR_MAKE_SHARED<SubscriberObject>(
      def, r.name, r.type, typesupport_.message(Flavor::Cpp, r.type),
      typesupport_.cpp_handle(r.type), node_.get_logger());
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  } catch (const TypeSupportError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  const rclcpp::QoS qos = subscription_qos(node_, r.name);
  obj->start(proxy_->node(), qos, obj, proxy_);
  // The fast path, for volatile topics (a tap does not see the latched value).
  const bool fast = fast_path_ && qos.durability() != rclcpp::DurabilityPolicy::TransientLocal &&
    obj->start_tap(obj);
  RCLCPP_INFO(
    node_.get_logger(), "Subscribed %s (%s, %s, %s%s) for client %u", r.name.c_str(),
    r.type.c_str(),
    qos.reliability() == rclcpp::ReliabilityPolicy::Reliable ? "reliable" : "best effort",
    qos.durability() == rclcpp::DurabilityPolicy::TransientLocal ? "transient local" : "volatile",
    fast ? ", fast path" : "", client);
  return add(obj, client, ProxyKind::Subscriber);
}

int32_t ProxyManager::publish(
  const std::string & topic, const std::string & msgtype, uint32_t client)
{
  const std::string fq = expand(topic, false);
  if (!policy_->topic_publishable(fq)) {
    throw RR::InvalidArgumentException("Topic '" + fq + "' is not available for publishing");
  }
  const Resolved r = resolve_topic(fq, msgtype);
  RR_SHARED_PTR<PublisherObject> obj;
  try {
    const std::string def = mapper_.topic(r.type);
    register_definitions(def);
    obj = RR_MAKE_SHARED<PublisherObject>(
      def, r.name, r.type, typesupport_.message(Flavor::Cpp, r.type),
      typesupport_.cpp_handle(r.type), node_.get_logger());
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  } catch (const TypeSupportError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  obj->start(proxy_->node(), rclcpp::QoS(10), proxy_);
  return add(obj, client, ProxyKind::Publisher);
}

int32_t ProxyManager::client(
  const std::string & service, const std::string & srvtype, uint32_t client)
{
  const std::string fq = expand(service, true);
  if (!policy_->service_callable(fq)) {
    throw RR::InvalidArgumentException("Service '" + fq + "' is not available");
  }
  const Resolved r = resolve_service(fq, srvtype);
  RR_SHARED_PTR<ProxyObject> obj;
  try {
    const std::string def = mapper_.service(r.type);
    register_definitions(def);
    obj = make_service_client(
      *this, def, r.name, r.type, typesupport_.service(Flavor::Cpp, r.type));
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  } catch (const TypeSupportError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  return add(obj, client, ProxyKind::Client);
}

int32_t ProxyManager::register_service(
  const std::string & service, const std::string & srvtype, uint32_t client)
{
  const std::string fq = expand(service, true);
  if (!policy_->service_servable(fq)) {
    throw RR::InvalidArgumentException("Service '" + fq + "' is not available for serving");
  }
  if (srvtype.empty()) {
    throw RR::InvalidArgumentException("register_service: give srvtype");
  }
  const Resolved r = resolve_service(fq, srvtype);
  RR_SHARED_PTR<ProxyObject> obj;
  try {
    const std::string def = mapper_.service(r.type);
    register_definitions(def);
    obj = make_service_server(
      *this, def, r.name, r.type, typesupport_.service(Flavor::Cpp, r.type),
      typesupport_.cpp_service_handle(r.type), client);
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  } catch (const TypeSupportError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  return add(obj, client, ProxyKind::Service);
}

int32_t ProxyManager::action_client(
  const std::string & action, const std::string & actiontype, uint32_t client)
{
  const std::string fq = expand(action, false);
  if (!policy_->action_sendable(fq)) {
    throw RR::InvalidArgumentException("Action '" + fq + "' is not available");
  }
  std::string type = actiontype;
  if (type.empty()) {
    // From the action's feedback topic: pkg/action/<A>_FeedbackMessage.
    const auto all = node_.get_topic_names_and_types();
    const auto it = all.find(fq + "/_action/feedback");
    const std::string suffix = "_FeedbackMessage";
    if (it == all.end() || it->second.size() != 1 || it->second.front().size() <= suffix.size()) {
      throw RR::InvalidArgumentException(
              "Action '" + fq + "': give actiontype (no single type in the graph)");
    }
    type = it->second.front().substr(0, it->second.front().size() - suffix.size());
  }
  RR_SHARED_PTR<ProxyObject> obj;
  try {
    const std::string full = parse_interface_type(type, InterfaceKind::Action).full_name();
    const std::string def = mapper_.action(full);
    register_definitions(def);
    obj = make_action_client(*this, def, fq, full);
  } catch (const TypeMapperError & e) {
    throw RR::InvalidArgumentException(e.what());
  } catch (const TypeSupportError & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  return add(obj, client, ProxyKind::ActionClient);
}

int32_t ProxyManager::add(
  const RR_SHARED_PTR<ProxyObject> & object, uint32_t client, ProxyKind kind)
{
  int32_t h = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!shut_down_) {
      h = next_handle_++;
      objects_[h] = Entry{object, client, kind};
    }
  }
  if (h == 0) {
    object->close();
    throw RR::InvalidOperationException("Bridge shut down");
  }
  object->bind(weak_from_this(), h);
  return h;
}

RR_SHARED_PTR<RR::RRObject> ProxyManager::object(int32_t handle, ProxyKind kind)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = objects_.find(handle);
  if (it == objects_.end() || it->second.kind != kind) {
    throw RR::InvalidArgumentException(
            std::string("No ") + kind_noun(kind) + " " + std::to_string(handle));
  }
  return it->second.object;
}

void ProxyManager::remove(int32_t handle)
{
  RR_SHARED_PTR<ProxyObject> obj;
  ProxyKind kind = ProxyKind::Subscriber;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = objects_.find(handle);
    if (it == objects_.end()) {
      return;
    }
    obj = it->second.object;
    kind = it->second.kind;
    objects_.erase(it);
  }
  obj->close();
  release_paths({{handle, kind}});
}

void ProxyManager::client_disconnected(uint32_t client)
{
  std::vector<RR_SHARED_PTR<ProxyObject>> closing;
  std::vector<std::pair<int32_t, ProxyKind>> handles;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = objects_.begin(); it != objects_.end(); ) {
      if (it->second.client == client) {
        closing.push_back(it->second.object);
        handles.emplace_back(it->first, it->second.kind);
        it = objects_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto & o : closing) {
    o->close();
  }
  release_paths(handles);
}

void ProxyManager::set_server_context(
  RR_WEAK_PTR<RR::ServerContext> context, const std::string & service_path)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    context_ = context;
    service_path_ = service_path;
  }
  auto ctx = context.lock();
  if (!ctx) {
    return;
  }
  std::weak_ptr<ProxyManager> weak = weak_from_this();
  ctx->ServerServiceListener.connect(
    [weak](const RR_SHARED_PTR<RR::ServerContext> &, RR::ServerServiceListenerEventType evt,
    const RR_SHARED_PTR<void> & param) {
      if (evt != RR::ServerServiceListenerEventType_ClientDisconnected || !param) {
        return;
      }
      if (auto m = weak.lock()) {
        m->client_disconnected(*RR_STATIC_POINTER_CAST<uint32_t>(param));
      }
    });
}

void ProxyManager::release_paths(const std::vector<std::pair<int32_t, ProxyKind>> & handles)
{
  if (handles.empty()) {
    return;
  }
  RR_SHARED_PTR<RR::ServerContext> ctx;
  std::string base;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ctx = context_.lock();
    base = service_path_;
  }
  auto rr = rr_node_.lock();
  if (!ctx || !rr) {
    return;
  }
  // Clients drop the objrefs' objects; the cached skels go. Posted: this
  // may run inside a call on the very object, or under the context's locks.
  std::vector<std::string> paths;
  for (const auto & h : handles) {
    paths.push_back(
      base + "." + kind_member(h.second) + "[" + std::to_string(h.first) + "]");
  }
  RR_WEAK_PTR<RR::ServerContext> weak_ctx = ctx;
  try {
    rr->GetThreadPool()->TryPost(
      [weak_ctx, paths]() {
        auto c = weak_ctx.lock();
        if (!c) {
          return;
        }
        for (const auto & p : paths) {
          try {
            c->ReleaseServicePath(p);
          } catch (const std::exception &) {
          }
        }
      });
  } catch (const std::exception &) {
    // node shutting down
  }
}

void ProxyManager::shutdown()
{
  std::map<int32_t, Entry> objects;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shut_down_ = true;
    objects.swap(objects_);
  }
  for (auto & kv : objects) {
    kv.second.object->close();
  }
  RR_SHARED_PTR<ProxyObject> params;
  RR_SHARED_PTR<ProxyObject> comps;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    params.swap(parameters_);
    comps.swap(components_);
  }
  if (params) {
    params->close();
  }
  if (comps) {
    comps->close();
  }
  proxy_->stop();  // runs the queued destructions first
}

void ProxyManager::register_definition_text(const std::string & name, const std::string & text)
{
  auto rr = rr_node_.lock();
  if (!rr) {
    throw RR::InvalidOperationException("Bridge shut down");
  }
  static std::mutex registration;
  std::lock_guard<std::mutex> lock(registration);
  if (!rr->IsServiceTypeRegistered(name)) {
    rr->RegisterServiceType(RR_MAKE_SHARED<GenericServiceFactory>(text));
  }
}

RR_SHARED_PTR<RR::RRObject> ProxyManager::parameters()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (shut_down_) {
    throw RR::InvalidOperationException("Bridge shut down");
  }
  if (!parameters_) {
    parameters_ = make_parameters_object(*this);
  }
  return parameters_;
}

RR_SHARED_PTR<RR::RRObject> ProxyManager::components()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (shut_down_) {
    throw RR::InvalidOperationException("Bridge shut down");
  }
  if (!components_) {
    components_ = make_components_object(*this);
  }
  return components_;
}

}  // namespace robotraconteur_ros2_debug_bridge
