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

#define CHECK_SERVICE(service) \
  RMW_CHECK_ARGUMENT_FOR_NULL(service, RMW_RET_INVALID_ARGUMENT); \
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH( \
    service, service->implementation_identifier, kIdentifier, \
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION)

ServiceImpl * impl_of(const rmw_service_t * service)
{
  return static_cast<ServiceImpl *>(service->data);
}

}  // namespace

extern "C"
{
rmw_service_t *
rmw_create_service(
  const rmw_node_t * node,
  const rosidl_service_type_support_t * type_support,
  const char * service_name,
  const rmw_qos_profile_t * qos_profile)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier, return nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(type_support, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos_profile, nullptr);
  if (!rmw_intraprocess_cpp::validate_topic_name(
      service_name, qos_profile->avoid_ros_namespace_conventions))
  {
    return nullptr;
  }
  if (!rmw_intraprocess_cpp::validate_qos(*qos_profile)) {
    return nullptr;
  }
  std::shared_ptr<ServiceImpl> impl;
  std::unique_ptr<rmw_service_t> handle;
  try {
    impl = std::make_shared<ServiceImpl>();
    handle = std::make_unique<rmw_service_t>();
    if (!rmw_intraprocess_cpp::resolve_service_type_support(type_support, impl->type)) {
      return nullptr;
    }
    impl->node = static_cast<NodeImpl *>(node->data);
    impl->context = node->context;
    impl->service_name = service_name;
    impl->qos = rmw_intraprocess_cpp::resolve_qos(
      *qos_profile, rmw_intraprocess_cpp::EndpointKind::kSubscription);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate service");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->data = impl.get();
  handle->service_name = impl->service_name.c_str();
  try {
    Registry::instance().add_service(impl);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to register service");
    return nullptr;
  }
  return handle.release();
}

rmw_ret_t
rmw_destroy_service(rmw_node_t * node, rmw_service_t * service)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  CHECK_SERVICE(service);
  ServiceImpl * impl = impl_of(service);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "service impl is null", return RMW_RET_ERROR);
  impl->mark_destroyed();
  Registry::instance().remove_service(impl);
  delete service;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_take_request(
  const rmw_service_t * service, rmw_service_info_t * request_header, void * ros_request,
  bool * taken)
{
  CHECK_SERVICE(service);
  RMW_CHECK_ARGUMENT_FOR_NULL(request_header, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_request, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  ServiceImpl * impl = impl_of(service);
  *taken = false;
  ServiceMessage m;
  if (!impl->take(m)) {
    return RMW_RET_OK;
  }
  if (!rmw_intraprocess_cpp::deserialize_message(
      impl->type.request, m.cdr->data(), m.cdr->size(), ros_request))
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
rmw_send_response(
  const rmw_service_t * service, rmw_request_id_t * request_header, void * ros_response)
{
  CHECK_SERVICE(service);
  RMW_CHECK_ARGUMENT_FOR_NULL(request_header, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(ros_response, RMW_RET_INVALID_ARGUMENT);
  ServiceImpl * impl = impl_of(service);
  try {
    std::shared_ptr<ClientImpl> client =
      Registry::instance().find_client(*impl, request_header->writer_guid);
    if (!client) {
      // The client is gone: drop the response (RMW_RET_OK, as other RMWs).
      return RMW_RET_OK;
    }
    auto cdr = std::make_shared<std::vector<std::uint8_t>>();
    if (!rmw_intraprocess_cpp::serialize_message(impl->type.response, ros_response, *cdr)) {
      return RMW_RET_ERROR;
    }
    ServiceMessage m;
    m.cdr = std::move(cdr);
    m.request_id = *request_header;
    m.source_timestamp = rmw_intraprocess_cpp::now_ns();
    client->deliver(std::move(m), client->qos);
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate while sending a response");
    return RMW_RET_BAD_ALLOC;
  }
  return RMW_RET_OK;
}

rmw_ret_t
rmw_service_response_publisher_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  CHECK_SERVICE(service);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = impl_of(service)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_service_request_subscription_get_actual_qos(
  const rmw_service_t * service, rmw_qos_profile_t * qos)
{
  CHECK_SERVICE(service);
  RMW_CHECK_ARGUMENT_FOR_NULL(qos, RMW_RET_INVALID_ARGUMENT);
  *qos = impl_of(service)->qos;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_service_set_on_new_request_callback(
  rmw_service_t * service, rmw_event_callback_t callback, const void * user_data)
{
  CHECK_SERVICE(service);
  impl_of(service)->set_callback(callback, user_data);
  return RMW_RET_OK;
}
}  // extern "C"
