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

// Events, TRANSIENT_LOCAL, lifespan and KEEP_ALL limit behavior. This test
// runs with RMW_INTRAPROCESS_KEEP_ALL_LIMIT=50 (set in CMakeLists.txt).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/msg/strings.hpp"

#include "endpoint.hpp"
#include "./rmw_fixture.hpp"

using namespace std::chrono_literals;

namespace
{
const rosidl_message_type_support_t * basic()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::BasicTypes>();
}
const rosidl_message_type_support_t * strings()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::Strings>();
}

struct Counter
{
  std::atomic<std::size_t> calls{0};
  std::atomic<std::size_t> events{0};
};
void count_cb(const void * user_data, size_t n)
{
  auto c = static_cast<Counter *>(const_cast<void *>(user_data));
  ++c->calls;
  c->events += n;
}
}  // namespace

class Events : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    node = rmw_create_node(&context, "events", "/");
    ASSERT_NE(nullptr, node);
  }
  void TearDown() override
  {
    for (auto & e : events) {
      EXPECT_EQ(RMW_RET_OK, rmw_event_fini(&e));
    }
    for (auto p : pubs) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, p));
    }
    for (auto s : subs) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s));
    }
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    RmwContextFixture::TearDown();
  }
  rmw_publisher_t * pub(
    const char * topic, rmw_qos_profile_t qos = rmw_qos_profile_default,
    const rosidl_message_type_support_t * ts = basic())
  {
    rmw_publisher_options_t o = rmw_get_default_publisher_options();
    rmw_publisher_t * p = rmw_create_publisher(node, ts, topic, &qos, &o);
    EXPECT_NE(nullptr, p) << rmw_get_error_string().str;
    pubs.push_back(p);
    return p;
  }
  rmw_subscription_t * sub(
    const char * topic, rmw_qos_profile_t qos = rmw_qos_profile_default,
    const rosidl_message_type_support_t * ts = basic())
  {
    rmw_subscription_options_t o = rmw_get_default_subscription_options();
    rmw_subscription_t * s = rmw_create_subscription(node, ts, topic, &qos, &o);
    EXPECT_NE(nullptr, s) << rmw_get_error_string().str;
    subs.push_back(s);
    return s;
  }
  rmw_event_t * pub_event(rmw_publisher_t * p, rmw_event_type_t type)
  {
    events.reserve(16);
    events.push_back(rmw_get_zero_initialized_event());
    EXPECT_EQ(RMW_RET_OK, rmw_publisher_event_init(&events.back(), p, type));
    return &events.back();
  }
  rmw_event_t * sub_event(rmw_subscription_t * s, rmw_event_type_t type)
  {
    events.reserve(16);
    events.push_back(rmw_get_zero_initialized_event());
    EXPECT_EQ(RMW_RET_OK, rmw_subscription_event_init(&events.back(), s, type));
    return &events.back();
  }
  void publish(rmw_publisher_t * p, int64_t value)
  {
    test_msgs::msg::BasicTypes m;
    m.int64_value = value;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  }
  std::vector<int64_t> take_all(rmw_subscription_t * s)
  {
    std::vector<int64_t> out;
    test_msgs::msg::BasicTypes m;
    bool taken = false;
    while (rmw_take(s, &m, &taken, nullptr) == RMW_RET_OK && taken) {
      out.push_back(m.int64_value);
    }
    return out;
  }
  rmw_node_t * node = nullptr;
  std::vector<rmw_publisher_t *> pubs;
  std::vector<rmw_subscription_t *> subs;
  std::vector<rmw_event_t> events;
};

TEST_F(Events, MatchedEventsBothSides)
{
  auto p = pub("/matched");
  rmw_event_t * pe = pub_event(p, RMW_EVENT_PUBLICATION_MATCHED);
  Counter pc;
  ASSERT_EQ(RMW_RET_OK, rmw_event_set_callback(pe, count_cb, &pc));
  auto s = sub("/matched");
  rmw_event_t * se = sub_event(s, RMW_EVENT_SUBSCRIPTION_MATCHED);
  EXPECT_EQ(1u, pc.calls.load());

  rmw_matched_status_t st{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  EXPECT_TRUE(taken);
  EXPECT_EQ(1u, st.total_count);
  EXPECT_EQ(1u, st.total_count_change);
  EXPECT_EQ(1u, st.current_count);
  EXPECT_EQ(1, st.current_count_change);
  // The subscription saw the match at creation (before its event was inited).
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se, &st, &taken));
  EXPECT_EQ(1u, st.current_count);
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se, &st, &taken));
  EXPECT_EQ(0u, st.total_count_change);
  EXPECT_EQ(0, st.current_count_change);

  ASSERT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s));
  subs.clear();
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  EXPECT_EQ(1u, st.total_count);
  EXPECT_EQ(0u, st.current_count);
  EXPECT_EQ(-1, st.current_count_change);
  events.pop_back();  // se belonged to the destroyed subscription
}

TEST_F(Events, EventWakesWaitSetAndCallbackReportsUnread)
{
  auto p = pub("/wake");
  rmw_event_t * pe = pub_event(p, RMW_EVENT_PUBLICATION_MATCHED);
  rmw_wait_set_t * ws = rmw_create_wait_set(&context, 0);
  ASSERT_NE(nullptr, ws);
  std::thread t([this] {
      std::this_thread::sleep_for(30ms);
      sub("/wake");
    });
  void * ev[1] = {pe};
  rmw_events_t evs{1, ev};
  const rmw_time_t timeout{5, 0};
  EXPECT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, nullptr, nullptr, &evs, ws, &timeout));
  t.join();
  EXPECT_EQ(pe, ev[0]);
  Counter c;
  ASSERT_EQ(RMW_RET_OK, rmw_event_set_callback(pe, count_cb, &c));
  EXPECT_EQ(1u, c.calls.load());
  EXPECT_EQ(1u, c.events.load());
  rmw_matched_status_t st{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  ev[0] = pe;
  const rmw_time_t zero{0, 0};
  EXPECT_EQ(RMW_RET_TIMEOUT, rmw_wait(nullptr, nullptr, nullptr, nullptr, &evs, ws, &zero));
  EXPECT_EQ(nullptr, ev[0]);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(ws));
}

TEST_F(Events, IncompatibleQosReportsPolicy)
{
  rmw_qos_profile_t be = rmw_qos_profile_default;
  be.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  auto p = pub("/incompat", be);
  rmw_event_t * pe = pub_event(p, RMW_EVENT_OFFERED_QOS_INCOMPATIBLE);
  auto s = sub("/incompat");  // RELIABLE
  rmw_event_t * se = sub_event(s, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE);
  rmw_qos_incompatible_event_status_t st{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  EXPECT_EQ(1, st.total_count);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY, st.last_policy_kind);
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se, &st, &taken));
  EXPECT_EQ(1, st.total_count);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY, st.last_policy_kind);

  rmw_qos_profile_t tl = rmw_qos_profile_default;
  tl.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  sub("/incompat", tl);  // durability mismatch with the VOLATILE publisher too
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  EXPECT_EQ(2, st.total_count);
  EXPECT_EQ(1, st.total_count_change);

  rmw_qos_profile_t dl = rmw_qos_profile_default;
  dl.deadline = rmw_time_t{1, 0};
  auto s2 = sub("/deadline", dl);
  rmw_event_t * se2 = sub_event(s2, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE);
  pub("/deadline");
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se2, &st, &taken));
  EXPECT_EQ(RMW_QOS_POLICY_DEADLINE, st.last_policy_kind);

  // A lease-duration mismatch is reported as LIVELINESS, as the DDS RMWs do
  // (rcl test_events, IncompatibleQoS_LivelinessLeaseDuration).
  rmw_qos_profile_t lease = rmw_qos_profile_default;
  lease.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  lease.liveliness_lease_duration = rmw_time_t{1, 0};
  auto s3 = sub("/lease", lease);
  rmw_event_t * se3 = sub_event(s3, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE);
  rmw_qos_profile_t long_lease = lease;
  long_lease.liveliness_lease_duration = rmw_time_t{5, 0};
  pub("/lease", long_lease);
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se3, &st, &taken));
  EXPECT_EQ(1, st.total_count);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS, st.last_policy_kind);
}

TEST_F(Events, IncompatibleTypeBothSides)
{
  auto p = pub("/typed", rmw_qos_profile_default, basic());
  rmw_event_t * pe = pub_event(p, RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE);
  auto s = sub("/typed", rmw_qos_profile_default, strings());
  rmw_event_t * se = sub_event(s, RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE);
  rmw_incompatible_type_status_t st{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(pe, &st, &taken));
  EXPECT_EQ(1, st.total_count);
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se, &st, &taken));
  EXPECT_EQ(1, st.total_count);
  size_t n = 9;
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &n));
  EXPECT_EQ(0u, n);
}

TEST_F(Events, UnsupportedEventTypes)
{
  auto p = pub("/unsupported");
  auto s = sub("/unsupported");
  rmw_event_t e = rmw_get_zero_initialized_event();
  EXPECT_EQ(RMW_RET_UNSUPPORTED, rmw_publisher_event_init(&e, p, RMW_EVENT_LIVELINESS_LOST));
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED, rmw_publisher_event_init(&e, p, RMW_EVENT_OFFERED_DEADLINE_MISSED));
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED, rmw_subscription_event_init(&e, s, RMW_EVENT_LIVELINESS_CHANGED));
  rmw_reset_error();
  // A subscription-only type on a publisher.
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_publisher_event_init(&e, p, RMW_EVENT_MESSAGE_LOST));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_assert_liveliness(p));
}

TEST_F(Events, KeepAllLimitRejectsAndReportsMessageLost)
{
  ASSERT_EQ(50u, rmw_intraprocess_cpp::keep_all_limit()) << "test expects the CMake ENV";
  rmw_qos_profile_t all = rmw_qos_profile_default;
  all.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  auto p = pub("/lost", all);
  auto s = sub("/lost", all);
  rmw_event_t * se = sub_event(s, RMW_EVENT_MESSAGE_LOST);
  Counter c;
  ASSERT_EQ(RMW_RET_OK, rmw_event_set_callback(se, count_cb, &c));
  for (int i = 0; i < 60; ++i) {
    publish(p, i);
  }
  EXPECT_EQ(10u, c.calls.load());
  rmw_message_lost_status_t st{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_event(se, &st, &taken));
  EXPECT_EQ(10u, st.total_count);
  const auto got = take_all(s);
  ASSERT_EQ(50u, got.size());  // the oldest 50 are kept (newer ones rejected)
  EXPECT_EQ(0, got.front());
  EXPECT_EQ(49, got.back());
}

TEST_F(Events, TransientLocalLateJoiner)
{
  rmw_qos_profile_t tl = rmw_qos_profile_default;
  tl.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  tl.depth = 3;
  auto p = pub("/latched", tl);
  for (int i = 1; i <= 5; ++i) {
    publish(p, i);
  }
  auto late_tl = sub("/latched", tl);
  auto late_volatile = sub("/latched");
  EXPECT_EQ((std::vector<int64_t>{3, 4, 5}), take_all(late_tl));
  EXPECT_TRUE(take_all(late_volatile).empty());
  // Subscription depth limits history delivery too.
  rmw_qos_profile_t tl1 = tl;
  tl1.depth = 1;
  auto late_depth1 = sub("/latched", tl1);
  EXPECT_EQ((std::vector<int64_t>{5}), take_all(late_depth1));
  // A VOLATILE publisher cannot serve a TRANSIENT_LOCAL subscription at all.
  auto vp = pub("/latched_volatile");
  publish(vp, 1);
  auto tl_sub = sub("/latched_volatile", tl);
  size_t n = 9;
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_count_matched_publishers(tl_sub, &n));
  EXPECT_EQ(0u, n);
}

TEST_F(Events, TransientLocalJoinWhilePublishingHasNoGapsOrDuplicates)
{
  rmw_qos_profile_t tl = rmw_qos_profile_default;
  tl.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  tl.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  tl.depth = 40;
  auto p = pub("/tl_race", tl);
  std::atomic<bool> stop{false};
  std::thread publisher([&] {
      test_msgs::msg::BasicTypes m;
      for (int64_t i = 0; !stop; ++i) {
        m.int64_value = i;
        EXPECT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
        if (i % 16 == 0) {
          std::this_thread::yield();
        }
      }
    });
  rmw_qos_profile_t sub_qos = tl;
  sub_qos.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  for (int round = 0; round < 20; ++round) {
    std::this_thread::sleep_for(1ms);
    auto s = sub("/tl_race", sub_qos);
    std::this_thread::sleep_for(1ms);
    const auto got = take_all(s);
    // History (40) and live samples meet within the first KEEP_ALL-limit (50)
    // entries; past the limit newer samples are rejected (MESSAGE_LOST), which
    // legitimately leaves a gap.
    ASSERT_GE(got.size(), 40u);
    const std::size_t checked = std::min(got.size(), rmw_intraprocess_cpp::keep_all_limit());
    for (std::size_t i = 1; i < checked; ++i) {
      ASSERT_EQ(got[i - 1] + 1, got[i]) << "round " << round << " index " << i;
    }
    ASSERT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s));
    subs.pop_back();
  }
  stop = true;
  publisher.join();
}

TEST_F(Events, LifespanExpiresSamples)
{
  rmw_qos_profile_t q = rmw_qos_profile_default;
  q.lifespan = rmw_time_t{0, 100000000};  // 100 ms
  q.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  auto p = pub("/lifespan", q);
  auto s = sub("/lifespan", q);
  publish(p, 1);
  std::this_thread::sleep_for(200ms);
  publish(p, 2);
  EXPECT_EQ((std::vector<int64_t>{2}), take_all(s));
  std::this_thread::sleep_for(200ms);
  // Expired history is not delivered to late joiners, and an expired sample
  // does not make a wait set ready.
  auto late = sub("/lifespan", q);
  rmw_wait_set_t * ws = rmw_create_wait_set(&context, 0);
  void * arr[1] = {late->data};
  rmw_subscriptions_t subs_arr{1, arr};
  const rmw_time_t zero{0, 0};
  EXPECT_EQ(RMW_RET_TIMEOUT, rmw_wait(&subs_arr, nullptr, nullptr, nullptr, nullptr, ws, &zero));
  EXPECT_TRUE(take_all(late).empty());
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(ws));
}
