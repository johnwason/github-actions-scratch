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

#ifndef EVENT_HPP_
#define EVENT_HPP_

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "rmw/event.h"
#include "rmw/event_callback_type.h"
#include "rmw/qos_policy_kind.h"

#include "new_data_callback.hpp"
#include "waitable.hpp"

namespace rmw_intraprocess_cpp
{

/// Status of one event type of one endpoint (behind rmw_event_t::data).
/// Owned by the endpoint; ready for rmw_wait while it has unread changes.
class EventState : public Waitable
{
public:
  explicit EventState(rmw_event_type_t type)
  : type_(type) {}

  rmw_event_type_t type() const {return type_;}

  // Recorders: update the status under the entity lock and wake wait sets.
  // They return true if the caller should then call fire_callback() (outside
  // the registry lock).
  bool record_matched(int delta);
  bool record_incompatible_qos(rmw_qos_policy_kind_t kind);
  bool record_incompatible_type();
  bool record_message_lost(std::size_t count);

  /// Calls the user callback (if any) with one new event.
  void fire_callback();

  /// Copies the status into `info` (the rmw status struct for type()) and
  /// resets the *_change fields.
  void take(void * info);

  bool has_unread() const;

  void set_callback(rmw_event_callback_t callback, const void * user_data);

  /// No more callbacks after this returns.
  void mark_destroyed();

private:
  bool changed_locked();

  const rmw_event_type_t type_;
  // Guarded by Waitable::mutex_.
  std::size_t unread_ = 0;
  bool destroyed_ = false;
  std::int64_t total_ = 0;
  std::int64_t total_change_ = 0;
  std::int64_t current_ = 0;
  std::int64_t current_change_ = 0;
  rmw_qos_policy_kind_t last_policy_kind_ = RMW_QOS_POLICY_INVALID;

  NewDataCallback new_data_callback_;
};

/// Event types this RMW supports (deadline and liveliness are not: PLAN.md Q3).
bool event_type_supported(rmw_event_type_t type);

}  // namespace rmw_intraprocess_cpp

#endif  // EVENT_HPP_
