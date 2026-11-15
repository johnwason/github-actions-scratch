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

// Services over Robot Raconteur (B6), as in the upstream bridge:
//
//   rosservice_<pkg>__<Srv>.rosclient   call(request), wait_for_service(timeout)
//   rosservice_<pkg>__<Srv>.rosservice  callback servicefunction(request)
//
// A rosclient is a generic ROS client on the proxy thread; call() and
// wait_for_service() complete asynchronously (no Robot Raconteur thread
// waits). A rosservice is a ROS service server whose requests go to the
// owning client's servicefunction callback.

#ifndef SERVICE_PROXY_HPP_
#define SERVICE_PROXY_HPP_

#include <cstdint>
#include <string>

#include "topic_proxy.hpp"

struct rosidl_service_type_support_t;

namespace robotraconteur_ros2_debug_bridge
{

/// `members`: the C++ introspection ServiceMembers. Creates the ROS client.
RR_SHARED_PTR<ProxyObject> make_service_client(
  ProxyManager & manager, const std::string & definition, const std::string & service,
  const std::string & type, const void * members);

/// Creates the ROS service; requests go to client `owner`.
RR_SHARED_PTR<ProxyObject> make_service_server(
  ProxyManager & manager, const std::string & definition, const std::string & service,
  const std::string & type, const void * members, const rosidl_service_type_support_t * handle,
  uint32_t owner);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // SERVICE_PROXY_HPP_
