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

// Graph API (M5): node, topic and service names and types, counts, and
// endpoint info. Every context in the process shares one graph (single domain).
// Names are stored as ROS names, so `no_demangle` has no effect.

#include <cstddef>
#include <string>
#include <vector>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rcutils/types/string_array.h"
#include "rmw/error_handling.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/rmw.h"
#include "rmw/sanity_checks.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

#include "identifier.hpp"
#include "registry.hpp"

using rmw_intraprocess_cpp::Registry;
using Kind = rmw_intraprocess_cpp::Registry::EndpointKind;

namespace
{

rmw_ret_t check_node(const rmw_node_t * node)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, rmw_intraprocess_cpp::kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  return RMW_RET_OK;
}

rmw_ret_t check_allocator(const rcutils_allocator_t * allocator)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(allocator, RMW_RET_INVALID_ARGUMENT);
  RCUTILS_CHECK_ALLOCATOR_WITH_MSG(
    allocator, "allocator is invalid", return RMW_RET_INVALID_ARGUMENT);
  return RMW_RET_OK;
}

rmw_ret_t check_names_and_types_out(rmw_names_and_types_t * out)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(out, RMW_RET_INVALID_ARGUMENT);
  if (rmw_names_and_types_check_zero(out) != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

rmw_ret_t check_string_array_out(rcutils_string_array_t * out, const char * what)
{
  if (out == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s is null", what);
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (rmw_check_zero_rmw_string_array(out) != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

/// Validates a fully qualified topic or service name.
rmw_ret_t check_full_name(const char * name, const char * what)
{
  if (name == nullptr) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("%s is null", what);
    return RMW_RET_INVALID_ARGUMENT;
  }
  int result = RMW_TOPIC_VALID;
  if (rmw_validate_full_topic_name(name, &result, nullptr) != RMW_RET_OK) {
    return RMW_RET_ERROR;
  }
  if (result != RMW_TOPIC_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "%s is invalid: %s", what, rmw_full_topic_name_validation_result_string(result));
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

rmw_ret_t check_node_name_and_namespace(const char * node_name, const char * node_namespace)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node_name, RMW_RET_INVALID_ARGUMENT);
  int result = RMW_NODE_NAME_VALID;
  if (rmw_validate_node_name(node_name, &result, nullptr) != RMW_RET_OK) {
    return RMW_RET_ERROR;
  }
  if (result != RMW_NODE_NAME_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "node name is invalid: %s", rmw_node_name_validation_result_string(result));
    return RMW_RET_INVALID_ARGUMENT;
  }
  RMW_CHECK_ARGUMENT_FOR_NULL(node_namespace, RMW_RET_INVALID_ARGUMENT);
  result = RMW_NAMESPACE_VALID;
  if (rmw_validate_namespace(node_namespace, &result, nullptr) != RMW_RET_OK) {
    return RMW_RET_ERROR;
  }
  if (result != RMW_NAMESPACE_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "node namespace is invalid: %s", rmw_namespace_validation_result_string(result));
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

/// Copies `in` into a zero-initialized `out`. On failure `out` stays zero.
rmw_ret_t copy_names_and_types(
  const Registry::NamesAndTypes & in, rcutils_allocator_t * allocator,
  rmw_names_and_types_t * out)
{
  if (in.empty()) {
    return RMW_RET_OK;
  }
  rmw_ret_t ret = rmw_names_and_types_init(out, in.size(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  std::size_t i = 0;
  for (const auto & entry : in) {
    out->names.data[i] = rcutils_strdup(entry.first.c_str(), *allocator);
    bool ok = out->names.data[i] != nullptr &&
      rcutils_string_array_init(&out->types[i], entry.second.size(), allocator) ==
      RCUTILS_RET_OK;
    std::size_t j = 0;
    for (const std::string & type : entry.second) {
      if (!ok) {
        break;
      }
      out->types[i].data[j] = rcutils_strdup(type.c_str(), *allocator);
      ok = out->types[i].data[j] != nullptr;
      ++j;
    }
    if (!ok) {
      rmw_reset_error();
      const auto cleanup_ret = rmw_names_and_types_fini(out);
      (void)cleanup_ret;  // best-effort cleanup on the error path
      RMW_SET_ERROR_MSG("failed to allocate names and types");
      return RMW_RET_BAD_ALLOC;
    }
    ++i;
  }
  return RMW_RET_OK;
}

rmw_ret_t names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, Kind kind, rmw_names_and_types_t * out)
{
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_allocator(allocator);
  }
  if (ret == RMW_RET_OK) {
    ret = check_node_name_and_namespace(node_name, node_namespace);
  }
  if (ret == RMW_RET_OK) {
    ret = check_names_and_types_out(out);
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  Registry::NamesAndTypes result;
  if (!Registry::instance().graph_names_and_types({kind}, node_name, node_namespace, result)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "node '%s' in namespace '%s' does not exist", node_name, node_namespace);
    return RMW_RET_NODE_NAME_NON_EXISTENT;
  }
  return copy_names_and_types(result, allocator, out);
}

rmw_ret_t get_node_names_impl(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  std::vector<Registry::GraphNode> nodes = Registry::instance().graph_nodes();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rcutils_string_array_t * arrays[3] = {node_names, node_namespaces, enclaves};
  bool ok = true;
  for (rcutils_string_array_t * a : arrays) {
    if (a != nullptr && ok) {
      ok = rcutils_string_array_init(a, nodes.size(), &allocator) == RCUTILS_RET_OK;
    }
  }
  for (std::size_t i = 0; ok && i < nodes.size(); ++i) {
    const std::string * values[3] = {&nodes[i].name, &nodes[i].namespace_, &nodes[i].enclave};
    for (int k = 0; ok && k < 3; ++k) {
      if (arrays[k] != nullptr) {
        arrays[k]->data[i] = rcutils_strdup(values[k]->c_str(), allocator);
        ok = arrays[k]->data[i] != nullptr;
      }
    }
  }
  if (!ok) {
    rmw_reset_error();
    for (rcutils_string_array_t * a : arrays) {
      if (a != nullptr) {
        const auto cleanup_ret = rcutils_string_array_fini(a);
        (void)cleanup_ret;  // best-effort cleanup on the error path
      }
    }
    rcutils_reset_error();
    RMW_SET_ERROR_MSG("failed to allocate node names");
    return RMW_RET_BAD_ALLOC;
  }
  (void)node;
  return RMW_RET_OK;
}

rmw_ret_t endpoint_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name,
  bool publishers, rmw_topic_endpoint_info_array_t * out)
{
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_allocator(allocator);
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  RMW_CHECK_ARGUMENT_FOR_NULL(topic_name, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(out, RMW_RET_INVALID_ARGUMENT);
  if (rmw_topic_endpoint_info_array_check_zero(out) != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::vector<Registry::GraphTopicEndpoint> endpoints =
    Registry::instance().graph_topic_endpoints(publishers, topic_name);
  if (endpoints.empty()) {
    return RMW_RET_OK;
  }
  ret = rmw_topic_endpoint_info_array_init_with_size(out, endpoints.size(), allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  for (std::size_t i = 0; i < endpoints.size() && ret == RMW_RET_OK; ++i) {
    const Registry::GraphTopicEndpoint & e = endpoints[i];
    rmw_topic_endpoint_info_t & info = out->info_array[i];
    ret = rmw_topic_endpoint_info_set_node_name(&info, e.node_name.c_str(), allocator);
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_node_namespace(
        &info, e.node_namespace.c_str(), allocator);
    }
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_topic_type(&info, e.type_name.c_str(), allocator);
    }
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_topic_type_hash(&info, &e.type_hash);
    }
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_endpoint_type(
        &info, publishers ? RMW_ENDPOINT_PUBLISHER : RMW_ENDPOINT_SUBSCRIPTION);
    }
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_gid(&info, e.gid.data, RMW_GID_STORAGE_SIZE);
    }
    if (ret == RMW_RET_OK) {
      ret = rmw_topic_endpoint_info_set_qos_profile(&info, &e.qos);
    }
  }
  if (ret != RMW_RET_OK) {
    rmw_error_string_t error = rmw_get_error_string();
    rmw_reset_error();
    const auto cleanup_ret = rmw_topic_endpoint_info_array_fini(out, allocator);
    (void)cleanup_ret;  // best-effort cleanup on the error path
    rmw_reset_error();
    RMW_SET_ERROR_MSG(error.str);
  }
  return ret;
}

rmw_ret_t count_impl(const rmw_node_t * node, const char * name, Kind kind, size_t * count)
{
  rmw_ret_t ret = check_node(node);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  bool topic = kind == Kind::kPublisher || kind == Kind::kSubscription;
  ret = check_full_name(name, topic ? "topic name" : "service name");
  if (ret != RMW_RET_OK) {
    return ret;
  }
  RMW_CHECK_ARGUMENT_FOR_NULL(count, RMW_RET_INVALID_ARGUMENT);
  *count = Registry::instance().count_endpoints(kind, name);
  return RMW_RET_OK;
}

}  // namespace

extern "C"
{
rmw_ret_t
rmw_get_node_names(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces)
{
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_string_array_out(node_names, "node_names");
  }
  if (ret == RMW_RET_OK) {
    ret = check_string_array_out(node_namespaces, "node_namespaces");
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return get_node_names_impl(node, node_names, node_namespaces, nullptr);
}

rmw_ret_t
rmw_get_node_names_with_enclaves(
  const rmw_node_t * node, rcutils_string_array_t * node_names,
  rcutils_string_array_t * node_namespaces, rcutils_string_array_t * enclaves)
{
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_string_array_out(node_names, "node_names");
  }
  if (ret == RMW_RET_OK) {
    ret = check_string_array_out(node_namespaces, "node_namespaces");
  }
  if (ret == RMW_RET_OK) {
    ret = check_string_array_out(enclaves, "enclaves");
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  return get_node_names_impl(node, node_names, node_namespaces, enclaves);
}

rmw_ret_t
rmw_get_topic_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator, bool no_demangle,
  rmw_names_and_types_t * topic_names_and_types)
{
  (void)no_demangle;
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_allocator(allocator);
  }
  if (ret == RMW_RET_OK) {
    ret = check_names_and_types_out(topic_names_and_types);
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  Registry::NamesAndTypes result;
  (void)Registry::instance().graph_names_and_types(
    {Kind::kPublisher, Kind::kSubscription}, nullptr, nullptr, result);
  return copy_names_and_types(result, allocator, topic_names_and_types);
}

rmw_ret_t
rmw_get_service_names_and_types(
  const rmw_node_t * node, rcutils_allocator_t * allocator,
  rmw_names_and_types_t * service_names_and_types)
{
  rmw_ret_t ret = check_node(node);
  if (ret == RMW_RET_OK) {
    ret = check_allocator(allocator);
  }
  if (ret == RMW_RET_OK) {
    ret = check_names_and_types_out(service_names_and_types);
  }
  if (ret != RMW_RET_OK) {
    return ret;
  }
  // Like the DDS RMWs, a service name is in the graph if a server or a client
  // uses it.
  Registry::NamesAndTypes result;
  (void)Registry::instance().graph_names_and_types(
    {Kind::kService, Kind::kClient}, nullptr, nullptr, result);
  return copy_names_and_types(result, allocator, service_names_and_types);
}

rmw_ret_t
rmw_get_publisher_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  (void)no_demangle;
  return names_and_types_by_node(
    node, allocator, node_name, node_namespace, Kind::kPublisher, topic_names_and_types);
}

rmw_ret_t
rmw_get_subscriber_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, bool no_demangle, rmw_names_and_types_t * topic_names_and_types)
{
  (void)no_demangle;
  return names_and_types_by_node(
    node, allocator, node_name, node_namespace, Kind::kSubscription, topic_names_and_types);
}

rmw_ret_t
rmw_get_service_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return names_and_types_by_node(
    node, allocator, node_name, node_namespace, Kind::kService, service_names_and_types);
}

rmw_ret_t
rmw_get_client_names_and_types_by_node(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * node_name,
  const char * node_namespace, rmw_names_and_types_t * service_names_and_types)
{
  return names_and_types_by_node(
    node, allocator, node_name, node_namespace, Kind::kClient, service_names_and_types);
}

rmw_ret_t
rmw_get_publishers_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name,
  bool no_mangle, rmw_topic_endpoint_info_array_t * publishers_info)
{
  (void)no_mangle;
  return endpoint_info_by_topic(node, allocator, topic_name, true, publishers_info);
}

rmw_ret_t
rmw_get_subscriptions_info_by_topic(
  const rmw_node_t * node, rcutils_allocator_t * allocator, const char * topic_name,
  bool no_mangle, rmw_topic_endpoint_info_array_t * subscriptions_info)
{
  (void)no_mangle;
  return endpoint_info_by_topic(node, allocator, topic_name, false, subscriptions_info);
}

rmw_ret_t
rmw_count_publishers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return count_impl(node, topic_name, Kind::kPublisher, count);
}

rmw_ret_t
rmw_count_subscribers(const rmw_node_t * node, const char * topic_name, size_t * count)
{
  return count_impl(node, topic_name, Kind::kSubscription, count);
}

rmw_ret_t
rmw_count_clients(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return count_impl(node, service_name, Kind::kClient, count);
}

rmw_ret_t
rmw_count_services(const rmw_node_t * node, const char * service_name, size_t * count)
{
  return count_impl(node, service_name, Kind::kService, count);
}
}  // extern "C"
