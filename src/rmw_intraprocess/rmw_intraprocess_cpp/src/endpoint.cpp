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

#include "endpoint.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "rcutils/env.h"
#include "rcutils/time.h"
#include "rmw/error_handling.h"
#include "rmw/time.h"
#include "rmw/validate_full_topic_name.h"

namespace rmw_intraprocess_cpp
{

rcutils_time_point_value_t now_ns()
{
  rcutils_time_point_value_t t = 0;
  if (rcutils_system_time_now(&t) != RCUTILS_RET_OK) {
    rcutils_reset_error();
    return 0;
  }
  return t;
}

rcutils_duration_value_t finite_duration_ns(const rmw_time_t & t)
{
  const rmw_time_t inf = RMW_DURATION_INFINITE;
  if ((t.sec == 0 && t.nsec == 0) || t.sec >= inf.sec) {
    return 0;
  }
  return static_cast<rcutils_duration_value_t>(rmw_time_total_nsec(t));
}

std::size_t keep_all_limit()
{
  static const std::size_t limit = [] {
      const char * value = nullptr;
      if (rcutils_get_env("RMW_INTRAPROCESS_KEEP_ALL_LIMIT", &value) == nullptr &&
        value != nullptr && value[0] != '\0')
      {
        char * end = nullptr;
        const unsigned long long v = std::strtoull(value, &end, 10);  // NOLINT(runtime/int)
        if (end != value && *end == '\0' && v > 0) {
          return static_cast<std::size_t>(v);
        }
      }
      return static_cast<std::size_t>(100000);
    }();
  return limit;
}

bool validate_topic_name(const char * topic_name, bool avoid_ros_namespace_conventions)
{
  if (topic_name == nullptr || topic_name[0] == '\0') {
    RMW_SET_ERROR_MSG("topic name is null or empty");
    return false;
  }
  if (avoid_ros_namespace_conventions) {
    return true;
  }
  int result = RMW_TOPIC_VALID;
  if (rmw_validate_full_topic_name(topic_name, &result, nullptr) != RMW_RET_OK) {
    return false;
  }
  if (result != RMW_TOPIC_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
      "invalid topic name '%s': %s", topic_name,
      rmw_full_topic_name_validation_result_string(result));
    return false;
  }
  return true;
}

void EndpointImpl::create_events(bool is_publisher)
{
  const rmw_event_type_t pub_types[] = {
    RMW_EVENT_PUBLICATION_MATCHED, RMW_EVENT_OFFERED_QOS_INCOMPATIBLE,
    RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE};
  const rmw_event_type_t sub_types[] = {
    RMW_EVENT_SUBSCRIPTION_MATCHED, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE,
    RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE, RMW_EVENT_MESSAGE_LOST};
  if (is_publisher) {
    for (rmw_event_type_t t : pub_types) {
      events[t] = std::make_shared<EventState>(t);
    }
  } else {
    for (rmw_event_type_t t : sub_types) {
      events[t] = std::make_shared<EventState>(t);
    }
  }
}

EventState * EndpointImpl::event(rmw_event_type_t event_type) const
{
  if (event_type < 0 || event_type >= RMW_EVENT_INVALID) {
    return nullptr;
  }
  return events[event_type].get();
}

void EndpointImpl::destroy_events()
{
  for (const auto & e : events) {
    if (e) {
      e->mark_destroyed();
    }
  }
}

bool SubscriptionImpl::enqueue_locked(Sample sample)
{
  sample.received_timestamp = now_ns();
  if (qos.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST) {
    while (queue_.size() >= qos.depth && !queue_.empty()) {
      if (queue_.front().expiry != 0) {
        --expiring_;
      }
      queue_.pop_front();
    }
  } else if (queue_.size() >= keep_all_limit()) {
    return false;
  }
  sample.reception_sequence_number = ++reception_sequence_number_;
  if (sample.expiry != 0) {
    ++expiring_;
  }
  queue_.push_back(std::move(sample));
  return true;
}

void SubscriptionImpl::purge_expired_locked()
{
  if (expiring_ == 0) {
    return;
  }
  const rcutils_time_point_value_t now = now_ns();
  auto it = std::remove_if(
    queue_.begin(), queue_.end(), [now](const Sample & s) {return s.expired(now);});
  // Every removed sample had an expiry (only those can be expired).
  expiring_ -= static_cast<std::size_t>(std::distance(it, queue_.end()));
  queue_.erase(it, queue_.end());
}

void SubscriptionImpl::deliver(Sample sample)
{
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (destroyed_) {
      return;
    }
    accepted = enqueue_locked(std::move(sample));
    if (accepted) {
      notify_locked();
    }
  }
  if (!accepted) {
    EventState * lost = event(RMW_EVENT_MESSAGE_LOST);
    if (lost != nullptr && lost->record_message_lost(1)) {
      lost->fire_callback();
    }
    return;
  }
  new_data_callback_.report(1);
}

void SubscriptionImpl::deliver_history(const std::vector<Sample> & samples)
{
  std::size_t delivered = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const rcutils_time_point_value_t now = now_ns();
    for (const Sample & s : samples) {
      if (!s.expired(now) && enqueue_locked(s)) {
        ++delivered;
      }
    }
    if (!queue_.empty()) {
      notify_locked();
    }
  }
  // Called (under the registry lock) before the subscription is returned to its
  // creator, so no callback can be set yet: this only counts the samples, and
  // a later set_on_new_message_callback reports them.
  new_data_callback_.report(delivered);
}

bool SubscriptionImpl::take(Sample & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  purge_expired_locked();
  if (queue_.empty()) {
    return false;
  }
  out = std::move(queue_.front());
  queue_.pop_front();
  if (out.expiry != 0) {
    --expiring_;
  }
  return true;
}

bool SubscriptionImpl::has_data()
{
  std::lock_guard<std::mutex> lock(mutex_);
  purge_expired_locked();
  return !queue_.empty();
}

std::size_t SubscriptionImpl::queued()
{
  std::lock_guard<std::mutex> lock(mutex_);
  purge_expired_locked();
  return queue_.size();
}

void SubscriptionImpl::set_on_new_message_callback(
  rmw_event_callback_t callback, const void * user_data)
{
  new_data_callback_.set(callback, user_data);
}

void SubscriptionImpl::mark_destroyed()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroyed_ = true;
    queue_.clear();
    expiring_ = 0;
  }
  new_data_callback_.clear();
  destroy_events();
}

}  // namespace rmw_intraprocess_cpp
