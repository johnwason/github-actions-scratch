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

#include "event.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "rmw/events_statuses/events_statuses.h"

namespace rmw_intraprocess_cpp
{

bool event_type_supported(rmw_event_type_t type)
{
  switch (type) {
    case RMW_EVENT_PUBLICATION_MATCHED:
    case RMW_EVENT_SUBSCRIPTION_MATCHED:
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE:
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE:
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE:
    case RMW_EVENT_MESSAGE_LOST:
      return true;
    default:
      return false;
  }
}

bool EventState::changed_locked()
{
  ++unread_;
  notify_locked();
  return !destroyed_;
}

bool EventState::record_matched(int delta)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (delta > 0) {
    total_ += delta;
    total_change_ += delta;
  }
  current_ += delta;
  current_change_ += delta;
  return changed_locked();
}

bool EventState::record_incompatible_qos(rmw_qos_policy_kind_t kind)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++total_;
  ++total_change_;
  last_policy_kind_ = kind;
  return changed_locked();
}

bool EventState::record_incompatible_type()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++total_;
  ++total_change_;
  return changed_locked();
}

bool EventState::record_message_lost(std::size_t count)
{
  std::lock_guard<std::mutex> lock(mutex_);
  total_ += static_cast<std::int64_t>(count);
  total_change_ += static_cast<std::int64_t>(count);
  return changed_locked();
}

void EventState::fire_callback()
{
  new_data_callback_.report(1);
}

void EventState::take(void * info)
{
  std::lock_guard<std::mutex> lock(mutex_);
  switch (type_) {
    case RMW_EVENT_PUBLICATION_MATCHED:
    case RMW_EVENT_SUBSCRIPTION_MATCHED: {
        auto s = static_cast<rmw_matched_status_t *>(info);
        s->total_count = static_cast<std::size_t>(total_);
        s->total_count_change = static_cast<std::size_t>(total_change_);
        s->current_count = static_cast<std::size_t>(current_);
        s->current_count_change = static_cast<std::int32_t>(current_change_);
        break;
      }
    case RMW_EVENT_OFFERED_QOS_INCOMPATIBLE:
    case RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE: {
        auto s = static_cast<rmw_qos_incompatible_event_status_t *>(info);
        s->total_count = static_cast<std::int32_t>(total_);
        s->total_count_change = static_cast<std::int32_t>(total_change_);
        s->last_policy_kind = last_policy_kind_;
        break;
      }
    case RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE:
    case RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE: {
        auto s = static_cast<rmw_incompatible_type_status_t *>(info);
        s->total_count = static_cast<std::int32_t>(total_);
        s->total_count_change = static_cast<std::int32_t>(total_change_);
        break;
      }
    case RMW_EVENT_MESSAGE_LOST: {
        auto s = static_cast<rmw_message_lost_status_t *>(info);
        s->total_count = static_cast<std::size_t>(total_);
        s->total_count_change = static_cast<std::size_t>(total_change_);
        break;
      }
    default:
      break;
  }
  total_change_ = 0;
  current_change_ = 0;
  unread_ = 0;
}

bool EventState::has_unread() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return unread_ > 0;
}

void EventState::set_callback(rmw_event_callback_t callback, const void * user_data)
{
  new_data_callback_.set(callback, user_data);
}

void EventState::mark_destroyed()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroyed_ = true;
  }
  new_data_callback_.clear();
}

}  // namespace rmw_intraprocess_cpp
