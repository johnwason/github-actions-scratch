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

#include <new>

#include "rcutils/strdup.h"
#include "rmw/discovery_options.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/security_options.h"

#include "context.hpp"
#include "identifier.hpp"

using rmw_intraprocess_cpp::kIdentifier;

extern "C"
{
rmw_ret_t
rmw_init_options_init(rmw_init_options_t * init_options, rcutils_allocator_t allocator)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(init_options, RMW_RET_INVALID_ARGUMENT);
  RCUTILS_CHECK_ALLOCATOR(&allocator, return RMW_RET_INVALID_ARGUMENT);
  if (init_options->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized init_options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_init_options_t tmp = rmw_get_zero_initialized_init_options();
  tmp.instance_id = 0;
  tmp.implementation_identifier = kIdentifier;
  tmp.allocator = allocator;
  tmp.impl = nullptr;
  tmp.enclave = nullptr;
  // Accepted and ignored: there is a single domain per process.
  tmp.domain_id = RMW_DEFAULT_DOMAIN_ID;
  tmp.security_options = rmw_get_default_security_options();
  // Accepted and ignored: all communication is in-process.
  tmp.localhost_only = RMW_LOCALHOST_ONLY_DEFAULT;
  tmp.discovery_options = rmw_get_zero_initialized_discovery_options();
  rmw_ret_t ret = rmw_discovery_options_init(&tmp.discovery_options, 0, &allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  *init_options = tmp;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_init_options_copy(const rmw_init_options_t * src, rmw_init_options_t * dst)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(src, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(dst, RMW_RET_INVALID_ARGUMENT);
  if (src->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized src");
    return RMW_RET_INVALID_ARGUMENT;
  }
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    src, src->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  if (dst->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized dst");
    return RMW_RET_INVALID_ARGUMENT;
  }
  // Local copy: the rmw *_copy helpers take a non-const allocator.
  rcutils_allocator_t allocator_copy = src->allocator;
  rcutils_allocator_t * allocator = &allocator_copy;
  RCUTILS_CHECK_ALLOCATOR(allocator, return RMW_RET_INVALID_ARGUMENT);

  rmw_init_options_t tmp = *src;
  tmp.enclave = nullptr;
  tmp.security_options = rmw_get_zero_initialized_security_options();
  tmp.discovery_options = rmw_get_zero_initialized_discovery_options();

  if (src->enclave != nullptr) {
    tmp.enclave = rcutils_strdup(src->enclave, *allocator);
    if (tmp.enclave == nullptr) {
      RMW_SET_ERROR_MSG("failed to copy enclave");
      return RMW_RET_BAD_ALLOC;
    }
  }
  rmw_ret_t ret = rmw_security_options_copy(
    &src->security_options, allocator, &tmp.security_options);
  if (ret != RMW_RET_OK) {
    allocator->deallocate(tmp.enclave, allocator->state);
    return ret;
  }
  ret = rmw_discovery_options_copy(&src->discovery_options, allocator, &tmp.discovery_options);
  if (ret != RMW_RET_OK) {
    allocator->deallocate(tmp.enclave, allocator->state);
    rmw_ret_t fini_ret = rmw_security_options_fini(&tmp.security_options, allocator);
    (void)fini_ret;
    return ret;
  }
  *dst = tmp;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_init_options_fini(rmw_init_options_t * init_options)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(init_options, RMW_RET_INVALID_ARGUMENT);
  if (init_options->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized init_options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    init_options, init_options->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  rcutils_allocator_t * allocator = &init_options->allocator;
  RCUTILS_CHECK_ALLOCATOR(allocator, return RMW_RET_INVALID_ARGUMENT);

  rmw_ret_t ret = rmw_security_options_fini(&init_options->security_options, allocator);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  ret = rmw_discovery_options_fini(&init_options->discovery_options);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  allocator->deallocate(init_options->enclave, allocator->state);
  *init_options = rmw_get_zero_initialized_init_options();
  return RMW_RET_OK;
}

rmw_ret_t
rmw_init(const rmw_init_options_t * options, rmw_context_t * context)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(options, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(context, RMW_RET_INVALID_ARGUMENT);
  if (options->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    options, options->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  if (options->enclave == nullptr) {
    RMW_SET_ERROR_MSG("expected non-null enclave");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected a zero-initialized context");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (options->security_options.enforce_security == RMW_SECURITY_ENFORCEMENT_ENFORCE) {
    RMW_SET_ERROR_MSG("security (SROS2) is not supported by rmw_intraprocess_cpp");
    return RMW_RET_UNSUPPORTED;
  }

  rmw_context_t tmp = rmw_get_zero_initialized_context();
  tmp.instance_id = options->instance_id;
  tmp.implementation_identifier = kIdentifier;
  // Single domain: the requested id is reported back but never used for matching.
  tmp.actual_domain_id =
    options->domain_id == RMW_DEFAULT_DOMAIN_ID ? 0u : options->domain_id;
  tmp.options = rmw_get_zero_initialized_init_options();
  rmw_ret_t ret = rmw_init_options_copy(options, &tmp.options);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  tmp.impl = new (std::nothrow) rmw_context_impl_t();
  if (tmp.impl == nullptr) {
    rmw_ret_t fini_ret = rmw_init_options_fini(&tmp.options);
    (void)fini_ret;
    RMW_SET_ERROR_MSG("failed to allocate context impl");
    return RMW_RET_BAD_ALLOC;
  }
  *context = tmp;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_shutdown(rmw_context_t * context)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(context, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->implementation_identifier, "expected initialized context",
    return RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    context, context->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->impl, "expected initialized context", return RMW_RET_INVALID_ARGUMENT);
  context->impl->is_shutdown.store(true);
  return RMW_RET_OK;
}

rmw_ret_t
rmw_context_fini(rmw_context_t * context)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(context, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->implementation_identifier, "expected initialized context",
    return RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    context, context->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->impl, "expected initialized context", return RMW_RET_INVALID_ARGUMENT);
  if (!context->impl->is_shutdown.load()) {
    RMW_SET_ERROR_MSG("context has not been shut down");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->impl->node_count.load() != 0) {
    RMW_SET_ERROR_MSG("context still has nodes; destroy them before rmw_context_fini");
    return RMW_RET_ERROR;
  }
  rmw_ret_t ret = rmw_init_options_fini(&context->options);
  if (ret != RMW_RET_OK) {
    return ret;
  }
  delete context->impl;
  *context = rmw_get_zero_initialized_context();
  return RMW_RET_OK;
}
}  // extern "C"
