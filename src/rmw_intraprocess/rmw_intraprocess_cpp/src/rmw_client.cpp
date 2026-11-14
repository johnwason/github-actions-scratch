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
#include <utility>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "endpoint.hpp"
#include "identifier.hpp"
#include "node.hpp"
#include "qos.hpp"
#include "registry.hpp"
#include "service.hpp"

using rmw_intraprocess_cpp::ClientImpl;
using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::NodeImpl;
using rmw_intraprocess_cpp::Registry;
using rmw_intraprocess_cpp::ServiceImpl;
using rmw_intraprocess_cpp::ServiceMessage;

namespace
{

#define CHECK_CLIENT(client) \
  RMW_CHECK_ARGUMENT_FOR_NULL(client, RMW_RET_INVALID_ARGUMENT); \
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH( \
    client, client->implementation_identifier, kIdentifier, \
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION)

ClientImpl * impl_of(const rmw_client_t * client)
{
  return static_cast<ClientImpl *>(client->data);
}

}  // namespace

extern "C"
{
rmw_client_t *
rmw_create_client(
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name,
  const rmw_qos_profile_t * qos_policies)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier, return nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos_policies, nullptr);
  if (!rmw_intraprocess_cpp::validate_topic_name(
      service_name, qos_policies->avoid_ros_namespace_conventions))
  {
    return nullptr;
  }
  if (!rmw_intraprocess_cpp::validate_qos(*qos_policies)) {
    return nullptr;
  }
  std::shared_ptr<ClientImpl> impl;
  std::unique_ptr<rmw_client_t> handle;
  try {
    impl = std::make_shared<ClientImpl>();
    handle = std::make_unique<rmw_client_t>();
    if (!rmw_intraprocess_cpp::resolve_service_type_support(type_support, impl->type)) {
      return nullptr;
    }
    impl->node = static_cast<NodeImpl *>(node->data);
    impl->context = node->context;
    impl->service_name = service_name;
    impl->qos = rmw_intraprocess_cpp::resolve_qos(
      *qos_policies, rmw_intraprocess_cpp::EndpointKind::kPublisher);
    impl->gid = Registry::instance().new_gid();
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate client");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->data = impl.get();
  handle->service_name = impl->service_name.c_str();
  try {
    Registry::instance().add_client(impl);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to register client");
    return nullptr;
  }
  return handle.release();
}

rmw_ret_t
rmw_destroy_client(rmw_node_t * node, rmw_client_t * client)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  CHECK_CLIENT(client);
  ClientImpl * impl = impl_of(client);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "client impl is null", return RMW_RET_ERROR);
  impl->mark_destroyed();
  Registry::instance().remove_client(impl);
  delete client;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_send_request(const rmw_client_t * client, const void * ros_request, int64_t * sequence_id)
{
  CHECK_CLIENT(client);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_request, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(sequence_id, RMW_RET_INVALID_ARGUMENT);
  ClientImpl * impl = impl_of(client);
  try {
    auto cdr = std::make_shared<std::vector<std::uint8_t>>();
    if (!rmw_intraprocess_cpp::serialize_message(impl->type.request, ros_request, *cdr)) {
      return RMW_RET_ERROR;
    }
    ServiceMessage m;
    m.cdr = std::move(cdr);
    std::memcpy(m.request_id.writer_guid, impl->gid.data, RMW_GID_STORAGE_SIZE);
    m.request_id.sequence_number = ++impl->sequence_number;
    m.source_timestamp = rmw_intraprocess_cpp::now_ns();
    *sequence_id = m.request_id.sequence_number;
    std::vector<std::shared_ptr<ServiceImpl>> targets;
    Registry::instance().request_targets(*impl, targets);
    for (const auto & s : targets) {
      s->deliver(m, s->qos);
    }
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while sending a request");
    return RMW_RET_BAD_ALLOC;
  }
  return RMW_RET_OK;
}

rmw_ret_t
rmw_take_response(
  const rmw_client_t * client, rmw_service_info_t * request_header, void * ros_response,
  bool * taken)
{
  CHECK_CLIENT(client);
  RMW_CHECK_ARGUMENT_FOR_NULL(request_header, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_response, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  ClientImpl * impl = impl_of(client);
  *taken = false;
  ServiceMessage m;
  if (!impl->take(m)) {
    return RMW_RET_OK;
  }
  if (!rmw_intraprocess_cpp::deserialize_message(
      impl->type.response, m.cdr->data(), m.cdr->size(), ros_response))
  {
    return RMW_RET_ERROR;
  }
  request_header->request_id = m.request_id;
  request_header->source_timestamp = m.source_timestamp;
  request_header->received_timestamp = m.received_timestamp;
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_client_request_publisher_get_actual_qos(const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  CHECK_CLIENT(client);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = impl_of(client)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_client_response_subscription_get_actual_qos(
  const rmw_client_t * client, rmw_qos_profile_t * qos)
{
  CHECK_CLIENT(client);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = impl_of(client)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_get_gid_for_client(const rmw_client_t * client, rmw_gid_t * gid)
{
  CHECK_CLIENT(client);
  RMW_CHECK_ARGUMENT_FOR_NULL(gid, RMW_RET_INVALID_ARGUMENT);
  *gid = impl_of(client)->gid;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_service_server_is_available(
  const rmw_node_t * node, const rmw_client_t * client, bool * is_available)
{
  // test_rmw_implementation expects RMW_RET_ERROR (not INVALID_ARGUMENT) for nulls here.
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_ERROR);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_ARGUMENT_FOR_NULL(client, RMW_RET_ERROR);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    client, client->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_ARGUMENT_FOR_NULL(is_available, RMW_RET_ERROR);
  *is_available = Registry::instance().service_available(*impl_of(client));
  return RMW_RET_OK;
}

rmw_ret_t
rmw_client_set_on_new_response_callback(
  rmw_client_t * client, rmw_event_callback_t callback, const void * user_data)
{
  CHECK_CLIENT(client);
  impl_of(client)->set_callback(callback, user_data);
  return RMW_RET_OK;
}
}  // extern "C"
