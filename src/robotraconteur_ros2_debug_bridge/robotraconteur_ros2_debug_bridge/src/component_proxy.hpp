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

// Component loading over Robot Raconteur (B9): the root's `components`
// object, experimental.ros2_debug_bridge_components.Components:
//
//   containers()                 component containers the rules allow
//   load(container, request)     composition_interfaces/LoadNode
//   unload(container, unique_id) composition_interfaces/UnloadNode
//   list(container)              composition_interfaces/ListNodes
//
// It uses the containers' own composition services
// (<container>/_container/...), so components load into whatever container
// runs them: rclcpp_components' component_container, or rmw_intraprocess's
// container_host. Needs the load_component capability (off by default).

#ifndef COMPONENT_PROXY_HPP_
#define COMPONENT_PROXY_HPP_

#include <string>

#include "topic_proxy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

const std::string & components_definition();

RR_SHARED_PTR<ProxyObject> make_components_object(ProxyManager & manager);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // COMPONENT_PROXY_HPP_
