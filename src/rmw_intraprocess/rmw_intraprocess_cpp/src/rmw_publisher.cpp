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

#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"

#include "endpoint.hpp"
#include "identifier.hpp"
#include "node.hpp"
#include "qos.hpp"
#include "registry.hpp"
#include "tap.hpp"

using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::NodeImpl;
using rmw_intraprocess_cpp::PublisherImpl;
using rmw_intraprocess_cpp::Registry;
using rmw_intraprocess_cpp::Sample;
using rmw_intraprocess_cpp::SubscriptionImpl;

namespace
{

#define CHECK_PUBLISHER(publisher) \
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher, RMW_RET_INVALID_ARGUMENT); \
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH( \
    publisher, publisher->implementation_identifier, kIdentifier, \
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION)

/// Calls the extension API's taps (M9) for one published sample.
void tap(const PublisherImpl & impl, const Sample & sample, const void * ros_message)
{
  rmw_intraprocess_tap_sample_t t{};
  t.topic_name = impl.topic_name.c_str();
  t.type_name = impl.type.type_name.c_str();
  t.ros_message = ros_message;
  t.introspection_members = impl.type.introspection_members;
  if (impl.type.introspection_members != nullptr) {
    t.introspection_identifier = impl.type.introspection_is_cpp ?
      rosidl_typesupport_introspection_cpp::typesupport_identifier :
      rosidl_typesupport_introspection_c__identifier;
  }
  t.cdr = sample.cdr->data();
  t.cdr_length = sample.cdr->size();
  t.publisher_gid = sample.publisher_gid;
  t.source_timestamp = sample.source_timestamp;
  t.publication_sequence_number = sample.publication_sequence_number;
  rmw_intraprocess_cpp::TapRegistry::instance().invoke(t);
}

/// Delivers one CDR buffer to every target subscription, then to the taps.
/// ros_message: the native message, or null for a serialized publish.
rmw_ret_t publish_cdr(
  PublisherImpl & impl, std::shared_ptr<const std::vector<std::uint8_t>> cdr,
  const void * ros_message)
{
  Sample sample;
  sample.cdr = std::move(cdr);
  sample.publisher_gid = impl.gid;
  sample.source_timestamp = rmw_intraprocess_cpp::now_ns();
  sample.publication_sequence_number = ++impl.sequence_number;
  if (impl.lifespan_ns > 0) {
    sample.expiry = sample.source_timestamp + impl.lifespan_ns;
  }
  std::vector<std::shared_ptr<SubscriptionImpl>> targets;
  Registry::instance().prepare_publish(impl, sample, targets);
  for (const auto & sub : targets) {
    sub->deliver(sample);
  }
  targets.clear();
  if (!rmw_intraprocess_cpp::TapRegistry::instance().empty()) {
    tap(impl, sample, ros_message);
  }
  return RMW_RET_OK;
}

}  // namespace

extern "C"
{
rmw_publisher_t *
rmw_create_publisher(
  const rmw_node_t * node,
  const rosidl_message_type_support_t * type_support,
  const char * topic_name,
  const rmw_qos_profile_t * qos_profile,
  const rmw_publisher_options_t * publisher_options)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier, return nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos_profile, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher_options, nullptr);
  if (!rmw_intraprocess_cpp::validate_topic_name(
      topic_name, qos_profile->avoid_ros_namespace_conventions))
  {
    return nullptr;
  }
  if (!rmw_intraprocess_cpp::validate_qos(*qos_profile)) {
    return nullptr;
  }

  std::unique_ptr<PublisherImpl> impl;
  std::unique_ptr<rmw_publisher_t> handle;
  try {
    impl = std::make_unique<PublisherImpl>();
    handle = std::make_unique<rmw_publisher_t>();
    if (!rmw_intraprocess_cpp::resolve_message_type_support(type_support, impl->type)) {
      return nullptr;
    }
    impl->node = static_cast<NodeImpl *>(node->data);
    impl->context = node->context;
    impl->topic_name = topic_name;
    rmw_qos_profile_t requested_qos = *qos_profile;
    if (rmw_intraprocess_cpp::has_best_available_policy(requested_qos)) {
      // BEST_AVAILABLE: meet the existing subscriptions.
      std::vector<rmw_qos_profile_t> others;
      for (const auto & e : Registry::instance().graph_topic_endpoints(false, topic_name)) {
        others.push_back(e.qos);
      }
      rmw_intraprocess_cpp::adapt_best_available_qos(
        others, requested_qos, rmw_intraprocess_cpp::EndpointKind::kPublisher);
    }
    impl->qos = rmw_intraprocess_cpp::resolve_qos(
      requested_qos, rmw_intraprocess_cpp::EndpointKind::kPublisher);
    impl->options = *publisher_options;
    impl->lifespan_ns = rmw_intraprocess_cpp::finite_duration_ns(impl->qos.lifespan);
    impl->create_events(true);
    impl->gid = Registry::instance().new_gid();
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate publisher");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->data = impl.get();
  handle->topic_name = impl->topic_name.c_str();
  handle->options = *publisher_options;
  rmw_intraprocess_cpp::warn_unenforced_qos(impl->qos, topic_name);
  handle->can_loan_messages = false;

  try {
    Registry::instance().add_publisher(impl.get());
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to register publisher");
    return nullptr;
  }
  impl.release();
  return handle.release();
}

rmw_ret_t
rmw_destroy_publisher(rmw_node_t * node, rmw_publisher_t * publisher)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  CHECK_PUBLISHER(publisher);
  auto impl = static_cast<PublisherImpl *>(publisher->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "publisher impl is null", return RMW_RET_ERROR);
  impl->destroy_events();
  Registry::instance().remove_publisher(impl);
  delete impl;
  delete publisher;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_publish(
  const rmw_publisher_t * publisher, const void * ros_message, rmw_publisher_allocation_t *)
{
  CHECK_PUBLISHER(publisher);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_message, RMW_RET_INVALID_ARGUMENT);
  auto impl = static_cast<PublisherImpl *>(publisher->data);
  try {
    auto cdr = std::make_shared<std::vector<std::uint8_t>>();
    if (!rmw_intraprocess_cpp::serialize_message(impl->type, ros_message, *cdr)) {
      return RMW_RET_ERROR;
    }
    return publish_cdr(*impl, std::move(cdr), ros_message);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while publishing");
    return RMW_RET_BAD_ALLOC;
  }
}

rmw_ret_t
rmw_publish_serialized_message(
  const rmw_publisher_t * publisher,
  const rmw_serialized_message_t * serialized_message,
  rmw_publisher_allocation_t *)
{
  CHECK_PUBLISHER(publisher);
  RMW_CHECK_ARGUMENT_FOR_NULL(serialized_message, RMW_RET_INVALID_ARGUMENT);
  if (serialized_message->buffer == nullptr && serialized_message->buffer_length != 0) {
    RMW_SET_ERROR_MSG("serialized message buffer is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto impl = static_cast<PublisherImpl *>(publisher->data);
  try {
    auto cdr = std::make_shared<std::vector<std::uint8_t>>(
      serialized_message->buffer, serialized_message->buffer + serialized_message->buffer_length);
    return publish_cdr(*impl, std::move(cdr), nullptr);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while publishing");
    return RMW_RET_BAD_ALLOC;
  }
}

rmw_ret_t
rmw_publisher_count_matched_subscriptions(
  const rmw_publisher_t * publisher, size_t * subscription_count)
{
  CHECK_PUBLISHER(publisher);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription_count, RMW_RET_INVALID_ARGUMENT);
  *subscription_count = Registry::instance().count_matched_subscriptions(
    *static_cast<const PublisherImpl *>(publisher->data));
  return RMW_RET_OK;
}

rmw_ret_t
rmw_publisher_wait_for_all_acked(const rmw_publisher_t * publisher, rmw_time_t wait_timeout)
{
  (void)wait_timeout;
  CHECK_PUBLISHER(publisher);
  // Delivery is synchronous inside rmw_publish: nothing is ever outstanding.
  return RMW_RET_OK;
}

rmw_ret_t
rmw_publisher_get_actual_qos(const rmw_publisher_t * publisher, rmw_qos_profile_t * qos)
{
  CHECK_PUBLISHER(publisher);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = static_cast<const PublisherImpl *>(publisher->data)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_get_gid_for_publisher(const rmw_publisher_t * publisher, rmw_gid_t * gid)
{
  CHECK_PUBLISHER(publisher);
  RMW_CHECK_ARGUMENT_FOR_NULL(gid, RMW_RET_INVALID_ARGUMENT);
  *gid = static_cast<const PublisherImpl *>(publisher->data)->gid;
  return RMW_RET_OK;
}
}  // extern "C"
