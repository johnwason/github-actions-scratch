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
#include <memory>
#include <new>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"

#include "endpoint.hpp"
#include "identifier.hpp"
#include "node.hpp"
#include "qos.hpp"
#include "registry.hpp"

using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::NodeImpl;
using rmw_intraprocess_cpp::Registry;
using rmw_intraprocess_cpp::Sample;
using rmw_intraprocess_cpp::SubscriptionImpl;

namespace
{

#define CHECK_SUBSCRIPTION(subscription) \
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription, RMW_RET_INVALID_ARGUMENT); \
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH( \
    subscription, subscription->implementation_identifier, kIdentifier, \
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION)

SubscriptionImpl * impl_of(const rmw_subscription_t * subscription)
{
  return static_cast<SubscriptionImpl *>(subscription->data);
}

void fill_message_info(const Sample & s, rmw_message_info_t * info)
{
  info->source_timestamp = s.source_timestamp;
  info->received_timestamp = s.received_timestamp;
  info->publication_sequence_number = s.publication_sequence_number;
  info->reception_sequence_number = s.reception_sequence_number;
  info->publisher_gid = s.publisher_gid;
  info->from_intra_process = false;
}

/// Takes one sample and deserializes it into ros_message.
rmw_ret_t take_one(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_message_info_t * message_info)
{
  SubscriptionImpl * impl = impl_of(subscription);
  Sample s;
  *taken = false;
  if (!impl->take(s)) {
    return RMW_RET_OK;
  }
  if (!rmw_intraprocess_cpp::deserialize_message(
      impl->type, s.cdr->data(), s.cdr->size(), ros_message))
  {
    return RMW_RET_ERROR;
  }
  if (message_info != nullptr) {
    fill_message_info(s, message_info);
  }
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t take_serialized_one(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info)
{
  SubscriptionImpl * impl = impl_of(subscription);
  Sample s;
  *taken = false;
  if (!impl->take(s)) {
    return RMW_RET_OK;
  }
  const std::size_t size = s.cdr->size();
  if (serialized_message->buffer_capacity < size) {
    rmw_ret_t ret = rmw_serialized_message_resize(serialized_message, size);
    if (ret != RMW_RET_OK) {
      return ret;
    }
  }
  if (size > 0) {
    std::memcpy(serialized_message->buffer, s.cdr->data(), size);
  }
  serialized_message->buffer_length = size;
  if (message_info != nullptr) {
    fill_message_info(s, message_info);
  }
  *taken = true;
  return RMW_RET_OK;
}

}  // namespace

extern "C"
{
rmw_subscription_t *
rmw_create_subscription(
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support,
  const char * topic_name,
  const rmw_qos_profile_t * qos_policies,
  const rmw_subscription_options_t * subscription_options)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier, return nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos_policies, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription_options, nullptr);
  if (!rmw_intraprocess_cpp::validate_topic_name(
      topic_name, qos_policies->avoid_ros_namespace_conventions))
  {
    return nullptr;
  }
  if (!rmw_intraprocess_cpp::validate_qos(*qos_policies)) {
    return nullptr;
  }

  std::shared_ptr<SubscriptionImpl> impl;
  std::unique_ptr<rmw_subscription_t> handle;
  try {
    impl = std::make_shared<SubscriptionImpl>();
    handle = std::make_unique<rmw_subscription_t>();
    if (!rmw_intraprocess_cpp::resolve_message_type_support(type_support, impl->type)) {
      return nullptr;
    }
    impl->node = static_cast<NodeImpl *>(node->data);
    impl->context = node->context;
    impl->topic_name = topic_name;
    rmw_qos_profile_t requested_qos = *qos_policies;
    if (rmw_intraprocess_cpp::has_best_available_policy(requested_qos)) {
      // BEST_AVAILABLE: match the existing publishers.
      std::vector<rmw_qos_profile_t> others;
      for (const auto & e : Registry::instance().graph_topic_endpoints(true, topic_name)) {
        others.push_back(e.qos);
      }
      rmw_intraprocess_cpp::adapt_best_available_qos(
        others, requested_qos, rmw_intraprocess_cpp::EndpointKind::kSubscription);
    }
    impl->qos = rmw_intraprocess_cpp::resolve_qos(
      requested_qos, rmw_intraprocess_cpp::EndpointKind::kSubscription);
    impl->options = *subscription_options;
    // Content filtering is not supported; never keep the caller's pointer.
    impl->options.content_filter_options = nullptr;
    impl->create_events(false);
    impl->gid = Registry::instance().new_gid();
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate subscription");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->data = impl.get();
  handle->topic_name = impl->topic_name.c_str();
  handle->options = impl->options;
  handle->can_loan_messages = false;
  handle->is_cft_enabled = false;
  rmw_intraprocess_cpp::warn_unenforced_qos(impl->qos, topic_name);

  try {
    Registry::instance().add_subscription(impl);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to register subscription");
    return nullptr;
  }
  return handle.release();
}

rmw_ret_t
rmw_destroy_subscription(rmw_node_t * node, rmw_subscription_t * subscription)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  CHECK_SUBSCRIPTION(subscription);
  SubscriptionImpl * impl = impl_of(subscription);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "subscription impl is null", return RMW_RET_ERROR);
  impl->mark_destroyed();
  // Drops the owning reference (impl may live on briefly in a concurrent publish).
  Registry::instance().remove_subscription(impl);
  delete subscription;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_subscription_count_matched_publishers(
  const rmw_subscription_t * subscription, size_t * publisher_count)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher_count, RMW_RET_INVALID_ARGUMENT);
  *publisher_count = Registry::instance().count_matched_publishers(*impl_of(subscription));
  return RMW_RET_OK;
}

rmw_ret_t
rmw_subscription_get_actual_qos(const rmw_subscription_t * subscription, rmw_qos_profile_t * qos)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = impl_of(subscription)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_take(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_subscription_allocation_t *)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  return take_one(subscription, ros_message, taken, nullptr);
}

rmw_ret_t
rmw_take_with_info(
  const rmw_subscription_t * subscription, void * ros_message, bool * taken,
  rmw_message_info_t * message_info, rmw_subscription_allocation_t *)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(message_info, RMW_RET_INVALID_ARGUMENT);
  return take_one(subscription, ros_message, taken, message_info);
}

rmw_ret_t
rmw_take_sequence(
  const rmw_subscription_t * subscription, size_t count,
  rmw_message_sequence_t * message_sequence,
  rmw_message_info_sequence_t * message_info_sequence, size_t * taken,
  rmw_subscription_allocation_t *)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(message_sequence, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(message_info_sequence, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  if (count == 0) {
    RMW_SET_ERROR_MSG("count cannot be 0");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (count > message_sequence->capacity) {
    RMW_SET_ERROR_MSG("insufficient capacity in message_sequence");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (count > message_info_sequence->capacity) {
    RMW_SET_ERROR_MSG("insufficient capacity in message_info_sequence");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *taken = 0;
  rmw_ret_t ret = RMW_RET_OK;
  while (*taken < count) {
    bool one = false;
    ret = take_one(
      subscription, message_sequence->data[*taken], &one,
      &message_info_sequence->data[*taken]);
    if (ret != RMW_RET_OK || !one) {
      break;
    }
    ++*taken;
  }
  message_sequence->size = *taken;
  message_info_sequence->size = *taken;
  return ret;
}

rmw_ret_t
rmw_take_serialized_message(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_subscription_allocation_t *)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(serialized_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  return take_serialized_one(subscription, serialized_message, taken, nullptr);
}

rmw_ret_t
rmw_take_serialized_message_with_info(
  const rmw_subscription_t * subscription, rmw_serialized_message_t * serialized_message,
  bool * taken, rmw_message_info_t * message_info, rmw_subscription_allocation_t *)
{
  CHECK_SUBSCRIPTION(subscription);
  RMW_CHECK_ARGUMENT_FOR_NULL(serialized_message, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(message_info, RMW_RET_INVALID_ARGUMENT);
  return take_serialized_one(subscription, serialized_message, taken, message_info);
}

rmw_ret_t
rmw_subscription_set_on_new_message_callback(
  rmw_subscription_t * subscription, rmw_event_callback_t callback, const void * user_data)
{
  CHECK_SUBSCRIPTION(subscription);
  impl_of(subscription)->set_on_new_message_callback(callback, user_data);
  return RMW_RET_OK;
}
}  // extern "C"
