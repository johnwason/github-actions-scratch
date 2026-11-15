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

#ifndef TYPE_SUPPORT_HPP_
#define TYPE_SUPPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_runtime_c/type_hash.h"

struct message_type_support_callbacks_t;

namespace rmw_intraprocess_cpp
{

/// A message type resolved to the type supports this RMW uses.
struct MessageTypeSupport
{
  enum class Language { kC, kCpp };

  /// Language of the CDR (fastrtps) type support.
  Language language = Language::kCpp;
  /// rosidl_typesupport_fastrtps_{c,cpp} callbacks: required (CDR transport).
  const message_type_support_callbacks_t * cdr = nullptr;
  /// rosidl_typesupport_introspection_{c,cpp} MessageMembers, if available
  /// (used by later milestones: graph type info, tap API).
  const void * introspection_members = nullptr;
  bool introspection_is_cpp = false;
  /// ROS type name, e.g. "std_msgs/msg/String".
  std::string type_name;
  /// RIHS type hash (graph endpoint info); zero if the type support has none.
  rosidl_type_hash_t type_hash = rosidl_get_zero_initialized_type_hash();
};

/// A service type: CDR type supports for request and response.
struct ServiceTypeSupport
{
  MessageTypeSupport request;
  MessageTypeSupport response;
  /// ROS service type name, e.g. "example_interfaces/srv/AddTwoInts".
  std::string type_name;
};

/// Resolves a (possibly dispatching) service type support handle (fastrtps C++
/// first, then C). On failure, sets the rmw error state and returns false.
bool resolve_service_type_support(
  const rosidl_service_type_support_t * type_support, ServiceTypeSupport & out);

/// Resolves a (possibly dispatching) message type support handle. The CDR type
/// support is required: C++ is tried first, then C. On failure, sets the rmw
/// error state and returns false.
bool resolve_message_type_support(
  const rosidl_message_type_support_t * type_support, MessageTypeSupport & out);

/// Serializes a ROS message to CDR (with encapsulation header) into out.
/// On failure, sets the rmw error state and returns false.
bool serialize_message(
  const MessageTypeSupport & type, const void * ros_message, std::vector<std::uint8_t> & out);

/// Deserializes CDR data into an initialized ROS message.
/// On failure (including malformed data), sets the rmw error state and returns false.
bool deserialize_message(
  const MessageTypeSupport & type, const std::uint8_t * data, std::size_t size,
  void * ros_message);

}  // namespace rmw_intraprocess_cpp

#endif  // TYPE_SUPPORT_HPP_
