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

// Compiled directly into the shared library (not the static core): the
// component registration is a static initializer and make_bridge_node() is
// referenced only from outside, so neither may live in a static library the
// linker could drop.

#include <memory>

#include "rclcpp_components/register_node_macro.hpp"
#include "robotraconteur_ros2_debug_bridge/bridge.hpp"

#include "bridge_component.hpp"

namespace robotraconteur_ros2_debug_bridge
{

rclcpp::Node::SharedPtr make_bridge_node(const rclcpp::NodeOptions & options)
{
  return std::make_shared<DebugBridge>(options);
}

}  // namespace robotraconteur_ros2_debug_bridge

RCLCPP_COMPONENTS_REGISTER_NODE(robotraconteur_ros2_debug_bridge::DebugBridge)
