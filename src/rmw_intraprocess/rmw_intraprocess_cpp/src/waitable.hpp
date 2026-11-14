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

#ifndef WAITABLE_HPP_
#define WAITABLE_HPP_

#include <algorithm>
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace rmw_intraprocess_cpp
{

/// Implementation behind rmw_wait_set_t::data. See docs/design.md "Wait sets".
class WaitSetImpl
{
public:
  /// Called by an entity (holding its own lock) when it becomes ready.
  void notify()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      triggered_ = true;
    }
    cv_.notify_all();
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  bool triggered_ = false;  // guarded by mutex_
};

/// Base of every entity rmw_wait can wait on. mutex_ is the entity lock
/// (queue lock for subscriptions); it is taken before any WaitSetImpl lock.
class Waitable
{
public:
  Waitable() = default;
  Waitable(const Waitable &) = delete;
  Waitable & operator=(const Waitable &) = delete;
  virtual ~Waitable() = default;

  void attach(WaitSetImpl * wait_set)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    waiters_.push_back(wait_set);
  }

  void detach(WaitSetImpl * wait_set)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(waiters_.begin(), waiters_.end(), wait_set);
    if (it != waiters_.end()) {
      waiters_.erase(it);
    }
  }

  /// Number of wait sets currently attached (tests: rmw_wait must detach).
  std::size_t waiter_count() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return waiters_.size();
  }

protected:
  /// Wakes every attached wait set. Caller holds mutex_.
  void notify_locked()
  {
    for (WaitSetImpl * w : waiters_) {
      w->notify();
    }
  }

  mutable std::mutex mutex_;

private:
  std::vector<WaitSetImpl *> waiters_;  // guarded by mutex_
};

}  // namespace rmw_intraprocess_cpp

#endif  // WAITABLE_HPP_
