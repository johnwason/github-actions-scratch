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

#include "tap.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rmw/error_handling.h"

namespace rmw_intraprocess_cpp
{

namespace
{
/// The tap whose callback runs on this thread (nested publishes can stack
/// several), so remove() from inside a callback does not wait for itself.
thread_local std::vector<const void *> t_running_taps;
}  // namespace

TapRegistry & TapRegistry::instance()
{
  // Leaked on purpose, like the registry: never destroyed before a late
  // publish from another library's static destructor.
  static TapRegistry * registry = new TapRegistry();
  return *registry;
}

uint64_t TapRegistry::add(
  const char * topic_name, rmw_intraprocess_tap_callback_t callback, void * user_data)
{
  if (callback == nullptr || (topic_name != nullptr && topic_name[0] != '/')) {
    return 0;
  }
  auto tap = std::make_shared<Tap>();
  tap->topic = topic_name != nullptr ? topic_name : "";
  tap->callback = callback;
  tap->user_data = user_data;
  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t handle = next_handle_++;
  taps_.emplace(handle, std::move(tap));
  count_.store(taps_.size(), std::memory_order_release);
  return handle;
}

bool TapRegistry::remove(uint64_t handle)
{
  std::shared_ptr<Tap> tap;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = taps_.find(handle);
    if (it == taps_.end()) {
      return false;
    }
    tap = std::move(it->second);
    taps_.erase(it);
    count_.store(taps_.size(), std::memory_order_release);
  }
  int own = 0;
  for (const void * running : t_running_taps) {
    if (running == tap.get()) {
      ++own;
    }
  }
  std::unique_lock<std::mutex> lock(tap->mutex);
  tap->removed = true;
  tap->idle.wait(lock, [&tap, own] {return tap->in_flight <= own;});
  return true;
}

void TapRegistry::invoke(const rmw_intraprocess_tap_sample_t & sample)
{
  std::vector<std::shared_ptr<Tap>> matching;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & entry : taps_) {
      const Tap & tap = *entry.second;
      if (tap.topic.empty() || tap.topic == sample.topic_name) {
        matching.push_back(entry.second);
      }
    }
  }
  for (const auto & tap : matching) {
    {
      std::lock_guard<std::mutex> lock(tap->mutex);
      if (tap->removed) {
        continue;
      }
      ++tap->in_flight;
    }
    t_running_taps.push_back(tap.get());
    try {
      tap->callback(&sample, tap->user_data);
    } catch (...) {
      // A C callback cannot throw; a C++ one must not (it would unwind
      // through rmw_publish). Contained so the tap stays removable.
    }
    t_running_taps.pop_back();
    {
      std::lock_guard<std::mutex> lock(tap->mutex);
      --tap->in_flight;
    }
    tap->idle.notify_all();
  }
}

}  // namespace rmw_intraprocess_cpp

namespace
{

uint64_t add_tap(
  const char * topic_name, rmw_intraprocess_tap_callback_t callback, void * user_data)
{
  try {
    return rmw_intraprocess_cpp::TapRegistry::instance().add(topic_name, callback, user_data);
  } catch (const std::exception &) {
    return 0;
  }
}

rmw_ret_t remove_tap(uint64_t handle)
{
  if (!rmw_intraprocess_cpp::TapRegistry::instance().remove(handle)) {
    RMW_SET_ERROR_MSG("unknown tap handle");
    return RMW_RET_ERROR;
  }
  return RMW_RET_OK;
}

const rmw_intraprocess_extension_api_t kApiV1 = {
  RMW_INTRAPROCESS_EXTENSION_API_VERSION,
  sizeof(rmw_intraprocess_extension_api_t),
  &add_tap,
  &remove_tap,
};

}  // namespace

extern "C"
{
const rmw_intraprocess_extension_api_t *
rmw_intraprocess_cpp_get_extension_api(uint32_t version)
{
  if (version < 1u || version > RMW_INTRAPROCESS_EXTENSION_API_VERSION) {
    return nullptr;
  }
  return &kApiV1;
}
}  // extern "C"
