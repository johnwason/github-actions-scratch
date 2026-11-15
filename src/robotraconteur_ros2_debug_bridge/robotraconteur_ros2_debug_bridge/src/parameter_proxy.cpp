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

#include "parameter_proxy.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "proxy_executor.hpp"
#include "rcl_interfaces/msg/list_parameters_result.hpp"
#include "rcl_interfaces/msg/parameter.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rcl_interfaces/msg/parameter_value.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/parameter_client.hpp"
#include "value_converter.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

namespace
{

constexpr const char * kDefinitionName = "experimental.ros2_debug_bridge_parameters";
constexpr std::chrono::seconds kCallDeadline{10};

const char * const kRosTypes[] = {
  "rcl_interfaces/msg/Parameter", "rcl_interfaces/msg/ParameterValue",
  "rcl_interfaces/msg/ParameterDescriptor", "rcl_interfaces/msg/SetParametersResult",
  "rcl_interfaces/msg/ListParametersResult", "rcl_interfaces/msg/ParameterEvent"};

RRErrorPtr error(const std::string & what)
{
  return RR_MAKE_SHARED<RR::OperationFailedException>(what);
}

ElementPtr find(const std::vector<ElementPtr> & params, const char * name)
{
  for (const auto & p : params) {
    if (p->ElementName.str() == name) {
      return p;
    }
  }
  throw RR::InvalidArgumentException(std::string("parameter ") + name + " missing");
}

std::string string_param(const std::vector<ElementPtr> & params, const char * name)
{
  const auto e = find(params, name);
  if (e->ElementType != RR::DataTypes_string_t) {
    throw RR::InvalidArgumentException(std::string(name) + ": expected a string");
  }
  return RR::RRArrayToString(e->CastData<RR::RRArray<char>>());
}

std::vector<ElementPtr> list_param(const std::vector<ElementPtr> & params, const char * name)
{
  const auto e = find(params, name);
  if (e->ElementType == RR::DataTypes_void_t) {
    return {};
  }
  if (e->ElementType != RR::DataTypes_list_t) {
    throw RR::InvalidArgumentException(std::string(name) + ": expected a list");
  }
  return e->CastDataToNestedList()->Elements;
}

std::vector<std::string> strings_param(const std::vector<ElementPtr> & params, const char * name)
{
  std::vector<std::string> out;
  for (const auto & i : list_param(params, name)) {
    if (i->ElementType != RR::DataTypes_string_t) {
      throw RR::InvalidArgumentException(std::string(name) + ": expected strings");
    }
    out.push_back(RR::RRArrayToString(i->CastData<RR::RRArray<char>>()));
  }
  return out;
}

ElementPtr list_return(const std::vector<RR_INTRUSIVE_PTR<RR::MessageElementData>> & items)
{
  std::vector<ElementPtr> e;
  for (size_t i = 0; i < items.size(); ++i) {
    e.push_back(RR::CreateMessageElement(static_cast<int32_t>(i), items[i]));
  }
  return RR::CreateMessageElement(
    "return", RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(e)));
}

std::string node_name(const std::string & n)
{
  if (n.empty()) {
    throw RR::InvalidArgumentException("node: empty");
  }
  return n[0] == '/' ? n : "/" + n;
}

class ParametersObject : public ProxyObject, public RR_ENABLE_SHARED_FROM_THIS<ParametersObject>
{
public:
  explicit ParametersObject(ProxyManager & manager)
  : ProxyObject(std::string(kDefinitionName) + ".Parameters"), manager_(manager),
    proxy_(manager.proxy_executor()), events_(std::make_shared<PipeFanout>(64))
  {
    for (const char * t : kRosTypes) {
      const auto it = parse_interface_type(t);
      members_[it.name] = manager.typesupport().message(Flavor::Cpp, t);
      structs_[it.name] = message_definition_name(it.package, it.name) + "." + fixname(it.name);
    }
  }

  ~ParametersObject() override {close();}

  void start()
  {
    RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
    auto sub = proxy_->run(
      [&]() {
        return proxy_->node().create_subscription<rcl_interfaces::msg::ParameterEvent>(
          "/parameter_events", rclcpp::ParameterEventsQoS(),
          [weak](const rcl_interfaces::msg::ParameterEvent & ev) {
            if (auto o = weak.lock()) {
              o->on_event(ev);
            }
          });
      });
    std::lock_guard<std::mutex> lock(mutex_);
    events_sub_ = sub;
  }

  void attach(GenericSkel & skel) override
  {
    if (auto p = skel.pipe("events")) {
      events_->attach(p);
    }
  }

  void close() override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroy_on_proxy_thread(proxy_, std::move(events_sub_));
    for (auto & kv : clients_) {
      destroy_on_proxy_thread(proxy_, std::move(kv.second));
    }
    clients_.clear();
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done) override
  {
    if (name == "nodes") {
      std::vector<RR_INTRUSIVE_PTR<RR::MessageElementData>> items;
      auto all = manager_.node().get_node_names();
      std::sort(all.begin(), all.end());
      all.erase(std::unique(all.begin(), all.end()), all.end());
      for (const auto & n : all) {
        if (manager_.policy().parameters_readable(n)) {
          items.push_back(RR::stringToRRArray(n));
        }
      }
      done(list_return(items), RRErrorPtr());
      return;
    }
    const std::string node = node_name(string_param(params, "node"));
    const bool writing = name == "set";
    if (writing ? !manager_.policy().parameters_settable(node) :
      !manager_.policy().parameters_readable(node))
    {
      throw RR::InvalidArgumentException(
              "Parameters of node '" + node + "' are not available" +
              (writing ? " for setting" : ""));
    }
    if (name == "list") {
      const auto prefixes = strings_param(params, "prefixes");
      const auto d = find(params, "depth");
      if (d->ElementType != RR::DataTypes_uint64_t) {
        throw RR::InvalidArgumentException("depth: expected a uint64");
      }
      const uint64_t depth = RR::RRArrayToScalar(d->CastData<RR::RRArray<uint64_t>>());
      with_client(
        node, done, [this, prefixes, depth, done](rclcpp::AsyncParametersClient & c) {
          RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
          c.list_parameters(
            prefixes, depth,
            [weak, done](std::shared_future<rcl_interfaces::msg::ListParametersResult> f) {
              auto o = weak.lock();
              if (!o) {
                return;
              }
              auto r = f.get();
              done(RR::CreateMessageElement("return", o->to_rr("ListParametersResult", &r)),
                RRErrorPtr());
            });
        });
    } else if (name == "describe") {
      const auto names = strings_param(params, "names");
      with_client(
        node, done, [this, names, done](rclcpp::AsyncParametersClient & c) {
          RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
          c.describe_parameters(
            names,
            [weak, done](
              std::shared_future<std::vector<rcl_interfaces::msg::ParameterDescriptor>> f) {
              auto o = weak.lock();
              if (!o) {
                return;
              }
              std::vector<RR_INTRUSIVE_PTR<RR::MessageElementData>> items;
              for (const auto & d : f.get()) {
                items.push_back(o->to_rr("ParameterDescriptor", &d));
              }
              done(list_return(items), RRErrorPtr());
            });
        });
    } else if (name == "get") {
      const auto names = strings_param(params, "names");
      with_client(
        node, done, [this, names, done](rclcpp::AsyncParametersClient & c) {
          RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
          c.get_parameters(
            names, [weak, done](std::shared_future<std::vector<rclcpp::Parameter>> f) {
              auto o = weak.lock();
              if (!o) {
                return;
              }
              std::vector<RR_INTRUSIVE_PTR<RR::MessageElementData>> items;
              for (const auto & p : f.get()) {
                const auto v = p.get_value_message();
                items.push_back(o->to_rr("ParameterValue", &v));
              }
              done(list_return(items), RRErrorPtr());
            });
        });
    } else if (name == "set") {
      std::vector<rclcpp::Parameter> values;
      for (const auto & e : list_param(params, "parameters")) {
        rcl_interfaces::msg::Parameter p;
        const auto s = element_structure(e);
        if (!s) {
          throw RR::InvalidArgumentException("parameters: expected " + structs_.at("Parameter"));
        }
        try {
          rr_to_ros(Flavor::Cpp, members_.at("Parameter"), s, &p, structs_.at("Parameter"));
          values.push_back(rclcpp::Parameter::from_parameter_msg(p));
        } catch (const std::exception & ex) {
          throw RR::InvalidArgumentException(std::string("parameters: ") + ex.what());
        }
      }
      with_client(
        node, done, [this, values, done](rclcpp::AsyncParametersClient & c) {
          RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
          c.set_parameters(
            values,
            [weak, done](
              std::shared_future<std::vector<rcl_interfaces::msg::SetParametersResult>> f) {
              auto o = weak.lock();
              if (!o) {
                return;
              }
              std::vector<RR_INTRUSIVE_PTR<RR::MessageElementData>> items;
              for (const auto & r : f.get()) {
                items.push_back(o->to_rr("SetParametersResult", &r));
              }
              done(list_return(items), RRErrorPtr());
            });
        });
    } else {
      GenericObject::call_function(name, params, done);
    }
  }

private:
  RR_INTRUSIVE_PTR<RR::MessageElementData> to_rr(const char * type, const void * msg) const
  {
    return ros_to_rr(Flavor::Cpp, members_.at(type), msg, structs_.at(type));
  }

  // On the proxy thread: the node's parameter client (made once), then
  // `send`, with a deadline for nodes that never answer.
  void with_client(
    const std::string & node, const Completion & done,
    std::function<void(rclcpp::AsyncParametersClient &)> send)
  {
    RR_WEAK_PTR<ParametersObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->post(
      [weak, node, done, send, proxy]() {
        auto o = weak.lock();
        if (!o) {
          done(ElementPtr(), error("closed"));
          return;
        }
        std::shared_ptr<rclcpp::AsyncParametersClient> c;
        {
          std::lock_guard<std::mutex> lock(o->mutex_);
          auto & slot = o->clients_[node];
          if (!slot) {
            slot = std::make_shared<rclcpp::AsyncParametersClient>(
              proxy->node().get_node_base_interface(), proxy->node().get_node_topics_interface(),
              proxy->node().get_node_graph_interface(),
              proxy->node().get_node_services_interface(), node);
          }
          c = slot;
        }
        if (!c->service_is_ready()) {
          done(ElementPtr(), error("node '" + node + "' has no parameter services"));
          return;
        }
        send(*c);
        // The skel completes once: a late answer after this is dropped.
        const auto deadline = std::chrono::steady_clock::now() + kCallDeadline;
        proxy->add_poller(
          [done, deadline]() {
            if (std::chrono::steady_clock::now() < deadline) {
              return false;
            }
            done(ElementPtr(), RR_MAKE_SHARED<RR::RequestTimeoutException>("no answer"));
            return true;
          });
      });
  }

  void on_event(const rcl_interfaces::msg::ParameterEvent & ev)
  {
    if (!manager_.policy().parameters_readable(ev.node)) {
      return;
    }
    events_->send(RR::CreateMessageElement("value", to_rr("ParameterEvent", &ev)));
  }

  ProxyManager & manager_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::map<std::string, const void *> members_;
  std::map<std::string, std::string> structs_;
  std::shared_ptr<PipeFanout> events_;
  std::mutex mutex_;
  rclcpp::Subscription<rcl_interfaces::msg::ParameterEvent>::SharedPtr events_sub_;
  std::map<std::string, std::shared_ptr<rclcpp::AsyncParametersClient>> clients_;
};

}  // namespace

const std::string & parameters_definition()
{
  static const std::string text =
    R"(service experimental.ros2_debug_bridge_parameters

stdver 0.10

import rosmsg_rcl_interfaces__Parameter
import rosmsg_rcl_interfaces__ParameterValue
import rosmsg_rcl_interfaces__ParameterDescriptor
import rosmsg_rcl_interfaces__SetParametersResult
import rosmsg_rcl_interfaces__ListParametersResult
import rosmsg_rcl_interfaces__ParameterEvent

# B7: parameters of ROS nodes, in the ROS types. Readable and settable nodes
# are the exposure rules' (parameters.allow, parameters.set).
object Parameters
    function string{list} nodes()
    function rosmsg_rcl_interfaces__ListParametersResult.ListParametersResult list(string node, string{list} prefixes, uint64 depth)
    function rosmsg_rcl_interfaces__ParameterDescriptor.ParameterDescriptor{list} describe(string node, string{list} names)
    function rosmsg_rcl_interfaces__ParameterValue.ParameterValue{list} get(string node, string{list} names)
    function rosmsg_rcl_interfaces__SetParametersResult.SetParametersResult{list} set(string node, rosmsg_rcl_interfaces__Parameter.Parameter{list} parameters)
    pipe rosmsg_rcl_interfaces__ParameterEvent.ParameterEvent events [readonly]
end
)";
  return text;
}

RR_SHARED_PTR<ProxyObject> make_parameters_object(ProxyManager & manager)
{
  for (const char * t : kRosTypes) {
    manager.register_definitions(manager.mapper().message(t));
  }
  manager.register_definition_text(kDefinitionName, parameters_definition());
  auto obj = RR_MAKE_SHARED<ParametersObject>(manager);
  obj->start();
  return obj;
}

}  // namespace robotraconteur_ros2_debug_bridge
