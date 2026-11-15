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

// RMW API functions that are not implemented (yet). Each returns
// RMW_RET_UNSUPPORTED (or nullptr / false) and sets an error message naming
// the milestone that implements it, or why it is not supported.

#include "rmw/dynamic_message_type_support.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"

namespace
{

/// Publisher/subscription allocations: unsupported, as in rmw_fastrtps_cpp
/// (rclcpp works without them).
constexpr char kAllocationsUnsupported[] = "not supported (as in rmw_fastrtps_cpp)";

rmw_ret_t unsupported(const char * function, const char * reason)
{
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s: %s (rmw_intraprocess_cpp)", function, reason);
  return RMW_RET_UNSUPPORTED;
}

}  // namespace

extern "C"
{
rmw_ret_t
rmw_init_publisher_allocation(
  const rosidl_message_type_support_t *,
  const rosidl_runtime_c__Sequence__bound *,
  rmw_publisher_allocation_t *)
{
  return unsupported("rmw_init_publisher_allocation", kAllocationsUnsupported);
}

rmw_ret_t
rmw_fini_publisher_allocation(rmw_publisher_allocation_t * allocation)
{
  (void)allocation;
  return unsupported("rmw_fini_publisher_allocation", kAllocationsUnsupported);
}

rmw_ret_t
rmw_borrow_loaned_message(const rmw_publisher_t *, const rosidl_message_type_support_t *, void **)
{
  return unsupported(
    "rmw_borrow_loaned_message",
    "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_return_loaned_message_from_publisher(const rmw_publisher_t *, void *)
{
  return unsupported(
    "rmw_return_loaned_message_from_publisher",
    "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_publish_loaned_message(const rmw_publisher_t *, void *, rmw_publisher_allocation_t *)
{
  return unsupported(
    "rmw_publish_loaned_message",
    "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_get_serialized_message_size(
  const rosidl_message_type_support_t *,
  const rosidl_runtime_c__Sequence__bound *,
  size_t *)
{
  return unsupported("rmw_get_serialized_message_size", "not supported (as in rmw_fastrtps_cpp)");
}

rmw_ret_t
rmw_init_subscription_allocation(
  const rosidl_message_type_support_t *,
  const rosidl_runtime_c__Sequence__bound *,
  rmw_subscription_allocation_t *)
{
  return unsupported("rmw_init_subscription_allocation", kAllocationsUnsupported);
}

rmw_ret_t
rmw_fini_subscription_allocation(rmw_subscription_allocation_t * allocation)
{
  (void)allocation;
  return unsupported("rmw_fini_subscription_allocation", kAllocationsUnsupported);
}

rmw_ret_t
rmw_subscription_set_content_filter(
  rmw_subscription_t *,
  const rmw_subscription_content_filter_options_t *)
{
  return unsupported(
    "rmw_subscription_set_content_filter",
    "content filtered topics are not supported");
}

rmw_ret_t
rmw_subscription_get_content_filter(
  const rmw_subscription_t *,
  rcutils_allocator_t *,
  rmw_subscription_content_filter_options_t *)
{
  return unsupported(
    "rmw_subscription_get_content_filter",
    "content filtered topics are not supported");
}

rmw_ret_t
rmw_take_loaned_message(
  const rmw_subscription_t *,
  void **,
  bool *,
  rmw_subscription_allocation_t *)
{
  return unsupported("rmw_take_loaned_message", "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_take_loaned_message_with_info(
  const rmw_subscription_t *,
  void **,
  bool *,
  rmw_message_info_t *,
  rmw_subscription_allocation_t *)
{
  return unsupported(
    "rmw_take_loaned_message_with_info",
    "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_return_loaned_message_from_subscription(const rmw_subscription_t *, void *)
{
  return unsupported(
    "rmw_return_loaned_message_from_subscription",
    "loaned messages are not supported (PLAN.md Q4)");
}

rmw_ret_t
rmw_publisher_get_network_flow_endpoints(
  const rmw_publisher_t *,
  rcutils_allocator_t *,
  rmw_network_flow_endpoint_array_t *)
{
  return unsupported(
    "rmw_publisher_get_network_flow_endpoints",
    "network flow endpoints do not apply to an intra-process RMW");
}

rmw_ret_t
rmw_subscription_get_network_flow_endpoints(
  const rmw_subscription_t *,
  rcutils_allocator_t *,
  rmw_network_flow_endpoint_array_t *)
{
  return unsupported(
    "rmw_subscription_get_network_flow_endpoints",
    "network flow endpoints do not apply to an intra-process RMW");
}

rmw_ret_t
rmw_take_dynamic_message(
  const rmw_subscription_t *,
  rosidl_dynamic_typesupport_dynamic_data_t *,
  bool *,
  rmw_subscription_allocation_t *)
{
  return unsupported("rmw_take_dynamic_message", "dynamic message type support is not supported");
}

rmw_ret_t
rmw_take_dynamic_message_with_info(
  const rmw_subscription_t *,
  rosidl_dynamic_typesupport_dynamic_data_t *,
  bool *,
  rmw_message_info_t *,
  rmw_subscription_allocation_t *)
{
  return unsupported(
    "rmw_take_dynamic_message_with_info",
    "dynamic message type support is not supported");
}

rmw_ret_t
rmw_serialization_support_init(
  const char *,
  rcutils_allocator_t *,
  rosidl_dynamic_typesupport_serialization_support_t *)
{
  return unsupported(
    "rmw_serialization_support_init",
    "dynamic message type support is not supported");
}
}  // extern "C"
