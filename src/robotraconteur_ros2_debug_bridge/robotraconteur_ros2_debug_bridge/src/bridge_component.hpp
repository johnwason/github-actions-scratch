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

#ifndef BRIDGE_COMPONENT_HPP_
#define BRIDGE_COMPONENT_HPP_

#include <memory>
#include <string>

#include "RobotRaconteur.h"
#include "rclcpp/rclcpp.hpp"

#include "config.hpp"

namespace robotraconteur_ros2_debug_bridge
{

class RootObject;
class ProxyManager;
class GraphMonitor;

/// The bridge component. Owns a private Robot Raconteur node (not the
/// process-wide singleton, so the component can be loaded, unloaded and
/// loaded again, and several can coexist with distinct node names).
///
/// Lifecycle: the constructor reads the configuration, starts the Robot
/// Raconteur transports it allows and registers the root object; the
/// destructor stops Robot Raconteur first (no more client calls, transport
/// and thread-pool threads joined), then releases the root object, and only
/// then lets the ROS node go (CLAUDE.md, "Shutdown order").
class DebugBridge : public rclcpp::Node
{
public:
  explicit DebugBridge(const rclcpp::NodeOptions & options);
  ~DebugBridge() override;

  const BridgeConfig & config() const {return config_;}

private:
  void start_robotraconteur();
  void stop_robotraconteur() noexcept;

  BridgeConfig config_;
  RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> rr_node_;
  std::unique_ptr<RobotRaconteur::RobotRaconteurNodeSetup> rr_setup_;
  std::shared_ptr<ProxyManager> proxies_;
  std::shared_ptr<GraphMonitor> graph_;
  rclcpp::TimerBase::SharedPtr graph_timer_;
  RR_SHARED_PTR<RootObject> root_;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // BRIDGE_COMPONENT_HPP_
