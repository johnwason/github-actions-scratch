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

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

#include "context.hpp"
#include "identifier.hpp"
#include "node.hpp"
#include "registry.hpp"

using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::NodeImpl;
using rmw_intraprocess_cpp::Registry;

extern "C"
{
rmw_node_t *
rmw_create_node(rmw_context_t * context, const char * name, const char * namespace_)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(context, nullptr);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->implementation_identifier, "expected initialized context", return nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    context, context->implementation_identifier, kIdentifier, return nullptr);
  RMW_CHECK_FOR_NULL_WITH_MSG(context->impl, "expected initialized context", return nullptr);
  if (context->impl->is_shutdown.load()) {
    RMW_SET_ERROR_MSG("context has been shut down");
    return nullptr;
  }
  RMW_CHECK_ARGUMENT_FOR_NULL(name, nullptr);
  RMW_CHECK_ARGUMENT_FOR_NULL(namespace_, nullptr);

  int validation_result = RMW_NODE_NAME_VALID;
  if (rmw_validate_node_name(name, &validation_result, nullptr) != RMW_RET_OK) {
    return nullptr;
  }
  if (validation_result != RMW_NODE_NAME_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "invalid node name: %s", rmw_node_name_validation_result_string(validation_result));
    return nullptr;
  }
  validation_result = RMW_NAMESPACE_VALID;
  if (rmw_validate_namespace(namespace_, &validation_result, nullptr) != RMW_RET_OK) {
    return nullptr;
  }
  if (validation_result != RMW_NAMESPACE_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "invalid node namespace: %s", rmw_namespace_validation_result_string(validation_result));
    return nullptr;
  }

  std::unique_ptr<NodeImpl> impl;
  std::unique_ptr<rmw_node_t> node;
  try {
    impl = std::make_unique<NodeImpl>(context, name, namespace_);
    if (context->options.enclave != nullptr) {
      impl->enclave = context->options.enclave;
    }
    node = std::make_unique<rmw_node_t>();
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to allocate node");
    return nullptr;
  }
  impl->graph_guard_condition_handle.implementation_identifier = kIdentifier;
  impl->graph_guard_condition_handle.data = &impl->graph_guard_condition;
  impl->graph_guard_condition_handle.context = context;

  node->implementation_identifier = kIdentifier;
  node->data = impl.get();
  node->name = impl->name.c_str();
  node->namespace_ = impl->namespace_.c_str();
  node->context = context;

  try {
    Registry::instance().add_node(impl.get());
  } catch (const std::bad_alloc &) {
    RMW_SET_ERROR_MSG("failed to register node");
    return nullptr;
  }
  context->impl->node_count.fetch_add(1);
  impl.release();
  return node.release();
}

rmw_ret_t
rmw_destroy_node(rmw_node_t * node)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  auto impl = static_cast<NodeImpl *>(node->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "node impl is null", return RMW_RET_ERROR);
  Registry::instance().remove_node(impl);
  if (impl->context != nullptr && impl->context->impl != nullptr) {
    impl->context->impl->node_count.fetch_sub(1);
  }
  delete impl;
  delete node;
  return RMW_RET_OK;
}

const rmw_guard_condition_t *
rmw_node_get_graph_guard_condition(const rmw_node_t * node)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(node, nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    node, node->implementation_identifier, kIdentifier, return nullptr);
  auto impl = static_cast<NodeImpl *>(node->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(impl, "node impl is null", return nullptr);
  return &impl->graph_guard_condition_handle;
}
}  // extern "C"
