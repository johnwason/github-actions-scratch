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

#include "component_proxy.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "composition_interfaces/srv/list_nodes.hpp"
#include "composition_interfaces/srv/load_node.hpp"
#include "composition_interfaces/srv/unload_node.hpp"
#include "proxy_executor.hpp"
#include "value_converter.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

namespace
{

constexpr const char * kDefinitionName = "experimental.ros2_debug_bridge_components";
// Loading constructs a node, which can take a while.
constexpr std::chrono::seconds kCallDeadline{30};
const char kLoadSuffix[] = "/_container/load_node";

using LoadNode = composition_interfaces::srv::LoadNode;
using UnloadNode = composition_interfaces::srv::UnloadNode;
using ListNodes = composition_interfaces::srv::ListNodes;

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

std::string container_name(const std::vector<ElementPtr> & params)
{
  const auto e = find(params, "container");
  if (e->ElementType != RR::DataTypes_string_t) {
    throw RR::InvalidArgumentException("container: expected a string");
  }
  const std::string n = RR::RRArrayToString(e->CastData<RR::RRArray<char>>());
  if (n.empty()) {
    throw RR::InvalidArgumentException("container: empty");
  }
  return n[0] == '/' ? n : "/" + n;
}

class ComponentsObject : public ProxyObject, public RR_ENABLE_SHARED_FROM_THIS<ComponentsObject>
{
public:
  explicit ComponentsObject(ProxyManager & manager)
  : ProxyObject(std::string(kDefinitionName) + ".Components"), manager_(manager),
    proxy_(manager.proxy_executor())
  {
    for (const char * t : {"LoadNode_Request", "LoadNode_Response", "UnloadNode_Response",
        "ListNodes_Response"})
    {
      members_[t] = manager.typesupport().message(
        Flavor::Cpp, std::string("composition_interfaces/srv/") + t);
    }
    const auto struct_of = [](const char * srv, const char * part) {
        return service_definition_name("composition_interfaces", srv) + "." +
               fixname(std::string(srv) + part);
      };
    structs_["LoadNode_Request"] = struct_of("LoadNode", "Request");
    structs_["LoadNode_Response"] = struct_of("LoadNode", "Response");
    structs_["UnloadNode_Response"] = struct_of("UnloadNode", "Response");
    structs_["ListNodes_Response"] = struct_of("ListNodes", "Response");
  }

  ~ComponentsObject() override = default;  // closed by the manager

  void close() override
  {
    auto self = shared_from_this();
    try {
      proxy_->post([self]() {self->clients_.clear();});
    } catch (const std::exception &) {
      clients_.clear();  // stopped: nothing else runs any more
    }
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done) override
  {
    if (name == "containers") {
      std::vector<ElementPtr> items;
      std::vector<std::string> found;
      for (const auto & kv : manager_.node().get_service_names_and_types()) {
        const std::string & s = kv.first;
        const size_t n = sizeof(kLoadSuffix) - 1;
        if (s.size() > n && s.compare(s.size() - n, n, kLoadSuffix) == 0) {
          const std::string c = s.substr(0, s.size() - n);
          if (manager_.policy().container_usable(c)) {
            found.push_back(c);
          }
        }
      }
      std::sort(found.begin(), found.end());
      for (size_t i = 0; i < found.size(); ++i) {
        items.push_back(
          RR::CreateMessageElement(static_cast<int32_t>(i), RR::stringToRRArray(found[i])));
      }
      done(
        RR::CreateMessageElement(
          "return",
          RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(items))),
        RRErrorPtr());
      return;
    }
    const std::string container = container_name(params);
    if (!manager_.policy().container_usable(container)) {
      throw RR::InvalidArgumentException("Container '" + container + "' is not available");
    }
    if (name == "load") {
      auto req = std::make_shared<LoadNode::Request>();
      const auto s = element_structure(find(params, "request"));
      if (!s) {
        throw RR::InvalidArgumentException("request: expected " + structs_.at("LoadNode_Request"));
      }
      try {
        rr_to_ros(
          Flavor::Cpp, members_.at("LoadNode_Request"), s, req.get(),
          structs_.at("LoadNode_Request"));
      } catch (const ConversionError & e) {
        throw RR::InvalidArgumentException(std::string("request: ") + e.what());
      }
      call<LoadNode>(container, "load_node", req, "LoadNode_Response", done);
    } else if (name == "unload") {
      const auto id = find(params, "unique_id");
      if (id->ElementType != RR::DataTypes_uint64_t) {
        throw RR::InvalidArgumentException("unique_id: expected a uint64");
      }
      auto req = std::make_shared<UnloadNode::Request>();
      req->unique_id = RR::RRArrayToScalar(id->CastData<RR::RRArray<uint64_t>>());
      call<UnloadNode>(container, "unload_node", req, "UnloadNode_Response", done);
    } else if (name == "list") {
      call<ListNodes>(
        container, "list_nodes", std::make_shared<ListNodes::Request>(), "ListNodes_Response",
        done);
    } else {
      GenericObject::call_function(name, params, done);
    }
  }

private:
  // On the proxy thread: the container's client (made once), the request,
  // and a deadline.
  template<class Srv>
  void call(
    const std::string & container, const char * service,
    const std::shared_ptr<typename Srv::Request> & req, const char * response_type,
    const Completion & done)
  {
    RR_WEAK_PTR<ComponentsObject> weak = shared_from_this();
    auto proxy = proxy_;
    const std::string name = container + "/_container/" + service;
    const std::string rt = response_type;
    proxy->post(
      [weak, name, req, rt, done, proxy]() {
        auto o = weak.lock();
        if (!o) {
          done(ElementPtr(), error("closed"));
          return;
        }
        auto & slot = o->clients_[name];
        if (!slot) {
          slot = proxy->node().create_client<Srv>(name, rclcpp::ServicesQoS(), proxy->group());
        }
        auto client = std::static_pointer_cast<rclcpp::Client<Srv>>(slot);
        if (!client->service_is_ready()) {
          done(ElementPtr(), error(name + ": not available"));
          return;
        }
        client->async_send_request(
          req, [weak, rt, done](typename rclcpp::Client<Srv>::SharedFuture f) {
            auto o2 = weak.lock();
            if (!o2) {
              return;
            }
            const auto res = f.get();
            done(
              RR::CreateMessageElement(
                "return",
                ros_to_rr(Flavor::Cpp, o2->members_.at(rt), res.get(), o2->structs_.at(rt))),
              RRErrorPtr());
          });
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

  ProxyManager & manager_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::map<std::string, const void *> members_;
  std::map<std::string, std::string> structs_;
  std::map<std::string, std::shared_ptr<rclcpp::ClientBase>> clients_;  // proxy thread
};

}  // namespace

const std::string & components_definition()
{
  static const std::string text =
    R"(service experimental.ros2_debug_bridge_components

stdver 0.10

import rosservice_composition_interfaces__LoadNode
import rosservice_composition_interfaces__UnloadNode
import rosservice_composition_interfaces__ListNodes

# B9: components in ROS component containers (their composition services).
# Usable containers are the exposure rules' (components.allow), with the
# load_component capability.
object Components
    function string{list} containers()
    function rosservice_composition_interfaces__LoadNode.LoadNodeResponse load(string container, rosservice_composition_interfaces__LoadNode.LoadNodeRequest request)
    function rosservice_composition_interfaces__UnloadNode.UnloadNodeResponse unload(string container, uint64 unique_id)
    function rosservice_composition_interfaces__ListNodes.ListNodesResponse list(string container)
end
)";
  return text;
}

RR_SHARED_PTR<ProxyObject> make_components_object(ProxyManager & manager)
{
  for (const char * t : {"composition_interfaces/srv/LoadNode",
      "composition_interfaces/srv/UnloadNode", "composition_interfaces/srv/ListNodes"})
  {
    manager.register_definitions(manager.mapper().service(t));
  }
  manager.register_definition_text(kDefinitionName, components_definition());
  return RR_MAKE_SHARED<ComponentsObject>(manager);
}

}  // namespace robotraconteur_ros2_debug_bridge
