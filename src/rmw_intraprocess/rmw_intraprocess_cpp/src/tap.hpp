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

#ifndef TAP_HPP_
#define TAP_HPP_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rmw_intraprocess_cpp/extension.h"

namespace rmw_intraprocess_cpp
{

/// The taps of the extension API (extension.h). Process-wide, like the
/// registry, and independent of it: taps are not graph entities.
///
/// Locking: `mutex_` guards the tap map only. A publish snapshots the matching
/// taps under it and calls them without it; each Tap counts the calls in
/// progress under its own mutex so remove() can wait for them. Lock order:
/// TapRegistry::mutex_ is never held while a Tap's mutex is taken or a
/// callback runs.
class TapRegistry
{
public:
  static TapRegistry & instance();

  uint64_t add(const char * topic_name, rmw_intraprocess_tap_callback_t callback, void * user_data);
  bool remove(uint64_t handle);

  /// True if any tap is registered (cheap check on the publish path).
  bool empty() const {return count_.load(std::memory_order_acquire) == 0;}

  /// Calls every tap of sample.topic_name (and the all-topics taps).
  void invoke(const rmw_intraprocess_tap_sample_t & sample);

private:
  struct Tap
  {
    std::string topic;  // empty: all topics
    rmw_intraprocess_tap_callback_t callback = nullptr;
    void * user_data = nullptr;
    std::mutex mutex;
    std::condition_variable idle;
    int in_flight = 0;
    bool removed = false;
  };

  mutable std::mutex mutex_;
  std::map<uint64_t, std::shared_ptr<Tap>> taps_;
  uint64_t next_handle_ = 1;
  std::atomic<std::size_t> count_{0};
};

}  // namespace rmw_intraprocess_cpp

#endif  // TAP_HPP_
