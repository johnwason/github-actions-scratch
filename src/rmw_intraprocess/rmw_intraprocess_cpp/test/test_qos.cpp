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

// Table-driven QoS compatibility matrix and QoS resolution.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"

#include "qos.hpp"

using rmw_intraprocess_cpp::EndpointKind;
using rmw_intraprocess_cpp::resolve_qos;

namespace
{
constexpr auto OK = RMW_QOS_COMPATIBILITY_OK;
constexpr auto WARN = RMW_QOS_COMPATIBILITY_WARNING;
constexpr auto ERR = RMW_QOS_COMPATIBILITY_ERROR;

/// A profile with every policy explicit (no SYSTEM_DEFAULT), so each table row
/// only exercises the policy it changes.
rmw_qos_profile_t base()
{
  rmw_qos_profile_t q = rmw_qos_profile_default;
  q.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  return q;
}

rmw_qos_compatibility_type_t check(const rmw_qos_profile_t & pub, const rmw_qos_profile_t & sub)
{
  rmw_qos_compatibility_type_t c = OK;
  char reason[256];
  EXPECT_EQ(RMW_RET_OK, rmw_qos_profile_check_compatible(pub, sub, &c, reason, sizeof(reason)));
  if (c == OK) {
    EXPECT_STREQ("", reason);
  } else {
    EXPECT_NE(0u, std::strlen(reason));
  }
  return c;
}

struct ReliabilityRow
{
  rmw_qos_reliability_policy_t pub, sub;
  rmw_qos_compatibility_type_t expected;
};
struct DurabilityRow
{
  rmw_qos_durability_policy_t pub, sub;
  rmw_qos_compatibility_type_t expected;
};
struct LivelinessRow
{
  rmw_qos_liveliness_policy_t pub, sub;
  rmw_qos_compatibility_type_t expected;
};
struct DurationRow
{
  rmw_time_t pub, sub;
  rmw_qos_compatibility_type_t expected;
};

const rmw_time_t kDefault = RMW_DURATION_UNSPECIFIED;
const rmw_time_t kInfinite = RMW_DURATION_INFINITE;
const rmw_time_t k1s{1, 0};
const rmw_time_t k2s{2, 0};
}  // namespace

TEST(QosMatrix, Reliability)
{
  constexpr auto R = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  constexpr auto B = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  constexpr auto S = RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT;
  constexpr auto A = RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE;
  const ReliabilityRow rows[] = {
    {R, R, OK}, {R, B, OK}, {B, B, OK}, {B, R, ERR},
    {S, R, WARN}, {S, B, OK}, {R, S, OK}, {B, S, WARN}, {S, S, WARN},
    {A, R, OK}, {A, B, OK}, {R, A, OK}, {B, A, OK}, {A, A, OK},
  };
  for (const auto & row : rows) {
    rmw_qos_profile_t pub = base(), sub = base();
    pub.reliability = row.pub;
    sub.reliability = row.sub;
    EXPECT_EQ(row.expected, check(pub, sub)) << "pub " << row.pub << " sub " << row.sub;
  }
}

TEST(QosMatrix, Durability)
{
  constexpr auto V = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  constexpr auto T = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  constexpr auto S = RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT;
  constexpr auto A = RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE;
  const DurabilityRow rows[] = {
    {V, V, OK}, {T, T, OK}, {T, V, OK}, {V, T, ERR},
    {S, T, WARN}, {S, V, OK}, {T, S, OK}, {V, S, WARN}, {S, S, WARN},
    {A, T, OK}, {V, A, OK}, {A, A, OK},
  };
  for (const auto & row : rows) {
    rmw_qos_profile_t pub = base(), sub = base();
    pub.durability = row.pub;
    sub.durability = row.sub;
    EXPECT_EQ(row.expected, check(pub, sub)) << "pub " << row.pub << " sub " << row.sub;
  }
}

TEST(QosMatrix, Liveliness)
{
  constexpr auto AU = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  constexpr auto MT = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  constexpr auto S = RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT;
  constexpr auto BA = RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE;
  const LivelinessRow rows[] = {
    {AU, AU, OK}, {MT, MT, OK}, {MT, AU, OK}, {AU, MT, ERR},
    {S, MT, WARN}, {S, AU, OK}, {AU, S, WARN}, {MT, S, OK}, {S, S, WARN},
    {BA, MT, OK}, {AU, BA, OK},
  };
  for (const auto & row : rows) {
    rmw_qos_profile_t pub = base(), sub = base();
    pub.liveliness = row.pub;
    sub.liveliness = row.sub;
    EXPECT_EQ(row.expected, check(pub, sub)) << "pub " << row.pub << " sub " << row.sub;
  }
}

TEST(QosMatrix, DeadlineAndLeaseDuration)
{
  const rmw_time_t best_deadline = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  const rmw_time_t best_lease = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  const DurationRow rows[] = {
    {kDefault, kDefault, OK}, {kInfinite, kDefault, OK}, {k1s, kDefault, OK},
    {k1s, k1s, OK}, {k1s, k2s, OK}, {k2s, k1s, ERR},
    {kDefault, k1s, ERR}, {kInfinite, k1s, ERR},
  };
  for (const auto & row : rows) {
    rmw_qos_profile_t pub = base(), sub = base();
    pub.deadline = row.pub;
    sub.deadline = row.sub;
    EXPECT_EQ(row.expected, check(pub, sub)) << "deadline " << row.pub.sec << "/" << row.sub.sec;
    pub = base();
    sub = base();
    pub.liveliness_lease_duration = row.pub;
    sub.liveliness_lease_duration = row.sub;
    EXPECT_EQ(row.expected, check(pub, sub)) << "lease " << row.pub.sec << "/" << row.sub.sec;
  }
  rmw_qos_profile_t pub = base(), sub = base();
  sub.deadline = k1s;
  pub.deadline = best_deadline;
  EXPECT_EQ(OK, check(pub, sub));
  pub = base();
  sub = base();
  sub.liveliness_lease_duration = k1s;
  pub.liveliness_lease_duration = best_lease;
  EXPECT_EQ(OK, check(pub, sub));
}

TEST(QosMatrix, ErrorsWinOverWarningsAndAccumulate)
{
  rmw_qos_profile_t pub = base(), sub = base();
  pub.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  sub.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  pub.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  sub.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  pub.liveliness = RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT;
  sub.liveliness = RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT;
  rmw_qos_compatibility_type_t c = OK;
  char reason[512];
  ASSERT_EQ(RMW_RET_OK, rmw_qos_profile_check_compatible(pub, sub, &c, reason, sizeof(reason)));
  EXPECT_EQ(ERR, c);
  const std::string r(reason);
  EXPECT_NE(std::string::npos, r.find("Best effort"));
  EXPECT_NE(std::string::npos, r.find("Volatile"));
  EXPECT_EQ(std::string::npos, r.find("WARNING"));
}

TEST(QosMatrix, ReasonBufferHandling)
{
  rmw_qos_profile_t pub = base(), sub = base();
  pub.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  rmw_qos_compatibility_type_t c = OK;
  char small[6];
  std::memset(small, 'x', sizeof(small));
  ASSERT_EQ(RMW_RET_OK, rmw_qos_profile_check_compatible(pub, sub, &c, small, sizeof(small)));
  EXPECT_EQ(ERR, c);
  EXPECT_EQ(5u, std::strlen(small));  // truncated and terminated
  ASSERT_EQ(RMW_RET_OK, rmw_qos_profile_check_compatible(pub, sub, &c, nullptr, 0u));
  EXPECT_EQ(ERR, c);
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_qos_profile_check_compatible(pub, sub, &c, nullptr, 1u));
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT, rmw_qos_profile_check_compatible(pub, sub, nullptr, nullptr, 0u));
  rmw_reset_error();
}

TEST(QosResolve, SystemDefaultsAndBestAvailable)
{
  rmw_qos_profile_t q = rmw_qos_profile_system_default;
  q.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE;
  q.durability = RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE;
  q.deadline = RMW_QOS_DEADLINE_BEST_AVAILABLE;
  q.liveliness_lease_duration = RMW_QOS_LIVELINESS_LEASE_DURATION_BEST_AVAILABLE;
  const rmw_qos_profile_t p = resolve_qos(q, EndpointKind::kPublisher);
  const rmw_qos_profile_t s = resolve_qos(q, EndpointKind::kSubscription);
  EXPECT_EQ(RMW_QOS_POLICY_HISTORY_KEEP_LAST, p.history);
  EXPECT_EQ(1u, p.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, p.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL, p.durability);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, s.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, s.durability);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, p.liveliness);
  // Default/best-available durations are reported as infinite (as DDS does;
  // rcl test_get_actual_qos).
  const rmw_time_t inf = RMW_DURATION_INFINITE;
  for (const rmw_qos_profile_t * r : {&p, &s}) {
    EXPECT_EQ(inf.sec, r->deadline.sec);
    EXPECT_EQ(inf.nsec, r->deadline.nsec);
    EXPECT_EQ(inf.sec, r->lifespan.sec);
    EXPECT_EQ(inf.sec, r->liveliness_lease_duration.sec);
    EXPECT_EQ(inf.nsec, r->liveliness_lease_duration.nsec);
  }
  // Resolved profiles of any BEST_AVAILABLE pair are compatible.
  rmw_qos_compatibility_type_t c = ERR;
  ASSERT_EQ(RMW_RET_OK, rmw_qos_profile_check_compatible(p, s, &c, nullptr, 0u));
  EXPECT_EQ(OK, c);
}

TEST(QosValidate, UnknownPoliciesAreRejected)
{
  EXPECT_FALSE(rmw_intraprocess_cpp::validate_qos(rmw_qos_profile_unknown));
  rmw_reset_error();
  EXPECT_TRUE(rmw_intraprocess_cpp::validate_qos(rmw_qos_profile_default));
  EXPECT_TRUE(rmw_intraprocess_cpp::validate_qos(rmw_qos_profile_system_default));
}
