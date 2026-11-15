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

#ifndef ROOT_OBJECT_HPP_
#define ROOT_OBJECT_HPP_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RobotRaconteur.h"
#include "robotraconteur_generated.h"  // NOLINT(build/include_subdir): generated

namespace robotraconteur_ros2_debug_bridge
{

/// Facts about the bridge and its ROS process, fixed at start-up.
struct BridgeIdentity
{
  std::string ros_node_name;
  std::string ros_namespace;
  std::string rmw_implementation;
  std::string bridge_version;
  std::vector<std::string> capabilities;
};

class ProxyManager;
class GraphMonitor;

/// The registered root object (experimental.ros2_debug_bridge.ROS2DebugBridge).
/// Called on Robot Raconteur thread-pool threads. The identity is
/// immutable; handles go to the thread-safe ProxyManager.
class RootObject : public experimental::ros2_debug_bridge::ROS2DebugBridge,
  public RobotRaconteur::IRRServiceObject
{
public:
  RootObject(
    BridgeIdentity identity, std::shared_ptr<ProxyManager> proxies,
    std::shared_ptr<GraphMonitor> graph);

  void RRServiceObjectInit(
    RR_WEAK_PTR<RobotRaconteur::ServerContext> context, const std::string & service_path) override;

  std::string get_ros_node_name() override;
  std::string get_ros_namespace() override;
  std::string get_rmw_implementation() override;
  std::string get_bridge_version() override;
  RobotRaconteur::RRListPtr<RobotRaconteur::RRArray<char>> get_capabilities() override;

  int32_t subscribe(const std::string & topic, const std::string & msgtype) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_subscribers(int32_t ind) override;
  int32_t publish(const std::string & topic, const std::string & msgtype) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_publishers(int32_t ind) override;
  int32_t client(const std::string & service, const std::string & srvtype) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_clients(int32_t ind) override;
  int32_t register_service(const std::string & service, const std::string & srvtype) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_services(int32_t ind) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_parameters() override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_components() override;
  int32_t action_client(const std::string & action, const std::string & actiontype) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> get_action_clients(int32_t ind) override;

  using GraphPtr = RR_INTRUSIVE_PTR<experimental::ros2_debug_bridge::Graph>;
  GraphPtr get_graph() override;
  RR_SHARED_PTR<RobotRaconteur::Wire<GraphPtr>> get_graph_changes() override;
  void set_graph_changes(const RR_SHARED_PTR<RobotRaconteur::Wire<GraphPtr>> & value) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRList<experimental::ros2_debug_bridge::TopicEndpoint>>
  topic_endpoints(const std::string & topic) override;

  /// A new graph version for graph_changes (from the bridge's poll timer).
  void graph_changed();

private:
  const BridgeIdentity identity_;
  const std::shared_ptr<ProxyManager> proxies_;
  const std::shared_ptr<GraphMonitor> graph_;
  std::mutex wire_mutex_;
  RR_SHARED_PTR<RobotRaconteur::Wire<GraphPtr>> graph_wire_;
  RR_SHARED_PTR<RobotRaconteur::WireBroadcaster<GraphPtr>> graph_broadcaster_;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // ROOT_OBJECT_HPP_
