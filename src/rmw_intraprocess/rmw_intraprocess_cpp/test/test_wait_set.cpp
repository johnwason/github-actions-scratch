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

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "endpoint.hpp"
#include "guard_condition.hpp"
#include "waitable.hpp"
#include "./rmw_fixture.hpp"

using namespace std::chrono_literals;
using rmw_intraprocess_cpp::GuardCondition;
using rmw_intraprocess_cpp::SubscriptionImpl;
using rmw_intraprocess_cpp::WaitSetImpl;
using Clock = std::chrono::steady_clock;

namespace
{
rmw_time_t ms(int64_t v)
{
  return rmw_time_t{static_cast<uint64_t>(v / 1000), static_cast<uint64_t>((v % 1000) * 1000000)};
}
}  // namespace

class WaitSet : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    ws = rmw_create_wait_set(&context, 0);
    ASSERT_NE(nullptr, ws);
    gc = rmw_create_guard_condition(&context);
    ASSERT_NE(nullptr, gc);
    node = rmw_create_node(&context, "ws", "/");
    ASSERT_NE(nullptr, node);
    const auto ts =
      rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::BasicTypes>();
    rmw_publisher_options_t po = rmw_get_default_publisher_options();
    rmw_subscription_options_t so = rmw_get_default_subscription_options();
    pub = rmw_create_publisher(node, ts, "/ws", &rmw_qos_profile_default, &po);
    sub = rmw_create_subscription(node, ts, "/ws", &rmw_qos_profile_default, &so);
    ASSERT_NE(nullptr, pub);
    ASSERT_NE(nullptr, sub);
  }
  void TearDown() override
  {
    EXPECT_EQ(0u, gc_impl()->waiter_count());
    EXPECT_EQ(0u, sub_impl()->waiter_count());
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, sub));
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_guard_condition(gc));
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(ws));
    RmwContextFixture::TearDown();
  }

  GuardCondition * gc_impl() {return static_cast<GuardCondition *>(gc->data);}
  SubscriptionImpl * sub_impl() {return static_cast<SubscriptionImpl *>(sub->data);}

  /// Waits on {sub, gc}; afterwards sub_ready / gc_ready tell what was reported.
  rmw_ret_t wait(const rmw_time_t * timeout)
  {
    subs_storage[0] = sub->data;
    gcs_storage[0] = gc->data;
    rmw_subscriptions_t subs{1, subs_storage};
    rmw_guard_conditions_t gcs{1, gcs_storage};
    rmw_ret_t ret = rmw_wait(&subs, &gcs, nullptr, nullptr, nullptr, ws, timeout);
    sub_ready = subs_storage[0] != nullptr;
    gc_ready = gcs_storage[0] != nullptr;
    return ret;
  }

  void publish()
  {
    test_msgs::msg::BasicTypes m;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));
  }

  rmw_wait_set_t * ws = nullptr;
  rmw_guard_condition_t * gc = nullptr;
  rmw_node_t * node = nullptr;
  rmw_publisher_t * pub = nullptr;
  rmw_subscription_t * sub = nullptr;
  void * subs_storage[1]{};
  void * gcs_storage[1]{};
  bool sub_ready = false;
  bool gc_ready = false;
};

TEST_F(WaitSet, ZeroTimeoutDoesNotBlock)
{
  const rmw_time_t zero{0, 0};
  const auto start = Clock::now();
  EXPECT_EQ(RMW_RET_TIMEOUT, wait(&zero));
  EXPECT_LT(Clock::now() - start, 100ms);
  EXPECT_FALSE(sub_ready);
  EXPECT_FALSE(gc_ready);
}

TEST_F(WaitSet, TimeoutIsHonored)
{
  const rmw_time_t t = ms(100);
  const auto start = Clock::now();
  EXPECT_EQ(RMW_RET_TIMEOUT, wait(&t));
  const auto elapsed = Clock::now() - start;
  EXPECT_GE(elapsed, 100ms);
  EXPECT_LT(elapsed, 5s);
}

TEST_F(WaitSet, ReadyBeforeWaitAndTriggerIsConsumed)
{
  ASSERT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
  publish();
  const rmw_time_t zero{0, 0};
  EXPECT_EQ(RMW_RET_OK, wait(&zero));
  EXPECT_TRUE(sub_ready);
  EXPECT_TRUE(gc_ready);
  // Guard condition consumed by the wait; the sample stays until taken.
  EXPECT_EQ(RMW_RET_OK, wait(&zero));
  EXPECT_TRUE(sub_ready);
  EXPECT_FALSE(gc_ready);
  test_msgs::msg::BasicTypes m;
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(sub, &m, &taken, nullptr));
  EXPECT_EQ(RMW_RET_TIMEOUT, wait(&zero));
}

TEST_F(WaitSet, GuardConditionWakesInfiniteWait)
{
  std::thread t([this] {
      std::this_thread::sleep_for(50ms);
      EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
    });
  EXPECT_EQ(RMW_RET_OK, wait(nullptr));
  t.join();
  EXPECT_TRUE(gc_ready);
  EXPECT_FALSE(sub_ready);
}

TEST_F(WaitSet, PublishWakesWait)
{
  std::thread t([this] {
      std::this_thread::sleep_for(50ms);
      publish();
    });
  const rmw_time_t t10 = ms(10000);
  const auto start = Clock::now();
  EXPECT_EQ(RMW_RET_OK, wait(&t10));
  EXPECT_LT(Clock::now() - start, 5s);
  t.join();
  EXPECT_TRUE(sub_ready);
}

TEST_F(WaitSet, SpuriousWakeupKeepsWaiting)
{
  // A wakeup with nothing ready (e.g. readiness stolen by another taker) must
  // not end the wait early.
  auto impl = static_cast<WaitSetImpl *>(ws->data);
  std::thread t([impl] {
      for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(10ms);
        impl->notify();
      }
    });
  const rmw_time_t t200 = ms(200);
  const auto start = Clock::now();
  EXPECT_EQ(RMW_RET_TIMEOUT, wait(&t200));
  EXPECT_GE(Clock::now() - start, 200ms);
  t.join();
}

TEST_F(WaitSet, StolenSampleKeepsWaiting)
{
  // Another thread takes the sample that woke us; the wait continues and then
  // returns for the next real event.
  std::thread t([this] {
      std::this_thread::sleep_for(30ms);
      publish();
      test_msgs::msg::BasicTypes m;
      bool taken = false;
      EXPECT_EQ(RMW_RET_OK, rmw_take(sub, &m, &taken, nullptr));
      std::this_thread::sleep_for(50ms);
      EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
    });
  EXPECT_EQ(RMW_RET_OK, wait(nullptr));
  t.join();
  // Contract: a successful wait reports something ready, even if the sample
  // that woke it was stolen. Usually the sample is gone before the waiter checks
  // it, so the wait continues until the guard condition (gc_ready). On a slow
  // or busy machine (seen on the Windows CI runner) the waiter may check before
  // the take and correctly report the subscription instead.
  EXPECT_TRUE(gc_ready || sub_ready);
  if (!gc_ready) {
    // The subscription was reported; the sample has since been stolen.
    test_msgs::msg::BasicTypes m;
    bool taken = true;
    EXPECT_EQ(RMW_RET_OK, rmw_take(sub, &m, &taken, nullptr));
    EXPECT_FALSE(taken);
  }
}

TEST_F(WaitSet, ManyWakeupsNoLostWakeup)
{
  // Ping-pong: the waiter must observe every trigger (each wait is woken by
  // exactly the trigger issued after the previous wait returned).
  std::atomic<int> round{0};
  constexpr int kRounds = 2000;
  std::thread t([&] {
      for (int i = 0; i < kRounds; ++i) {
        while (round.load() != i) {std::this_thread::yield();}
        EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
      }
    });
  const rmw_time_t t5 = ms(5000);
  for (int i = 0; i < kRounds; ++i) {
    round = i;
    ASSERT_EQ(RMW_RET_OK, wait(&t5)) << "round " << i;
    ASSERT_TRUE(gc_ready);
  }
  t.join();
}

TEST_F(WaitSet, UnusedArraysAreCleared)
{
  ASSERT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
  void * gcs_arr[1] = {gc->data};
  void * dummy[2] = {reinterpret_cast<void *>(0x1), reinterpret_cast<void *>(0x2)};
  void * dummy2[1] = {reinterpret_cast<void *>(0x3)};
  rmw_guard_conditions_t gcs{1, gcs_arr};
  rmw_services_t srvs{0, dummy};
  rmw_clients_t clis{0, dummy2};
  rmw_events_t evs{0, nullptr};
  const rmw_time_t zero{0, 0};
  EXPECT_EQ(RMW_RET_OK, rmw_wait(nullptr, &gcs, &srvs, &clis, &evs, ws, &zero));
  EXPECT_NE(nullptr, gcs_arr[0]);
}

TEST_F(WaitSet, WakeAfterContextShutdown)
{
  std::thread t([this] {
      std::this_thread::sleep_for(50ms);
      EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
      EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
    });
  EXPECT_EQ(RMW_RET_OK, wait(nullptr));
  t.join();
  EXPECT_TRUE(gc_ready);
}

TEST_F(WaitSet, BadArguments)
{
  const rmw_time_t zero{0, 0};
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_wait(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &zero));
  rmw_reset_error();
  const char * id = ws->implementation_identifier;
  ws->implementation_identifier = "not-this-rmw";
  EXPECT_EQ(
    RMW_RET_INCORRECT_RMW_IMPLEMENTATION,
    rmw_wait(nullptr, nullptr, nullptr, nullptr, nullptr, ws, &zero));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_destroy_wait_set(ws));
  rmw_reset_error();
  ws->implementation_identifier = id;
  EXPECT_EQ(nullptr, rmw_create_wait_set(nullptr, 0));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, rmw_destroy_wait_set(nullptr));
  rmw_reset_error();
}
