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

#include "qos.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#include "rcutils/logging_macros.h"
#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"
#include "rmw/time.h"

namespace rmw_intraprocess_cpp
{

namespace
{

bool is_default_duration(const rmw_time_t & t)
{
  return t.sec == 0 && t.nsec == 0;
}

bool same_duration(const rmw_time_t & a, const rmw_time_t & b)
{
  return a.sec == b.sec && a.nsec == b.nsec;
}

/// Deadline / lease duration: default (0) and "infinite" both mean no limit.
bool is_infinite(const rmw_time_t & t)
{
  const rmw_time_t inf = RMW_DURATION_INFINITE;
  return is_default_duration(t) || t.sec > inf.sec || (t.sec == inf.sec && t.nsec >= inf.nsec - 1);
}

bool longer(const rmw_time_t & a, const rmw_time_t & b)
{
  return a.sec > b.sec || (a.sec == b.sec && a.nsec > b.nsec);
}

void append(char * reason, std::size_t reason_size, const char * text)
{
  if (reason == nullptr || reason_size == 0) {
    return;
  }
  const std::size_t used = std::strlen(reason);
  if (used + 1 >= reason_size) {
    return;
  }
  std::snprintf(reason + used, reason_size - used, "%s", text);
}

}  // namespace

bool validate_qos(const rmw_qos_profile_t & q)
{
  if (q.history == RMW_QOS_POLICY_HISTORY_UNKNOWN ||
    q.reliability == RMW_QOS_POLICY_RELIABILITY_UNKNOWN ||
    q.durability == RMW_QOS_POLICY_DURABILITY_UNKNOWN ||
    q.liveliness == RMW_QOS_POLICY_LIVELINESS_UNKNOWN)
  {
    RMW_SET_ERROR_MSG("QoS profile has UNKNOWN policies");
    return false;
  }
  return true;
}

bool has_best_available_policy(const rmw_qos_profile_t & qos)
{
  const rmw_time_t deadline_best = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  const rmw_time_t lease_best = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  return qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE ||
         qos.durability == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE ||
         qos.liveliness == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE ||
         same_duration(qos.deadline, deadline_best) ||
         same_duration(qos.liveliness_lease_duration, lease_best);
}

void adapt_best_available_qos(
  const std::vector<rmw_qos_profile_t> & others, rmw_qos_profile_t & qos, EndpointKind kind)
{
  const bool pub = kind == EndpointKind::kPublisher;
  std::size_t reliable = 0;
  std::size_t transient_local = 0;
  std::size_t manual = 0;
  std::size_t deadline_default = 0;
  std::size_t lease_default = 0;
  bool have_deadline = false;
  bool have_lease = false;
  rmw_time_t deadline{0, 0};
  rmw_time_t lease{0, 0};
  // Largest finite value for a subscription, smallest for a publisher.
  auto better = [pub](const rmw_time_t & candidate, const rmw_time_t & current) {
      return pub ? longer(current, candidate) : longer(candidate, current);
    };
  for (const rmw_qos_profile_t & o : others) {
    reliable += o.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE ? 1u : 0u;
    transient_local += o.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL ? 1u : 0u;
    manual += o.liveliness == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC ? 1u : 0u;
    if (is_infinite(o.deadline)) {
      ++deadline_default;
    } else if (!have_deadline || better(o.deadline, deadline)) {
      deadline = o.deadline;
      have_deadline = true;
    }
    if (is_infinite(o.liveliness_lease_duration)) {
      ++lease_default;
    } else if (!have_lease || better(o.liveliness_lease_duration, lease)) {
      lease = o.liveliness_lease_duration;
      have_lease = true;
    }
  }
  const std::size_t n = others.size();
  const rmw_time_t unspecified = RMW_DURATION_UNSPECIFIED;  // the "default" duration
  if (qos.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE) {
    qos.reliability = (pub || reliable == n) ?
      RMW_QOS_POLICY_RELIABILITY_RELIABLE : RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  }
  if (qos.durability == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE) {
    qos.durability = (pub || transient_local == n) ?
      RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL : RMW_QOS_POLICY_DURABILITY_VOLATILE;
  }
  if (qos.liveliness == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE) {
    const bool use_manual = pub ? manual > 0 : manual == n;
    qos.liveliness = use_manual ?
      RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC : RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  }
  const rmw_time_t deadline_best = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  if (same_duration(qos.deadline, deadline_best)) {
    qos.deadline = deadline_default == n ? unspecified : deadline;
  }
  const rmw_time_t lease_best = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  if (same_duration(qos.liveliness_lease_duration, lease_best)) {
    qos.liveliness_lease_duration =
      lease_default == n ? unspecified : lease;
  }
}

rmw_qos_profile_t resolve_qos(const rmw_qos_profile_t & requested, EndpointKind kind)
{
  const bool pub = kind == EndpointKind::kPublisher;
  rmw_qos_profile_t q = requested;
  if (q.history != RMW_QOS_POLICY_HISTORY_KEEP_LAST &&
    q.history != RMW_QOS_POLICY_HISTORY_KEEP_ALL)
  {
    q.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  }
  if (q.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST && q.depth == 0) {
    q.depth = 1;
  }
  switch (q.reliability) {
    case RMW_QOS_POLICY_RELIABILITY_RELIABLE:
    case RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT:
      break;
    case RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE:
      q.reliability = pub ? RMW_QOS_POLICY_RELIABILITY_RELIABLE :
        RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
      break;
    default:  // SYSTEM_DEFAULT
      q.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
      break;
  }
  switch (q.durability) {
    case RMW_QOS_POLICY_DURABILITY_VOLATILE:
    case RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL:
      break;
    case RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE:
      q.durability = pub ? RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL :
        RMW_QOS_POLICY_DURABILITY_VOLATILE;
      break;
    default:  // SYSTEM_DEFAULT
      q.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
      break;
  }
  if (q.liveliness != RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
    q.liveliness != RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC)
  {
    q.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  }
  // The actual QoS reports "default"/"best available" durations as infinite,
  // like the DDS RMWs (rcl test_get_actual_qos expects >= a very large value).
  const rmw_time_t deadline_best = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  const rmw_time_t lease_best = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  const rmw_time_t infinite = RMW_DURATION_INFINITE;
  if (is_infinite(q.deadline) || same_duration(q.deadline, deadline_best)) {
    q.deadline = infinite;
  }
  if (is_infinite(q.lifespan)) {
    q.lifespan = infinite;
  }
  if (is_infinite(q.liveliness_lease_duration) ||
    same_duration(q.liveliness_lease_duration, lease_best))
  {
    q.liveliness_lease_duration = infinite;
  }
  return q;
}

void warn_unenforced_qos(const rmw_qos_profile_t & q, const char * topic_name)
{
  const bool deadline = !is_infinite(q.deadline);
  const bool manual = q.liveliness == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  const bool lease = !is_infinite(q.liveliness_lease_duration);
  if (deadline || manual || lease) {
    RCUTILS_LOG_WARN_NAMED(
      "rmw_intraprocess_cpp",
      "topic '%s': deadline and liveliness QoS are accepted but not enforced "
      "(deadline=%s, liveliness=%s, lease=%s); their events are unsupported",
      topic_name, deadline ? "finite" : "infinite", manual ? "MANUAL_BY_TOPIC" : "AUTOMATIC",
      lease ? "finite" : "infinite");
  }
}

rmw_ret_t check_qos_compatible(
  const rmw_qos_profile_t & pub, const rmw_qos_profile_t & sub,
  rmw_qos_compatibility_type_t * compatibility, char * reason, std::size_t reason_size)
{
  if (compatibility == nullptr) {
    RMW_SET_ERROR_MSG("compatibility parameter is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (reason == nullptr && reason_size != 0u) {
    RMW_SET_ERROR_MSG("reason parameter is null, but reason_size parameter is not zero");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *compatibility = RMW_QOS_COMPATIBILITY_OK;
  if (reason != nullptr && reason_size > 0u) {
    reason[0] = '\0';
  }

  // BEST_AVAILABLE is compatible with anything on either side.
  const bool pub_rel_best = pub.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE;
  const bool sub_rel_best = sub.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE;
  const bool pub_dur_best = pub.durability == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE;
  const bool sub_dur_best = sub.durability == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE;
  const rmw_time_t deadline_best = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  const rmw_time_t lease_best = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  const bool deadline_best_any =
    same_duration(pub.deadline, deadline_best) || same_duration(sub.deadline, deadline_best);
  const bool lease_best_any =
    same_duration(pub.liveliness_lease_duration, lease_best) ||
    same_duration(sub.liveliness_lease_duration, lease_best);
  const bool liveliness_best_any =
    pub.liveliness == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE ||
    sub.liveliness == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE;

  auto error = [&](const char * text) {
      *compatibility = RMW_QOS_COMPATIBILITY_ERROR;
      append(reason, reason_size, text);
    };
  if (!pub_rel_best && !sub_rel_best &&
    pub.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
    sub.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE)
  {
    error("ERROR: Best effort publisher and reliable subscription;");
  }
  if (!pub_dur_best && !sub_dur_best &&
    pub.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
    sub.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
  {
    error("ERROR: Volatile publisher and transient local subscription;");
  }
  if (!deadline_best_any && !is_infinite(sub.deadline)) {
    if (is_infinite(pub.deadline)) {
      error("ERROR: Subscription has a deadline, but publisher does not;");
    } else if (longer(pub.deadline, sub.deadline)) {
      error("ERROR: Subscription deadline is less than publisher deadline;");
    }
  }
  if (!liveliness_best_any &&
    pub.liveliness == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
    sub.liveliness == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC)
  {
    error("ERROR: Publisher's liveliness is automatic and subscription's is manual by topic;");
  }
  if (!lease_best_any && !is_infinite(sub.liveliness_lease_duration)) {
    if (is_infinite(pub.liveliness_lease_duration)) {
      error("ERROR: Subscription has a liveliness lease duration, but publisher does not;");
    } else if (longer(pub.liveliness_lease_duration, sub.liveliness_lease_duration)) {
      error("ERROR: Subscription liveliness lease duration is less than publisher;");
    }
  }
  if (*compatibility == RMW_QOS_COMPATIBILITY_ERROR) {
    return RMW_RET_OK;
  }

  // Warnings: a SYSTEM_DEFAULT (or UNKNOWN) policy may or may not be compatible,
  // including when both sides are unknown (as rmw_dds_common).
  auto warn = [&](const char * text) {
      *compatibility = RMW_QOS_COMPATIBILITY_WARNING;
      append(reason, reason_size, text);
    };
  const bool pub_rel_unknown = pub.reliability == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT ||
    pub.reliability == RMW_QOS_POLICY_RELIABILITY_UNKNOWN;
  const bool sub_rel_unknown = sub.reliability == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT ||
    sub.reliability == RMW_QOS_POLICY_RELIABILITY_UNKNOWN;
  const bool pub_dur_unknown = pub.durability == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT ||
    pub.durability == RMW_QOS_POLICY_DURABILITY_UNKNOWN;
  const bool sub_dur_unknown = sub.durability == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT ||
    sub.durability == RMW_QOS_POLICY_DURABILITY_UNKNOWN;
  const bool pub_live_unknown = pub.liveliness == RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT ||
    pub.liveliness == RMW_QOS_POLICY_LIVELINESS_UNKNOWN;
  const bool sub_live_unknown = sub.liveliness == RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT ||
    sub.liveliness == RMW_QOS_POLICY_LIVELINESS_UNKNOWN;

  if (pub_rel_unknown && sub_rel_unknown) {
    warn("WARNING: Publisher and subscription reliability are both unknown;");
  } else if (pub_rel_unknown && sub.reliability == RMW_QOS_POLICY_RELIABILITY_RELIABLE) {
    warn("WARNING: Publisher reliability is unknown and subscription is RELIABLE;");
  } else if (pub.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT && sub_rel_unknown) {
    warn("WARNING: Publisher reliability is BEST_EFFORT and subscription is unknown;");
  }
  if (pub_dur_unknown && sub_dur_unknown) {
    warn("WARNING: Publisher and subscription durability are both unknown;");
  } else if (pub_dur_unknown && sub.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL) {
    warn("WARNING: Publisher durability is unknown and subscription is TRANSIENT_LOCAL;");
  } else if (pub.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE && sub_dur_unknown) {
    warn("WARNING: Publisher durability is VOLATILE and subscription is unknown;");
  }
  if (pub_live_unknown && sub_live_unknown) {
    warn("WARNING: Publisher and subscription liveliness are both unknown;");
  } else if (pub_live_unknown && sub.liveliness == RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC) {
    warn("WARNING: Publisher liveliness is unknown and subscription is MANUAL_BY_TOPIC;");
  } else if (pub.liveliness == RMW_QOS_POLICY_LIVELINESS_AUTOMATIC && sub_live_unknown) {
    warn("WARNING: Publisher liveliness is AUTOMATIC and subscription is unknown;");
  }
  return RMW_RET_OK;
}

}  // namespace rmw_intraprocess_cpp

extern "C"
{
rmw_ret_t
rmw_qos_profile_check_compatible(
  const rmw_qos_profile_t publisher_profile,
  const rmw_qos_profile_t subscription_profile,
  rmw_qos_compatibility_type_t * compatibility,
  char * reason,
  size_t reason_size)
{
  return rmw_intraprocess_cpp::check_qos_compatible(
    publisher_profile, subscription_profile, compatibility, reason, reason_size);
}
}  // extern "C"
