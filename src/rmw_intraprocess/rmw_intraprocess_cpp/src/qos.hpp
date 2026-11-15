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

#ifndef QOS_HPP_
#define QOS_HPP_

#include <cstddef>
#include <vector>

#include "rmw/qos_profiles.h"
#include "rmw/types.h"

namespace rmw_intraprocess_cpp
{

enum class EndpointKind { kPublisher, kSubscription };

/// Rejects profiles with UNKNOWN policies. Sets the rmw error state on failure.
bool validate_qos(const rmw_qos_profile_t & requested);

/// True if any policy that adapt_best_available_qos adapts is BEST_AVAILABLE.
bool has_best_available_policy(const rmw_qos_profile_t & qos);

/// Adapts BEST_AVAILABLE policies to the endpoints already on the topic, with
/// the rules of rmw_dds_common's qos_profile_get_best_available_for_
/// {subscription,publisher} (which the DDS RMWs use), except that a duration is
/// "default" if it is 0 or infinite (this RMW reports defaults as infinite).
/// `others` are the actual QoS profiles of the existing publishers (for a
/// subscription) or subscriptions (for a publisher). Call before resolve_qos.
/// - Subscription: reliability RELIABLE / durability TRANSIENT_LOCAL /
///   liveliness MANUAL_BY_TOPIC if every publisher has it (so also with no
///   publishers), else BEST_EFFORT / VOLATILE / AUTOMATIC; deadline and lease
///   default if every publisher's is default, else the largest finite one.
/// - Publisher: RELIABLE, TRANSIENT_LOCAL; MANUAL_BY_TOPIC if any subscription
///   is; deadline and lease default if every subscription's is default, else
///   the smallest finite one.
void adapt_best_available_qos(
  const std::vector<rmw_qos_profile_t> & others, rmw_qos_profile_t & qos, EndpointKind kind);

/// Replaces SYSTEM_DEFAULT and any remaining BEST_AVAILABLE policies with the
/// values this RMW uses (what *_get_actual_qos reports):
/// - SYSTEM_DEFAULT: KEEP_LAST (depth 0 -> 1), RELIABLE, VOLATILE, AUTOMATIC.
/// - BEST_AVAILABLE publisher: the highest level (RELIABLE, TRANSIENT_LOCAL).
/// - BEST_AVAILABLE subscription: the most compatible level (BEST_EFFORT,
///   VOLATILE).
/// - default / BEST_AVAILABLE deadline, lifespan, lease duration: infinite.
rmw_qos_profile_t resolve_qos(const rmw_qos_profile_t & requested, EndpointKind kind);

/// Logs (once per call) a warning for QoS settings that are accepted but not
/// enforced: finite deadline, MANUAL_BY_TOPIC liveliness, finite lease duration
/// (PLAN.md Q3).
void warn_unenforced_qos(const rmw_qos_profile_t & qos, const char * topic_name);

/// Same contract as rmw_qos_profile_check_compatible.
rmw_ret_t check_qos_compatible(
  const rmw_qos_profile_t & publisher, const rmw_qos_profile_t & subscription,
  rmw_qos_compatibility_type_t * compatibility, char * reason, std::size_t reason_size);

}  // namespace rmw_intraprocess_cpp

#endif  // QOS_HPP_
