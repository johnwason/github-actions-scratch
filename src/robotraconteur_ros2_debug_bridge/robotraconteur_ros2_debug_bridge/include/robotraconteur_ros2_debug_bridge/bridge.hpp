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

// Public API of robotraconteur_ros2_debug_bridge. No Robot Raconteur or Boost
// types appear here: they stay behind the library boundary (Windows CRT/Boost
// isolation; scripts/in-container/check-public-headers.sh).
//
// The usual way to run the bridge is as a component
// (robotraconteur_ros2_debug_bridge::DebugBridge, registered with
// rclcpp_components) in the application's process. make_bridge_node() is for
// programs that compose nodes in code.

#ifndef ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE__BRIDGE_HPP_
#define ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE__BRIDGE_HPP_

#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"
#include "robotraconteur_ros2_debug_bridge/visibility_control.hpp"

namespace robotraconteur_ros2_debug_bridge
{

/// Creates the bridge node (ROS node name robotraconteur_ros2_debug_bridge).
/// Parameter config_file: path of the YAML configuration (default: the
/// package's config/default.yaml). Throws std::exception if the configuration
/// is invalid or Robot Raconteur cannot start.
ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_PUBLIC
rclcpp::Node::SharedPtr make_bridge_node(
  const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE__BRIDGE_HPP_
