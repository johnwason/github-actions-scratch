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

#ifndef NEW_DATA_CALLBACK_HPP_
#define NEW_DATA_CALLBACK_HPP_

#include <cstddef>
#include <mutex>

#include "rmw/event_callback_type.h"

namespace rmw_intraprocess_cpp
{

/// The rmw "new data" callback contract, shared by
/// rmw_subscription_set_on_new_message_callback,
/// rmw_service_set_on_new_request_callback,
/// rmw_client_set_on_new_response_callback and rmw_event_set_callback (as the
/// DDS RMWs implement it, checked by rclcpp's on_new_*_callback tests):
/// - each new item is reported to the callback when it arrives;
/// - items that arrive while no callback is set are counted, and that count is
///   reported once when a callback is set, then reset to zero;
/// - taking items does not change the count.
/// rclcpp sets each callback twice in a row (a temporary, then the permanent
/// one); the pending count goes to the first and is not repeated.
///
/// The mutex is held while the user callback runs, so after set(nullptr) or
/// clear() returns no callback is running or will run. Never call report()
/// while holding the registry lock or an entity's queue lock.
class NewDataCallback
{
public:
  void set(rmw_event_callback_t callback, const void * user_data)
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callback_ = callback;
    user_data_ = user_data;
    if (callback_ != nullptr && unreported_ > 0) {
      const std::size_t n = unreported_;
      unreported_ = 0;
      callback_(user_data_, n);
    }
  }

  /// `count` new items arrived.
  void report(std::size_t count)
  {
    if (count == 0) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (callback_ != nullptr) {
      callback_(user_data_, count);
    } else {
      unreported_ += count;
    }
  }

  /// Entity destruction: drops the callback and the pending count.
  void clear()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    callback_ = nullptr;
    user_data_ = nullptr;
    unreported_ = 0;
  }

private:
  std::recursive_mutex mutex_;
  rmw_event_callback_t callback_ = nullptr;  // guarded by mutex_
  const void * user_data_ = nullptr;  // guarded by mutex_
  std::size_t unreported_ = 0;  // guarded by mutex_
};

}  // namespace rmw_intraprocess_cpp

#endif  // NEW_DATA_CALLBACK_HPP_
