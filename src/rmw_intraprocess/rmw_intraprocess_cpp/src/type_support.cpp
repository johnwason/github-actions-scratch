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

#include "type_support.hpp"

#include <exception>
#include <initializer_list>
#include <new>
#include <string>
#include <vector>

#include "fastcdr/Cdr.h"
#include "fastcdr/FastBuffer.h"
#include "fastcdr/exceptions/Exception.h"
#include "rcutils/error_handling.h"
#include "rmw/error_handling.h"
#include "rosidl_typesupport_fastrtps_c/identifier.h"
#include "rosidl_typesupport_fastrtps_cpp/identifier.hpp"
#include "rosidl_typesupport_fastrtps_cpp/message_type_support.h"
#include "rosidl_typesupport_fastrtps_cpp/service_type_support.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

namespace rmw_intraprocess_cpp
{

namespace
{

/// "std_msgs::msg" or "std_msgs__msg" + "String" -> "std_msgs/msg/String".
std::string make_type_name(const char * message_namespace, const char * message_name)
{
  std::string ns = message_namespace != nullptr ? message_namespace : "";
  std::string out;
  out.reserve(ns.size() + 16);
  for (std::size_t i = 0; i < ns.size(); ++i) {
    if ((ns[i] == ':' || ns[i] == '_') && i + 1 < ns.size() && ns[i + 1] == ns[i]) {
      out.push_back('/');
      ++i;
    } else {
      out.push_back(ns[i]);
    }
  }
  if (!out.empty()) {
    out.push_back('/');
  }
  out += message_name != nullptr ? message_name : "";
  return out;
}

/// get_message_typesupport_handle without leaving an error behind on a miss.
const rosidl_message_type_support_t * lookup(
  const rosidl_message_type_support_t * type_support, const char * identifier)
{
  const rosidl_message_type_support_t * ts =
    get_message_typesupport_handle(type_support, identifier);
  if (ts == nullptr) {
    rcutils_reset_error();
  }
  return ts;
}

}  // namespace

bool resolve_message_type_support(
  const rosidl_message_type_support_t * type_support, MessageTypeSupport & out)
{
  if (type_support == nullptr) {
    RMW_SET_ERROR_MSG("type support is null");
    return false;
  }
  const rosidl_message_type_support_t * ts =
    lookup(type_support, rosidl_typesupport_fastrtps_cpp::typesupport_identifier);
  out.language = MessageTypeSupport::Language::kCpp;
  if (ts == nullptr) {
    ts = lookup(type_support, rosidl_typesupport_fastrtps_c__identifier);
    out.language = MessageTypeSupport::Language::kC;
  }
  if (ts == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "type support '%s' provides no rosidl_typesupport_fastrtps (C or C++) type support",
      type_support->typesupport_identifier != nullptr ? type_support->typesupport_identifier : "");
    return false;
  }
  out.cdr = static_cast<const message_type_support_callbacks_t *>(ts->data);
  out.type_name = make_type_name(out.cdr->message_namespace_, out.cdr->message_name_);
  out.type_hash = rosidl_get_zero_initialized_type_hash();
  for (const rosidl_message_type_support_t * h : {type_support, ts}) {
    if (h->get_type_hash_func != nullptr) {
      const rosidl_type_hash_t * hash = h->get_type_hash_func(h);
      if (hash != nullptr) {
        out.type_hash = *hash;
        break;
      }
    }
  }

  const rosidl_message_type_support_t * intro =
    lookup(type_support, rosidl_typesupport_introspection_cpp::typesupport_identifier);
  if (intro != nullptr) {
    out.introspection_members = intro->data;
    out.introspection_is_cpp = true;
  } else {
    intro = lookup(type_support, rosidl_typesupport_introspection_c__identifier);
    if (intro != nullptr) {
      out.introspection_members = intro->data;
      out.introspection_is_cpp = false;
    }
  }
  return true;
}

bool resolve_service_type_support(
  const rosidl_service_type_support_t * type_support, ServiceTypeSupport & out)
{
  if (type_support == nullptr) {
    RMW_SET_ERROR_MSG("service type support is null");
    return false;
  }
  const rosidl_service_type_support_t * ts = get_service_typesupport_handle(
    type_support, rosidl_typesupport_fastrtps_cpp::typesupport_identifier);
  MessageTypeSupport::Language language = MessageTypeSupport::Language::kCpp;
  if (ts == nullptr) {
    rcutils_reset_error();
    ts = get_service_typesupport_handle(type_support, rosidl_typesupport_fastrtps_c__identifier);
    language = MessageTypeSupport::Language::kC;
  }
  if (ts == nullptr) {
    rcutils_reset_error();
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "service type support '%s' provides no rosidl_typesupport_fastrtps (C or C++) type support",
      type_support->typesupport_identifier != nullptr ? type_support->typesupport_identifier : "");
    return false;
  }
  auto callbacks = static_cast<const service_type_support_callbacks_t *>(ts->data);
  out.type_name = make_type_name(callbacks->service_namespace_, callbacks->service_name_);
  const rosidl_message_type_support_t * parts[2] = {
    callbacks->request_members_, callbacks->response_members_};
  MessageTypeSupport * targets[2] = {&out.request, &out.response};
  for (int i = 0; i < 2; ++i) {
    if (parts[i] == nullptr || parts[i]->data == nullptr) {
      RMW_SET_ERROR_MSG("service type support has no request/response type support");
      return false;
    }
    targets[i]->language = language;
    targets[i]->cdr = static_cast<const message_type_support_callbacks_t *>(parts[i]->data);
    targets[i]->type_name =
      make_type_name(targets[i]->cdr->message_namespace_, targets[i]->cdr->message_name_);
  }
  return true;
}

bool serialize_message(
  const MessageTypeSupport & type, const void * ros_message, std::vector<std::uint8_t> & out)
{
  try {
    // 4 bytes of encapsulation header, as rmw_fastrtps.
    const std::size_t size = 4u + type.cdr->get_serialized_size(ros_message);
    out.resize(size);
    eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char *>(out.data()), size);
    eprosima::fastcdr::Cdr ser(
      buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
    ser.set_encoding_flag(eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
    ser.serialize_encapsulation();
    if (!type.cdr->cdr_serialize(ros_message, ser)) {
      RMW_SET_ERROR_MSG("failed to serialize ROS message");
      return false;
    }
    out.resize(ser.get_serialized_data_length());
  } catch (const eprosima::fastcdr::exception::Exception & e) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("failed to serialize ROS message: %s", e.what());
    return false;
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate serialization buffer");
    return false;
  } catch (const std::exception & e) {
    // e.g. std::length_error from a bounded sequence or string (rosidl).
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("failed to serialize ROS message: %s", e.what());
    return false;
  }
  return true;
}

bool deserialize_message(
  const MessageTypeSupport & type, const std::uint8_t * data, std::size_t size,
  void * ros_message)
{
  try {
    // FastBuffer takes a non-const pointer but deserialization only reads.
    eprosima::fastcdr::FastBuffer buffer(
      const_cast<char *>(reinterpret_cast<const char *>(data)), size);
    eprosima::fastcdr::Cdr deser(
      buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
    deser.read_encapsulation();
    if (!type.cdr->cdr_deserialize(deser, ros_message)) {
      RMW_SET_ERROR_MSG("failed to deserialize ROS message");
      return false;
    }
  } catch (const eprosima::fastcdr::exception::Exception & e) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("failed to deserialize ROS message: %s", e.what());
    return false;
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while deserializing");
    return false;
  } catch (const std::exception & e) {
    // Malformed data: e.g. std::length_error ("Exceeded upper bound") when a
    // bounded sequence declares more elements than its bound (rosidl
    // BoundedVector). Must not escape the C API (found by test_cdr_fuzz).
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("failed to deserialize ROS message: %s", e.what());
    return false;
  }
  return true;
}

}  // namespace rmw_intraprocess_cpp
