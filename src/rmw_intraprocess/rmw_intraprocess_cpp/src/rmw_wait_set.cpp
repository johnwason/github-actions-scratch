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

// rmw_wait: see docs/design.md "Wait sets" for the protocol and why it has no
// lost wakeups.

#include <chrono>
#include <cstddef>
#include <mutex>
#include <new>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "endpoint.hpp"
#include "event.hpp"
#include "guard_condition.hpp"
#include "service.hpp"
#include "identifier.hpp"
#include "waitable.hpp"

using rmw_intraprocess_cpp::EventState;
using rmw_intraprocess_cpp::ClientImpl;
using rmw_intraprocess_cpp::GuardCondition;
using rmw_intraprocess_cpp::ServiceImpl;
using rmw_intraprocess_cpp::kIdentifier;
using rmw_intraprocess_cpp::SubscriptionImpl;
using rmw_intraprocess_cpp::WaitSetImpl;

namespace
{

struct Entities
{
  rmw_subscriptions_t * subscriptions;
  rmw_guard_conditions_t * guard_conditions;
  rmw_services_t * services;
  rmw_clients_t * clients;
  rmw_events_t * events;

  std::size_t subscription_count() const
  {
    return subscriptions != nullptr ? subscriptions->subscriber_count : 0u;
  }
  std::size_t guard_condition_count() const
  {
    return guard_conditions != nullptr ? guard_conditions->guard_condition_count : 0u;
  }
  SubscriptionImpl * subscription(std::size_t i) const
  {
    return static_cast<SubscriptionImpl *>(subscriptions->subscribers[i]);
  }
  GuardCondition * guard_condition(std::size_t i) const
  {
    return static_cast<GuardCondition *>(guard_conditions->guard_conditions[i]);
  }
  std::size_t service_count() const
  {
    return services != nullptr ? services->service_count : 0u;
  }
  std::size_t client_count() const
  {
    return clients != nullptr ? clients->client_count : 0u;
  }
  ServiceImpl * service(std::size_t i) const
  {
    return static_cast<ServiceImpl *>(services->services[i]);
  }
  ClientImpl * client(std::size_t i) const
  {
    return static_cast<ClientImpl *>(clients->clients[i]);
  }
  std::size_t event_count() const
  {
    return events != nullptr ? events->event_count : 0u;
  }
  /// rcl passes rmw_event_t * (not its data) in the events array.
  EventState * event(std::size_t i) const
  {
    auto e = static_cast<rmw_event_t *>(events->events[i]);
    return e != nullptr ? static_cast<EventState *>(e->data) : nullptr;
  }

  template<typename F>
  void for_each_waitable(F f) const
  {
    for (std::size_t i = 0; i < subscription_count(); ++i) {
      if (subscription(i) != nullptr) {
        f(*subscription(i));
      }
    }
    for (std::size_t i = 0; i < guard_condition_count(); ++i) {
      if (guard_condition(i) != nullptr) {
        f(*guard_condition(i));
      }
    }
    for (std::size_t i = 0; i < event_count(); ++i) {
      if (event(i) != nullptr) {
        f(*event(i));
      }
    }
    for (std::size_t i = 0; i < service_count(); ++i) {
      if (service(i) != nullptr) {
        f(*static_cast<rmw_intraprocess_cpp::Waitable *>(service(i)));
      }
    }
    for (std::size_t i = 0; i < client_count(); ++i) {
      if (client(i) != nullptr) {
        f(*static_cast<rmw_intraprocess_cpp::Waitable *>(client(i)));
      }
    }
  }

  bool any_ready() const
  {
    for (std::size_t i = 0; i < subscription_count(); ++i) {
      if (subscription(i) != nullptr && subscription(i)->has_data()) {
        return true;
      }
    }
    for (std::size_t i = 0; i < guard_condition_count(); ++i) {
      if (guard_condition(i) != nullptr && guard_condition(i)->is_triggered()) {
        return true;
      }
    }
    for (std::size_t i = 0; i < event_count(); ++i) {
      if (event(i) != nullptr && event(i)->has_unread()) {
        return true;
      }
    }
    for (std::size_t i = 0; i < service_count(); ++i) {
      if (service(i) != nullptr && service(i)->has_data()) {
        return true;
      }
    }
    for (std::size_t i = 0; i < client_count(); ++i) {
      if (client(i) != nullptr && client(i)->has_data()) {
        return true;
      }
    }
    return false;
  }

  /// Nulls entries that are not ready; consumes guard condition triggers.
  bool finalize() const
  {
    bool any = false;
    for (std::size_t i = 0; i < subscription_count(); ++i) {
      if (subscription(i) != nullptr && subscription(i)->has_data()) {
        any = true;
      } else {
        subscriptions->subscribers[i] = nullptr;
      }
    }
    for (std::size_t i = 0; i < guard_condition_count(); ++i) {
      if (guard_condition(i) != nullptr && guard_condition(i)->take_triggered()) {
        any = true;
      } else {
        guard_conditions->guard_conditions[i] = nullptr;
      }
    }
    for (std::size_t i = 0; i < service_count(); ++i) {
      if (service(i) != nullptr && service(i)->has_data()) {
        any = true;
      } else {
        services->services[i] = nullptr;
      }
    }
    for (std::size_t i = 0; i < client_count(); ++i) {
      if (client(i) != nullptr && client(i)->has_data()) {
        any = true;
      } else {
        clients->clients[i] = nullptr;
      }
    }
    for (std::size_t i = 0; i < event_count(); ++i) {
      if (event(i) != nullptr && event(i)->has_unread()) {
        any = true;
      } else {
        events->events[i] = nullptr;
      }
    }
    return any;
  }
};

/// Timeouts at least this long are treated as infinite (avoids time_point overflow).
constexpr std::uint64_t kInfiniteSeconds = 100ull * 365ull * 24ull * 3600ull;

}  // namespace

extern "C"
{
rmw_wait_set_t *
rmw_create_wait_set(rmw_context_t * context, size_t max_conditions)
{
  (void)max_conditions;  // not needed: entities are attached per rmw_wait call
  RMW_CHECK_ARGUMENT_FOR_NULL(context, nullptr);
  RMW_CHECK_FOR_NULL_WITH_MSG(
    context->implementation_identifier, "expected initialized context", return nullptr);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    context, context->implementation_identifier, kIdentifier, return nullptr);

  auto impl = new (std::nothrow) WaitSetImpl();
  auto handle = new (std::nothrow) rmw_wait_set_t();
  if (impl == nullptr || handle == nullptr) {
    delete impl;
    delete handle;
    RMW_SET_ERROR_MSG("failed to allocate wait set");
    return nullptr;
  }
  handle->implementation_identifier = kIdentifier;
  handle->guard_conditions = nullptr;
  handle->data = impl;
  return handle;
}

rmw_ret_t
rmw_destroy_wait_set(rmw_wait_set_t * wait_set)
{
  // test_rmw_implementation expects RMW_RET_ERROR (not INVALID_ARGUMENT) here.
  RMW_CHECK_ARGUMENT_FOR_NULL(wait_set, RMW_RET_ERROR);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    wait_set, wait_set->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  delete static_cast<WaitSetImpl *>(wait_set->data);
  delete wait_set;
  return RMW_RET_OK;
}

rmw_ret_t
rmw_wait(
  rmw_subscriptions_t * subscriptions,
  rmw_guard_conditions_t * guard_conditions,
  rmw_services_t * services,
  rmw_clients_t * clients,
  rmw_events_t * events,
  rmw_wait_set_t * wait_set,
  const rmw_time_t * wait_timeout)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(wait_set, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    wait_set, wait_set->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  auto ws = static_cast<WaitSetImpl *>(wait_set->data);
  RMW_CHECK_FOR_NULL_WITH_MSG(ws, "wait set impl is null", return RMW_RET_ERROR);

  const Entities e{subscriptions, guard_conditions, services, clients, events};

  const bool infinite = wait_timeout == nullptr || wait_timeout->sec >= kInfiniteSeconds;
  const bool zero = !infinite && wait_timeout->sec == 0 && wait_timeout->nsec == 0;
  std::chrono::steady_clock::time_point deadline{};
  if (!infinite && !zero) {
    deadline = std::chrono::steady_clock::now() +
      std::chrono::seconds(static_cast<std::int64_t>(wait_timeout->sec)) +
      std::chrono::nanoseconds(static_cast<std::int64_t>(wait_timeout->nsec));
  }

  {
    std::lock_guard<std::mutex> lock(ws->mutex_);
    ws->triggered_ = false;
  }
  e.for_each_waitable([ws](rmw_intraprocess_cpp::Waitable & w) {w.attach(ws);});

  while (!zero && !e.any_ready()) {
    std::unique_lock<std::mutex> lock(ws->mutex_);
    bool timed_out = false;
    if (infinite) {
      ws->cv_.wait(lock, [ws] {return ws->triggered_;});
    } else {
      timed_out = !ws->cv_.wait_until(lock, deadline, [ws] {return ws->triggered_;});
    }
    ws->triggered_ = false;
    if (timed_out) {
      break;
    }
  }

  e.for_each_waitable([ws](rmw_intraprocess_cpp::Waitable & w) {w.detach(ws);});
  return e.finalize() ? RMW_RET_OK : RMW_RET_TIMEOUT;
}
}  // extern "C"
