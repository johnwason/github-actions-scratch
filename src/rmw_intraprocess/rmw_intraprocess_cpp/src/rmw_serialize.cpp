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

#include <cstring>
#include <new>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"

#include "type_support.hpp"

using rmw_intraprocess_cpp::MessageTypeSupport;

extern "C"
{
rmw_ret_t
rmw_serialize(
  const void * ros_message,
  const rosidl_message_type_support_t * type_support,
  rmw_serialized_message_t * serialized_message)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(serialized_message, RMW_RET_INVALID_ARGUMENT);
  try {
    MessageTypeSupport type;
    if (!rmw_intraprocess_cpp::resolve_message_type_support(type_support, type)) {
      return RMW_RET_ERROR;
    }
    std::vector<std::uint8_t> cdr;
    if (!rmw_intraprocess_cpp::serialize_message(type, ros_message, cdr)) {
      return RMW_RET_ERROR;
    }
    if (serialized_message->buffer_capacity < cdr.size()) {
      rmw_ret_t ret = rmw_serialized_message_resize(serialized_message, cdr.size());
      if (ret != RMW_RET_OK) {
        return ret;
      }
    }
    if (!cdr.empty()) {
      std::memcpy(serialized_message->buffer, cdr.data(), cdr.size());
    }
    serialized_message->buffer_length = cdr.size();
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while serializing");
    return RMW_RET_BAD_ALLOC;
  }
  return RMW_RET_OK;
}

rmw_ret_t
rmw_deserialize(
  const rmw_serialized_message_t * serialized_message,
  const rosidl_message_type_support_t * type_support,
  void * ros_message)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(serialized_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_message, RMW_RET_INVALID_ARGUMENT);
  try {
    MessageTypeSupport type;
    if (!rmw_intraprocess_cpp::resolve_message_type_support(type_support, type)) {
      return RMW_RET_ERROR;
    }
    if (!rmw_intraprocess_cpp::deserialize_message(
        type, serialized_message->buffer, serialized_message->buffer_length, ros_message))
    {
      return RMW_RET_ERROR;
    }
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while deserializing");
    return RMW_RET_BAD_ALLOC;
  }
  return RMW_RET_OK;
}
}  // extern "C"
