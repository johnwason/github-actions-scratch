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

#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/rmw.h"

#include "endpoint.hpp"
#include "event.hpp"
#include "identifier.hpp"

using rmw_intraprocess_cpp::EndpointImpl;
using rmw_intraprocess_cpp::EventState;
using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::PublisherImpl;
using rmw_intraprocess_cpp::SubscriptionImpl;

namespace
{

/// The same status on the other side (publisher <-> subscription). The upstream
/// test_rmw_implementation (and rmw_fastrtps_cpp) accept e.g. PUBLICATION_MATCHED on a
/// subscription; the status structs of each pair are identical.
rmw_event_type_t counterpart(rmw_event_type_t type)
{
  switch (type) {
    case RMW_EVENT_PUBLICATION_MATCHED: return RMW_EVENT_SUBSCRIPTION_MATCHED;
    case RMW_EVENT_SUBSCRIPTION_MATCHED: return RMW_EVENT_PUBLICATION_MATCHED;
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE: return RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE;
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE: return RMW_EVENT_OFFERED_QOS_INCOMPATIBLE;
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE: return RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE;
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE: return RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE;
    default: return RMW_EVENT_INVALID;
  }
}

rmw_ret_t event_init(
  rmw_event_t * rmw_event, const EndpointImpl & endpoint, rmw_event_type_t event_type)
{
  if (!rmw_intraprocess_cpp::event_type_supported(event_type)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "event type %d is not supported by rmw_intraprocess_cpp", static_cast<int>(event_type));
    return RMW_RET_UNSUPPORTED;
  }
  EventState * state = endpoint.event(event_type);
  if (state == nullptr) {
    state = endpoint.event(counterpart(event_type));
  }
  if (state == nullptr) {
    RMW_SET_ERROR_MSG("event type does not apply to this entity");
    return RMW_RET_INVALID_ARGUMENT;
  }
  rmw_event->implementation_identifier = kIdentifier;
  rmw_event->data = state;
  rmw_event->event_type = event_type;
  return RMW_RET_OK;
}

#define CHECK_EVENT(event) \
  RMW_CHECK_ARGUMENT_FOR_NULL(event, RMW_RET_INVALID_ARGUMENT); \
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH( \
    event, event->implementation_identifier, kIdentifier, \
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION)

}  // namespace

extern "C"
{
bool
rmw_event_type_is_supported(rmw_event_type_t rmw_event_type)
{
  return rmw_intraprocess_cpp::event_type_supported(rmw_event_type);
}

rmw_ret_t
rmw_publisher_event_init(
  rmw_event_t * rmw_event, const rmw_publisher_t * publisher, rmw_event_type_t event_type)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    publisher, publisher->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  return event_init(rmw_event, *static_cast<const PublisherImpl *>(publisher->data), event_type);
}

rmw_ret_t
rmw_subscription_event_init(
  rmw_event_t * rmw_event, const rmw_subscription_t * subscription, rmw_event_type_t event_type)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(rmw_event, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(subscription, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    subscription, subscription->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  return event_init(
    rmw_event, *static_cast<const SubscriptionImpl *>(subscription->data), event_type);
}

rmw_ret_t
rmw_take_event(const rmw_event_t * event_handle, void * event_info, bool * taken)
{
  CHECK_EVENT(event_handle);
  RMW_CHECK_ARGUMENT_FOR_NULL(event_info, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_ARGUMENT_FOR_NULL(taken, RMW_RET_INVALID_ARGUMENT);
  auto state = static_cast<EventState *>(event_handle->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(state, "event impl is null", return RMW_RET_ERROR);
  state->take(event_info);
  *taken = true;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_event_set_callback(rmw_event_t * event, rmw_event_callback_t callback, const void * user_data)
{
  CHECK_EVENT(event);
  auto state = static_cast<EventState *>(event->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(state, "event impl is null", return RMW_RET_ERROR);
  state->set_callback(callback, user_data);
  return RMW_RET_OK;
}

rmw_ret_t
rmw_publisher_assert_liveliness(const rmw_publisher_t * publisher)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(publisher, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    publisher, publisher->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  // Liveliness is not tracked (PLAN.md Q3): an in-process publisher is alive
  // as long as it exists, so asserting is a no-op.
  return RMW_RET_OK;
}
}  // extern "C"
