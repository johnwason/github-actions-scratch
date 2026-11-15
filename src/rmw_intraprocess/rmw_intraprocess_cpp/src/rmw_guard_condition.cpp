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

#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "guard_condition.hpp"
#include "identifier.hpp"

using rmw_intraprocess_cpp::GuardCondition;
using rmw_intraprocess_cpp::kIdentifier;

extern "C"
{
rmw_guard_condition_t *
rmw_create_guard_condition(rmw_context_t * context)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(context, nullptr);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->implementation_identifier, "expected initialized context", return nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    context, context->implementation_identifier, kIdentifier, return nullptr);

  auto gc = new (std::nothrow) GuardCondition();
  auto handle = new (std::nothrow) rmw_guard_condition_t();
  if (gc == nullptr || handle == nullptr) {
    delete gc;
    delete handle;
    RMW_SET_ERROR_MSG("failed to allocate guard condition");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->data = gc;
  handle->context = context;
  return handle;
}

rmw_ret_t
rmw_destroy_guard_condition(rmw_guard_condition_t * guard_condition)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(guard_condition, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    guard_condition, guard_condition->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  delete static_cast<GuardCondition *>(guard_condition->data);
  delete guard_condition;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_trigger_guard_condition(const rmw_guard_condition_t * guard_condition)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(guard_condition, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    guard_condition, guard_condition->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  auto gc = static_cast<GuardCondition *>(guard_condition->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(gc, "guard condition impl is null", return RMW_RET_ERROR);
  gc->trigger();
  return RMW_RET_OK;
}
}  // extern "C"
