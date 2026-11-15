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

// Parameters over Robot Raconteur (B7): the root's `parameters` object,
// experimental.ros2_debug_bridge_parameters.Parameters, in the ROS types
// (rosmsg_rcl_interfaces__*):
//
//   nodes()                       nodes whose parameters are readable
//   list(node, prefixes, depth)   ListParametersResult
//   describe(node, names)         ParameterDescriptor{list}
//   get(node, names)              ParameterValue{list}
//   set(node, parameters)         SetParametersResult{list}
//   events                        pipe of ParameterEvent (readable nodes)
//
// The verbs are the `ros2 param` ones (Robot Raconteur rejects member names
// starting with get_ or set_). Calls go to the node's parameter services
// asynchronously on the proxy thread.

#ifndef PARAMETER_PROXY_HPP_
#define PARAMETER_PROXY_HPP_

#include <string>

#include "topic_proxy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

/// The definition text of experimental.ros2_debug_bridge_parameters.
const std::string & parameters_definition();

/// Registers the definitions it needs and makes the object.
RR_SHARED_PTR<ProxyObject> make_parameters_object(ProxyManager & manager);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // PARAMETER_PROXY_HPP_
