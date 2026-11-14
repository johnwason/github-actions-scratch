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

#ifndef ENDPOINT_HPP_
#define ENDPOINT_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rcutils/time.h"
#include "rmw/event.h"
#include "rmw/event_callback_type.h"
#include "rmw/types.h"

#include "event.hpp"
#include "new_data_callback.hpp"
#include "node.hpp"
#include "type_support.hpp"
#include "waitable.hpp"

namespace rmw_intraprocess_cpp
{

/// State shared by publishers and subscriptions.
struct EndpointImpl
{
  NodeImpl * node = nullptr;
  /// Copied from the node: endpoints never dereference the node on the data path.
  const rmw_context_t * context = nullptr;
  std::string topic_name;
  MessageTypeSupport type;
  rmw_qos_profile_t qos{};
  rmw_gid_t gid{};
  /// Event states by rmw_event_type_t; null for types this endpoint lacks.
  std::array<std::shared_ptr<EventState>, RMW_EVENT_INVALID> events;

  /// Creates the event states for a publisher or a subscription.
  void create_events(bool is_publisher);
  /// The event state, or null if this endpoint does not support the type.
  EventState * event(rmw_event_type_t event_type) const;
  /// Stops all event callbacks (endpoint destruction).
  void destroy_events();
};

/// One published message as stored in a subscription queue.
struct Sample
{
  std::shared_ptr<const std::vector<std::uint8_t>> cdr;
  rmw_gid_t publisher_gid{};
  rcutils_time_point_value_t source_timestamp = 0;
  rcutils_time_point_value_t received_timestamp = 0;
  /// Lifespan expiry (system time, ns); 0 = never expires.
  rcutils_time_point_value_t expiry = 0;
  std::uint64_t publication_sequence_number = 0;
  std::uint64_t reception_sequence_number = 0;

  bool expired(rcutils_time_point_value_t now) const {return expiry != 0 && now >= expiry;}
};

struct PublisherImpl : EndpointImpl
{
  rmw_publisher_options_t options{};
  std::atomic<std::uint64_t> sequence_number{0};
  /// Lifespan in ns (0 = infinite).
  rcutils_duration_value_t lifespan_ns = 0;
  /// TRANSIENT_LOCAL history for late-joining subscriptions. Guarded by the
  /// registry mutex (see Registry::prepare_publish / add_subscription).
  std::deque<Sample> history;
};

/// Subscription state. Owned by the registry through a shared_ptr so that a
/// concurrent publish can finish delivering after rmw_destroy_subscription.
class SubscriptionImpl : public EndpointImpl, public Waitable
{
public:
  rmw_subscription_options_t options{};

  /// Enqueues a sample (KEEP_LAST drops the oldest; KEEP_ALL rejects it past
  /// the limit and records MESSAGE_LOST), wakes wait sets, then calls the
  /// new-message callback outside the queue lock.
  void deliver(Sample sample);

  /// Enqueues historical (TRANSIENT_LOCAL) samples on a new subscription. No
  /// callback can be set yet, so none is called.
  void deliver_history(const std::vector<Sample> & samples);

  /// Pops the oldest unexpired sample. Returns false if there is none.
  bool take(Sample & out);

  /// True if an unexpired sample is queued (wait set readiness).
  bool has_data();

  std::size_t queued();

  /// Sets or clears (callback == nullptr) the new-message callback (contract:
  /// NewDataCallback).
  void set_on_new_message_callback(rmw_event_callback_t callback, const void * user_data);

  /// Marks the subscription destroyed: no more deliveries or callbacks.
  void mark_destroyed();

private:
  /// Enqueue under mutex_. Returns false if the sample was rejected (KEEP_ALL limit).
  bool enqueue_locked(Sample sample);
  void purge_expired_locked();

  std::deque<Sample> queue_;  // guarded by Waitable::mutex_
  std::size_t expiring_ = 0;  // samples in queue_ with an expiry; guarded by mutex_
  std::uint64_t reception_sequence_number_ = 0;  // guarded by Waitable::mutex_
  bool destroyed_ = false;  // guarded by Waitable::mutex_

  NewDataCallback new_data_callback_;
};

/// Maximum queued samples for KEEP_ALL endpoints (env RMW_INTRAPROCESS_KEEP_ALL_LIMIT,
/// default 100000).
std::size_t keep_all_limit();

/// Validates a topic name for endpoint creation: a fully qualified ROS name, or
/// any non-empty name when avoid_ros_namespace_conventions is set (native name).
/// Sets the rmw error state and returns false if it is invalid.
bool validate_topic_name(const char * topic_name, bool avoid_ros_namespace_conventions);

/// Current system time in nanoseconds (0 if the clock fails).
rcutils_time_point_value_t now_ns();

/// rmw_time_t -> ns; 0 for "default"/infinite durations.
rcutils_duration_value_t finite_duration_ns(const rmw_time_t & t);

}  // namespace rmw_intraprocess_cpp

#endif  // ENDPOINT_HPP_
