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

#ifndef GUARD_CONDITION_HPP_
#define GUARD_CONDITION_HPP_

#include <mutex>

#include "waitable.hpp"

namespace rmw_intraprocess_cpp
{

/// Guard condition: a triggered flag that wakes attached wait sets.
class GuardCondition : public Waitable
{
public:
  void trigger()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    triggered_ = true;
    notify_locked();
  }

  /// Returns whether the condition was triggered and clears it.
  bool take_triggered()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bool was = triggered_;
    triggered_ = false;
    return was;
  }

  bool is_triggered() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return triggered_;
  }

private:
  bool triggered_ = false;  // guarded by mutex_
};

}  // namespace rmw_intraprocess_cpp

#endif  // GUARD_CONDITION_HPP_
