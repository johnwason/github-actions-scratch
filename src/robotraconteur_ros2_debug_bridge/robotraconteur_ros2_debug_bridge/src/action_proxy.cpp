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

#include "action_proxy.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "action_msgs/msg/goal_status.hpp"
#include "action_msgs/msg/goal_status_array.hpp"
#include "action_msgs/srv/cancel_goal.hpp"
#include "proxy_executor.hpp"
#include "rclcpp/generic_client.hpp"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"
#include "type_mapper.hpp"
#include "value_converter.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;
namespace ti = rosidl_typesupport_introspection_cpp;

namespace
{

using Uuid = std::array<uint8_t, 16>;

constexpr std::chrono::seconds kSendDeadline{30};

RRErrorPtr error(const std::string & what)
{
  return RR_MAKE_SHARED<RR::OperationFailedException>(what);
}

const ti::MessageMember & member(const ti::MessageMembers * m, const char * name)
{
  for (uint32_t i = 0; i < m->member_count_; ++i) {
    if (std::strcmp(m->members_[i].name_, name) == 0) {
      return m->members_[i];
    }
  }
  throw TypeMapperError(std::string("no member ") + name + " in " + m->message_name_);
}

void * field(const ti::MessageMembers * m, void * msg, const char * name)
{
  return static_cast<uint8_t *>(msg) + member(m, name).offset_;
}

const void * field(const ti::MessageMembers * m, const void * msg, const char * name)
{
  return static_cast<const uint8_t *>(msg) + member(m, name).offset_;
}

const ti::MessageMembers * nested(const ti::MessageMembers * m, const char * name)
{
  return static_cast<const ti::MessageMembers *>(member(m, name).members_->data);
}

// A unique_identifier_msgs/UUID field.
void set_uuid(const ti::MessageMembers * m, void * msg, const char * name, const Uuid & id)
{
  std::memcpy(field(nested(m, name), field(m, msg, name), "uuid"), id.data(), id.size());
}

Uuid get_uuid(const ti::MessageMembers * m, const void * msg, const char * name)
{
  Uuid id;
  std::memcpy(id.data(), field(nested(m, name), field(m, msg, name), "uuid"), id.size());
  return id;
}

Uuid random_uuid()
{
  static std::mutex mutex;
  static std::mt19937_64 gen{std::random_device{}()};
  std::lock_guard<std::mutex> lock(mutex);
  Uuid id;
  for (size_t i = 0; i < id.size(); i += 8) {
    const uint64_t v = gen();
    std::memcpy(id.data() + i, &v, 8);
  }
  return id;
}

// ---------------------------------------------------------------------------

// One goal's generator. Its updates are queued until the client asks.
class GoalRun : public GeneratorHandler
{
public:
  using Cancel = std::function<void (const Uuid &)>;

  GoalRun(Uuid id, Cancel cancel)
  : id_(id), cancel_(std::move(cancel)) {}

  const Uuid & id() const {return id_;}

  void next(const ElementPtr &, Done done) override
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!updates_.empty()) {
      auto e = updates_.front();
      updates_.pop_front();
      lock.unlock();
      done(e, RRErrorPtr());
    } else if (failure_) {
      auto err = failure_;
      failure_.reset();
      finished_ = true;
      lock.unlock();
      done(ElementPtr(), err);
    } else if (finished_) {
      lock.unlock();
      done(ElementPtr(), RR_MAKE_SHARED<RR::StopIterationException>(""));
    } else {
      pending_ = std::move(done);
    }
  }

  void abort() override {stop();}
  void close() override {stop();}

  void push(const ElementPtr & e, int8_t status, bool final)
  {
    Done pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (finished_) {
        return;
      }
      last_status_ = status;
      if (pending_) {
        pending.swap(pending_);
      } else {
        updates_.push_back(e);
      }
      if (final) {
        finished_ = true;
        done_ = true;
      }
    }
    if (pending) {
      pending(e, RRErrorPtr());
    }
  }

  void fail(const RRErrorPtr & err)
  {
    Done pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (finished_) {
        return;
      }
      done_ = true;
      if (pending_) {
        pending.swap(pending_);
        finished_ = true;
      } else {
        failure_ = err;
      }
    }
    if (pending) {
      pending(ElementPtr(), err);
    }
  }

  /// True the first time.
  bool mark_accepted()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool first = !accepted_;
    accepted_ = true;
    return first;
  }

  int8_t last_status()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_status_;
  }

private:
  // Abort or Close: cancel a goal still running; the generator ends.
  void stop()
  {
    bool running = false;
    Done pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running = !done_;
      finished_ = true;
      done_ = true;
      updates_.clear();
      pending.swap(pending_);
    }
    if (running && cancel_) {
      cancel_(id_);
    }
    if (pending) {
      pending(ElementPtr(), RR_MAKE_SHARED<RR::StopIterationException>(""));
    }
  }

  const Uuid id_;
  const Cancel cancel_;
  std::mutex mutex_;
  std::deque<ElementPtr> updates_;
  Done pending_;
  RRErrorPtr failure_;
  bool finished_ = false;  // no more updates to give
  bool done_ = false;      // the goal is over (or was never accepted)
  bool accepted_ = false;
  int8_t last_status_ = action_msgs::msg::GoalStatus::STATUS_UNKNOWN;
};

// ---------------------------------------------------------------------------

class ActionClientObject : public ProxyObject, public RR_ENABLE_SHARED_FROM_THIS<ActionClientObject>
{
public:
  ActionClientObject(
    IntrospectionCache & cache, const std::string & definition, std::string action,
    const std::string & type)
  : ProxyObject(definition + ".rosactionclient"), action_(std::move(action)), type_(type)
  {
    const auto t = parse_interface_type(type, InterfaceKind::Action);
    const auto * send = static_cast<const ti::ServiceMembers *>(
      cache.service(Flavor::Cpp, type + "_SendGoal"));
    const auto * result = static_cast<const ti::ServiceMembers *>(
      cache.service(Flavor::Cpp, type + "_GetResult"));
    send_request_ = send->request_members_;
    send_response_ = send->response_members_;
    result_request_ = result->request_members_;
    result_response_ = result->response_members_;
    feedback_message_ = static_cast<const ti::MessageMembers *>(
      cache.message(Flavor::Cpp, type + "_FeedbackMessage"));
    feedback_handle_ = cache.cpp_handle(type + "_FeedbackMessage");
    goal_struct_ = definition + "." + fixname(t.name + "Goal");
    result_struct_ = definition + "." + fixname(t.name + "Result");
    feedback_struct_ = definition + "." + fixname(t.name + "Feedback");
    status_struct_ = definition + "." + fixname(t.name + "GoalStatus");
  }

  // The manager closes every object before releasing it.
  ~ActionClientObject() override = default;

  void start(rclcpp::Node & node, const std::shared_ptr<ProxyExecutor> & proxy)
  {
    proxy_ = proxy;
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    const std::string base = action_ + "/_action/";
    proxy->run(
      [&]() {
        send_client_ = node.create_generic_client(
          base + "send_goal", type_ + "_SendGoal", rclcpp::ServicesQoS(), proxy->group());
        result_client_ = node.create_generic_client(
          base + "get_result", type_ + "_GetResult", rclcpp::ServicesQoS(), proxy->group());
        cancel_client_ = node.create_client<action_msgs::srv::CancelGoal>(
          base + "cancel_goal", rclcpp::ServicesQoS(), proxy->group());
        feedback_sub_ = node.create_generic_subscription(
          base + "feedback", type_ + "_FeedbackMessage", rclcpp::QoS(10),
          [weak](std::shared_ptr<const rclcpp::SerializedMessage> msg) {
            if (auto o = weak.lock()) {
              o->on_feedback(*msg);
            }
          });
        // rcl_action's status QoS: reliable, transient local, depth 1.
        status_sub_ = node.create_subscription<action_msgs::msg::GoalStatusArray>(
          base + "status", rclcpp::QoS(1).reliable().transient_local(),
          [weak](const action_msgs::msg::GoalStatusArray & s) {
            if (auto o = weak.lock()) {
              o->on_status(s);
            }
          });
      });
  }

  void close() override
  {
    if (!proxy_) {
      return;
    }
    // The entities are the proxy thread's: they go there.
    auto self = shared_from_this();
    try {
      proxy_->post([self]() {self->drop_entities();});
    } catch (const std::exception &) {
      drop_entities();  // stopped: nothing else runs any more
    }
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done) override
  {
    if (name != "wait_for_server") {
      GenericObject::call_function(name, params, done);
      return;
    }
    ElementPtr t;
    for (const auto & p : params) {
      if (p->ElementName.str() == "timeout") {
        t = p;
      }
    }
    if (!t || t->ElementType != RR::DataTypes_double_t) {
      throw RR::InvalidArgumentException("timeout: expected a double");
    }
    const double seconds = RR::RRArrayToScalar(t->CastData<RR::RRArray<double>>());
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(seconds < 0 ? 0 : seconds));
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->add_poller(
      [weak, done, deadline, proxy]() {
        auto o = weak.lock();
        if (!o || proxy->stopping() || !o->send_client_) {
          done(ElementPtr(), error("action client closed"));
          return true;
        }
        const bool ready = o->send_client_->service_is_ready() &&
        o->result_client_->service_is_ready() && o->cancel_client_->service_is_ready();
        if (ready || std::chrono::steady_clock::now() >= deadline) {
          done(
            RR::CreateMessageElement(
              "return", RR::ScalarToRRArray<RR::rr_bool>(RR::rr_bool(ready))), RRErrorPtr());
          return true;
        }
        return false;
      });
  }

  std::shared_ptr<GeneratorHandler> start_generator(
    const std::string & name, const std::vector<ElementPtr> & params) override
  {
    if (name != "send_goal") {
      return GenericObject::start_generator(name, params);
    }
    ElementPtr g;
    for (const auto & p : params) {
      if (p->ElementName.str() == "goal") {
        g = p;
      }
    }
    const auto s = element_structure(g);
    if (!s) {
      throw RR::InvalidArgumentException("goal: expected a " + goal_struct_);
    }
    auto request = std::make_shared<DynamicMessage>(Flavor::Cpp, send_request_);
    const Uuid id = random_uuid();
    try {
      set_uuid(send_request_, request->data(), "goal_id", id);
      rr_to_ros(
        Flavor::Cpp, nested(send_request_, "goal"), s,
        field(send_request_, request->data(), "goal"),
        goal_struct_);
    } catch (const ConversionError & e) {
      throw RR::InvalidArgumentException(action_ + ": " + e.what());
    }
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    auto run = std::make_shared<GoalRun>(
      id, [weak](const Uuid & goal) {
        if (auto o = weak.lock()) {
          o->cancel(goal);
        }
      });
    auto proxy = proxy_;
    proxy->post([weak, run, request]() {
        if (auto o = weak.lock()) {
          o->send(run, request);
        } else {
          run->fail(error("action client closed"));
        }
      });
    return run;
  }

private:
  // ----- on the proxy thread

  // ACCEPTED, once, before any other update.
  void accept_goal(const std::shared_ptr<GoalRun> & run)
  {
    if (run->mark_accepted()) {
      run->push(
        status(run->id(), action_msgs::msg::GoalStatus::STATUS_ACCEPTED),
        action_msgs::msg::GoalStatus::STATUS_ACCEPTED, false);
    }
  }

  void drop_entities()
  {
    send_client_.reset();
    result_client_.reset();
    cancel_client_.reset();
    feedback_sub_.reset();
    status_sub_.reset();
    goals_.clear();
  }

  void send(const std::shared_ptr<GoalRun> & run, const std::shared_ptr<DynamicMessage> & request)
  {
    if (!send_client_ || !send_client_->service_is_ready()) {
      run->fail(error(action_ + ": action server not available"));
      return;
    }
    goals_[run->id()] = run;
    auto sent = std::make_shared<rclcpp::GenericClient::FutureAndRequestId>(
      send_client_->async_send_request(request->data()));
    const auto deadline = std::chrono::steady_clock::now() + kSendDeadline;
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->add_poller(
      [weak, run, sent, deadline, proxy]() {
        auto o = weak.lock();
        if (!o || proxy->stopping()) {
          run->fail(error("action client closed"));
          return true;
        }
        if (sent->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
          o->accepted(run, sent->future.get());
          return true;
        }
        if (std::chrono::steady_clock::now() > deadline) {
          o->goals_.erase(run->id());
          run->fail(RR_MAKE_SHARED<RR::RequestTimeoutException>("no answer to send_goal"));
          return true;
        }
        return false;
      });
  }

  void accepted(const std::shared_ptr<GoalRun> & run, const std::shared_ptr<void> & response)
  {
    if (!*static_cast<const bool *>(field(send_response_, response.get(), "accepted"))) {
      goals_.erase(run->id());
      run->fail(error(action_ + ": goal rejected"));
      return;
    }
    accept_goal(run);
    // Ask for the result now; the server answers when the goal is done.
    auto request = std::make_shared<DynamicMessage>(Flavor::Cpp, result_request_);
    set_uuid(result_request_, request->data(), "goal_id", run->id());
    auto sent = std::make_shared<rclcpp::GenericClient::FutureAndRequestId>(
      result_client_->async_send_request(request->data()));
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->add_poller(
      [weak, run, sent, proxy]() {
        auto o = weak.lock();
        if (!o || proxy->stopping()) {
          run->fail(error("action client closed"));
          return true;
        }
        if (sent->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
          return false;
        }
        auto r = sent->future.get();
        const int8_t st =
        *static_cast<const int8_t *>(field(o->result_response_, r.get(), "status"));
        ElementPtr e;
        try {
          e = o->status(
            run->id(), st, nullptr,
            ros_to_rr(
              Flavor::Cpp, nested(o->result_response_, "result"),
              field(o->result_response_, r.get(), "result"), o->result_struct_));
        } catch (const std::exception & ex) {
          o->goals_.erase(run->id());
          run->fail(error(ex.what()));
          return true;
        }
        o->goals_.erase(run->id());
        run->push(e, st, true);
        return true;
      });
  }

  void on_feedback(const rclcpp::SerializedMessage & msg)
  {
    DynamicMessage m(Flavor::Cpp, feedback_message_);
    if (rmw_deserialize(&msg.get_rcl_serialized_message(), feedback_handle_, m.data()) !=
      RMW_RET_OK)
    {
      rmw_reset_error();
      return;
    }
    const auto it = goals_.find(get_uuid(feedback_message_, m.data(), "goal_id"));
    if (it == goals_.end()) {
      return;
    }
    auto run = it->second.lock();
    if (run) {
      accept_goal(run);  // an update before the send_goal answer: it was accepted
    }
    if (!run) {
      return;
    }
    run->push(
      status(
        it->first, action_msgs::msg::GoalStatus::STATUS_EXECUTING,
        ros_to_rr(
          Flavor::Cpp, nested(feedback_message_, "feedback"),
          field(feedback_message_, m.data(), "feedback"), feedback_struct_)),
      action_msgs::msg::GoalStatus::STATUS_EXECUTING, false);
  }

  // Status changes while the goal runs (the final one comes with the result).
  void on_status(const action_msgs::msg::GoalStatusArray & array)
  {
    for (const auto & s : array.status_list) {
      const auto it = goals_.find(s.goal_info.goal_id.uuid);
      if (it == goals_.end()) {
        continue;
      }
      auto run = it->second.lock();
      if (run) {
        accept_goal(run);
      }
      if (!run || s.status == run->last_status()) {
        continue;
      }
      if (s.status == action_msgs::msg::GoalStatus::STATUS_EXECUTING ||
        s.status == action_msgs::msg::GoalStatus::STATUS_CANCELING)
      {
        run->push(status(it->first, s.status), s.status, false);
      }
    }
  }

  void cancel(const Uuid & goal)
  {
    RR_WEAK_PTR<ActionClientObject> weak = shared_from_this();
    try {
      proxy_->post(
        [weak, goal]() {
          auto o = weak.lock();
          if (!o || !o->cancel_client_) {
            return;
          }
          auto req = std::make_shared<action_msgs::srv::CancelGoal::Request>();
          req->goal_info.goal_id.uuid = goal;
          o->cancel_client_->async_send_request(req);
          // The goal's generator has ended; its result is not delivered.
          o->goals_.erase(goal);
        });
    } catch (const std::exception &) {
    }
  }

  // An <A>GoalStatus value; null feedback and result when absent.
  ElementPtr status(
    const Uuid & id, int8_t st, const StructurePtr & feedback = nullptr,
    const StructurePtr & result = nullptr) const
  {
    std::vector<ElementPtr> f;
    f.push_back(RR::CreateMessageElement("goal_id", RR::AttachRRArrayCopy(id.data(), id.size())));
    f.push_back(RR::CreateMessageElement("status", RR::ScalarToRRArray<int8_t>(st)));
    f.push_back(RR::CreateMessageElement("feedback", feedback));
    f.push_back(RR::CreateMessageElement("result", result));
    return RR::CreateMessageElement(
      "value",
      RR::CreateMessageElementNestedElementList(
        RR::DataTypes_structure_t, status_struct_, std::move(f)));
  }

  const std::string action_;
  const std::string type_;
  const ti::MessageMembers * send_request_;
  const ti::MessageMembers * send_response_;
  const ti::MessageMembers * result_request_;
  const ti::MessageMembers * result_response_;
  const ti::MessageMembers * feedback_message_;
  const rosidl_message_type_support_t * feedback_handle_;
  std::string goal_struct_, result_struct_, feedback_struct_, status_struct_;
  std::shared_ptr<ProxyExecutor> proxy_;

  // Proxy thread only.
  rclcpp::GenericClient::SharedPtr send_client_;
  rclcpp::GenericClient::SharedPtr result_client_;
  rclcpp::Client<action_msgs::srv::CancelGoal>::SharedPtr cancel_client_;
  rclcpp::GenericSubscription::SharedPtr feedback_sub_;
  rclcpp::Subscription<action_msgs::msg::GoalStatusArray>::SharedPtr status_sub_;
  std::map<Uuid, std::weak_ptr<GoalRun>> goals_;
};

}  // namespace

RR_SHARED_PTR<ProxyObject> make_action_client(
  ProxyManager & manager, const std::string & definition, const std::string & action,
  const std::string & type)
{
  auto obj = RR_MAKE_SHARED<ActionClientObject>(manager.typesupport(), definition, action, type);
  obj->start(manager.proxy_executor()->node(), manager.proxy_executor());
  return obj;
}

}  // namespace robotraconteur_ros2_debug_bridge
