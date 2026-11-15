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

// Actions over Robot Raconteur (B8): rosaction_<pkg>__<A>.rosactionclient
//
//   function <A>GoalStatus{generator} send_goal(<A>Goal goal)
//   function bool wait_for_server(double timeout)
//
// Each Next of the generator gives the goal's next status: ACCEPTED, then
// EXECUTING (with feedback as it comes, and on status changes), then the
// final status with the result; then the generator ends. A rejected goal
// raises an error. Abort or Close cancels a goal still running.
//
// rclcpp_action (Jazzy) has no client for types known only at run time, so
// this speaks the action protocol directly: the send_goal and get_result
// services (generic clients), cancel_goal, and the feedback and status
// topics, all on the proxy thread.

#ifndef ACTION_PROXY_HPP_
#define ACTION_PROXY_HPP_

#include <string>

#include "topic_proxy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

/// `type`: "pkg/action/Name". Creates the ROS entities.
RR_SHARED_PTR<ProxyObject> make_action_client(
  ProxyManager & manager, const std::string & definition, const std::string & action,
  const std::string & type);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // ACTION_PROXY_HPP_
