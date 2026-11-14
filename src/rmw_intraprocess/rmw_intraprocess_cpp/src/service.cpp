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

#include "service.hpp"

#include <mutex>
#include <utility>

#include "endpoint.hpp"

namespace rmw_intraprocess_cpp
{

bool service_endpoints_match(const ClientImpl & client, const ServiceImpl & service)
{
  return client.service_name == service.service_name &&
         client.type.type_name == service.type.type_name;
}

void MessageQueue::deliver(ServiceMessage message, const rmw_qos_profile_t & qos)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (destroyed_) {
      return;
    }
    message.received_timestamp = now_ns();
    if (qos.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST) {
      while (queue_.size() >= qos.depth && !queue_.empty()) {
        queue_.pop_front();
      }
    } else if (queue_.size() >= keep_all_limit()) {
      return;
    }
    queue_.push_back(std::move(message));
    notify_locked();
  }
  new_data_callback_.report(1);
}

bool MessageQueue::take(ServiceMessage & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return false;
  }
  out = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

bool MessageQueue::has_data() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return !queue_.empty();
}

void MessageQueue::set_callback(rmw_event_callback_t callback, const void * user_data)
{
  new_data_callback_.set(callback, user_data);
}

void MessageQueue::mark_destroyed()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroyed_ = true;
    queue_.clear();
  }
  new_data_callback_.clear();
}

}  // namespace rmw_intraprocess_cpp
