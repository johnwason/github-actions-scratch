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
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "rcutils/allocator.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "test_msgs/msg/basic_types.h"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/msg/strings.h"
#include "test_msgs/msg/strings.hpp"
#include "test_msgs/msg/unbounded_sequences.hpp"

#include "./rmw_fixture.hpp"

namespace
{
const rosidl_message_type_support_t * basic_cpp()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::BasicTypes>();
}
const rosidl_message_type_support_t * basic_c()
{
  return ROSIDL_GET_MSG_TYPE_SUPPORT(test_msgs, msg, BasicTypes);
}
const rosidl_message_type_support_t * strings_cpp()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::Strings>();
}
const rosidl_message_type_support_t * strings_c()
{
  return ROSIDL_GET_MSG_TYPE_SUPPORT(test_msgs, msg, Strings);
}
}  // namespace

class PubSub : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    node = rmw_create_node(&context, "pubsub", "/");
    ASSERT_NE(nullptr, node);
  }
  void TearDown() override
  {
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
    const rosidl_message_type_support_t * ts, const char * topic,
    rmw_qos_profile_t qos = rmw_qos_profile_default, rmw_node_t * on = nullptr)
  {
    rmw_publisher_options_t o = rmw_get_default_publisher_options();
    rmw_publisher_t * p = rmw_create_publisher(on ? on : node, ts, topic, &qos, &o);
    EXPECT_NE(nullptr, p) << rmw_get_error_string().str;
    if (on == nullptr) {
      pubs.push_back(p);
    }
    return p;
  }
  rmw_subscription_t * sub(
    const rosidl_message_type_support_t * ts, const char * topic,
    rmw_qos_profile_t qos = rmw_qos_profile_default, bool ignore_local = false)
  {
    rmw_subscription_options_t o = rmw_get_default_subscription_options();
    o.ignore_local_publications = ignore_local;
    rmw_subscription_t * s = rmw_create_subscription(node, ts, topic, &qos, &o);
    EXPECT_NE(nullptr, s) << rmw_get_error_string().str;
    subs.push_back(s);
    return s;
  }
  rmw_node_t * node = nullptr;
  std::vector<rmw_publisher_t *> pubs;
  std::vector<rmw_subscription_t *> subs;
};

TEST_F(PubSub, PublishTakeCpp)
{
  auto p = pub(basic_cpp(), "/basic");
  auto s = sub(basic_cpp(), "/basic");
  test_msgs::msg::BasicTypes out, in;
  out.bool_value = true;
  out.int32_value = -42;
  out.uint64_value = 1234567890123ull;
  out.float64_value = 3.25;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &out, nullptr));
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(s, &in, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(out, in);
  ASSERT_EQ(RMW_RET_OK, rmw_take(s, &in, &taken, nullptr));
  EXPECT_FALSE(taken);
}

TEST_F(PubSub, CPublisherToCppSubscriptionAndBack)
{
  auto pc = pub(strings_c(), "/strings");
  auto pcpp = pub(strings_cpp(), "/strings");
  auto scpp = sub(strings_cpp(), "/strings");
  auto sc = sub(strings_c(), "/strings");

  test_msgs__msg__Strings cmsg;
  ASSERT_TRUE(test_msgs__msg__Strings__init(&cmsg));
  ASSERT_TRUE(rosidl_runtime_c__String__assign(&cmsg.string_value, "from C"));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(pc, &cmsg, nullptr));
  test_msgs::msg::Strings cppmsg;
  cppmsg.string_value = "from C++";
  ASSERT_EQ(RMW_RET_OK, rmw_publish(pcpp, &cppmsg, nullptr));

  bool taken = false;
  test_msgs::msg::Strings got_cpp;
  ASSERT_EQ(RMW_RET_OK, rmw_take(scpp, &got_cpp, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("from C", got_cpp.string_value);
  ASSERT_EQ(RMW_RET_OK, rmw_take(scpp, &got_cpp, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ("from C++", got_cpp.string_value);

  test_msgs__msg__Strings got_c;
  ASSERT_TRUE(test_msgs__msg__Strings__init(&got_c));
  ASSERT_EQ(RMW_RET_OK, rmw_take(sc, &got_c, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_STREQ("from C", got_c.string_value.data);
  ASSERT_EQ(RMW_RET_OK, rmw_take(sc, &got_c, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_STREQ("from C++", got_c.string_value.data);
  test_msgs__msg__Strings__fini(&got_c);
  test_msgs__msg__Strings__fini(&cmsg);
}

TEST_F(PubSub, MessageInfo)
{
  auto p = pub(basic_cpp(), "/info");
  auto s = sub(basic_cpp(), "/info");
  test_msgs::msg::BasicTypes m;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  rmw_gid_t gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(p, &gid));
  for (std::uint64_t i = 1; i <= 2; ++i) {
    rmw_message_info_t info = rmw_get_zero_initialized_message_info();
    bool taken = false;
    ASSERT_EQ(RMW_RET_OK, rmw_take_with_info(s, &m, &taken, &info, nullptr));
    ASSERT_TRUE(taken);
    EXPECT_EQ(i, info.publication_sequence_number);
    EXPECT_EQ(i, info.reception_sequence_number);
    EXPECT_GT(info.source_timestamp, 0);
    EXPECT_GE(info.received_timestamp, info.source_timestamp);
    EXPECT_FALSE(info.from_intra_process);
    bool equal = false;
    EXPECT_EQ(RMW_RET_OK, rmw_compare_gids_equal(&gid, &info.publisher_gid, &equal));
    EXPECT_TRUE(equal);
  }
}

TEST_F(PubSub, KeepLastDropsOldestKeepAllKeepsEverything)
{
  rmw_qos_profile_t last = rmw_qos_profile_default;
  last.depth = 3;
  rmw_qos_profile_t all = rmw_qos_profile_default;
  all.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  auto p = pub(basic_cpp(), "/hist");
  auto s_last = sub(basic_cpp(), "/hist", last);
  auto s_all = sub(basic_cpp(), "/hist", all);
  test_msgs::msg::BasicTypes m;
  for (int i = 0; i < 100; ++i) {
    m.int32_value = i;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  }
  bool taken = false;
  for (int i = 97; i < 100; ++i) {
    ASSERT_EQ(RMW_RET_OK, rmw_take(s_last, &m, &taken, nullptr));
    ASSERT_TRUE(taken);
    EXPECT_EQ(i, m.int32_value);
  }
  ASSERT_EQ(RMW_RET_OK, rmw_take(s_last, &m, &taken, nullptr));
  EXPECT_FALSE(taken);
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(RMW_RET_OK, rmw_take(s_all, &m, &taken, nullptr));
    ASSERT_TRUE(taken);
    EXPECT_EQ(i, m.int32_value);
  }
}

TEST_F(PubSub, IgnoreLocalPublicationsIsPerContext)
{
  auto s_ignore = sub(basic_cpp(), "/local", rmw_qos_profile_default, true);
  auto s_all = sub(basic_cpp(), "/local");
  auto p_local = pub(basic_cpp(), "/local");

  rmw_context_t other = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &other));
  rmw_node_t * other_node = rmw_create_node(&other, "other", "/");
  ASSERT_NE(nullptr, other_node);
  rmw_publisher_t * p_remote = pub(basic_cpp(), "/local", rmw_qos_profile_default, other_node);

  test_msgs::msg::BasicTypes m;
  m.int32_value = 1;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p_local, &m, nullptr));
  m.int32_value = 2;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p_remote, &m, nullptr));

  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(s_ignore, &m, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(2, m.int32_value);  // only the other context's message
  ASSERT_EQ(RMW_RET_OK, rmw_take(s_ignore, &m, &taken, nullptr));
  EXPECT_FALSE(taken);
  int n = 0;
  while (rmw_take(s_all, &m, &taken, nullptr) == RMW_RET_OK && taken) {
    ++n;
  }
  EXPECT_EQ(2, n);
  // Matching itself ignores ignore_local_publications.
  size_t count = 0;
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_count_matched_publishers(s_ignore, &count));
  EXPECT_EQ(2u, count);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(other_node, p_remote));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(other_node));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&other));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&other));
}

TEST_F(PubSub, DifferentTypesOnOneTopicDoNotMatch)
{
  auto p = pub(basic_cpp(), "/mixed");
  auto s = sub(strings_cpp(), "/mixed");
  size_t count = 99;
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &count));
  EXPECT_EQ(0u, count);
  test_msgs::msg::BasicTypes m;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  test_msgs::msg::Strings in;
  bool taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take(s, &in, &taken, nullptr));
  EXPECT_FALSE(taken);
}

TEST_F(PubSub, MatchedCountsFollowCreateAndDestroy)
{
  auto p = pub(basic_cpp(), "/count");
  size_t count = 99;
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &count));
  EXPECT_EQ(0u, count);
  rmw_subscription_options_t o = rmw_get_default_subscription_options();
  rmw_subscription_t * s1 =
    rmw_create_subscription(node, basic_c(), "/count", &rmw_qos_profile_default, &o);
  rmw_subscription_t * s2 =
    rmw_create_subscription(node, basic_cpp(), "/count", &rmw_qos_profile_sensor_data, &o);
  ASSERT_NE(nullptr, s1);
  ASSERT_NE(nullptr, s2);
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &count));
  EXPECT_EQ(2u, count);
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_count_matched_publishers(s1, &count));
  EXPECT_EQ(1u, count);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s1));
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &count));
  EXPECT_EQ(1u, count);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s2));
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_count_matched_subscriptions(p, &count));
  EXPECT_EQ(0u, count);
}

TEST_F(PubSub, SerializedPaths)
{
  auto p = pub(basic_cpp(), "/ser");
  auto s = sub(basic_c(), "/ser");
  test_msgs::msg::BasicTypes m;
  m.int16_value = -7;
  rmw_serialized_message_t ser = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RMW_RET_OK, rmw_serialized_message_init(&ser, 0, &context.options.allocator));
  ASSERT_EQ(RMW_RET_OK, rmw_serialize(&m, basic_cpp(), &ser));
  EXPECT_GT(ser.buffer_length, 4u);

  // Serialized publish -> typed (C) take.
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(p, &ser, nullptr));
  test_msgs__msg__BasicTypes cmsg;
  ASSERT_TRUE(test_msgs__msg__BasicTypes__init(&cmsg));
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take(s, &cmsg, &taken, nullptr));
  ASSERT_TRUE(taken);
  EXPECT_EQ(-7, cmsg.int16_value);

  // Typed publish -> serialized take is byte-identical to rmw_serialize.
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  rmw_serialized_message_t got = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RMW_RET_OK, rmw_serialized_message_init(&got, 0, &context.options.allocator));
  rmw_message_info_t info = rmw_get_zero_initialized_message_info();
  ASSERT_EQ(RMW_RET_OK, rmw_take_serialized_message_with_info(s, &got, &taken, &info, nullptr));
  ASSERT_TRUE(taken);
  ASSERT_EQ(ser.buffer_length, got.buffer_length);
  EXPECT_EQ(0, std::memcmp(ser.buffer, got.buffer, ser.buffer_length));
  EXPECT_EQ(2u, info.publication_sequence_number);

  // Malformed CDR is published as-is but fails cleanly on take.
  rmw_serialized_message_t bad = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RMW_RET_OK, rmw_serialized_message_init(&bad, 2, &context.options.allocator));
  bad.buffer_length = 2;
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(p, &bad, nullptr));
  EXPECT_EQ(RMW_RET_ERROR, rmw_take(s, &cmsg, &taken, nullptr));
  EXPECT_FALSE(taken);
  rmw_reset_error();

  test_msgs__msg__BasicTypes__fini(&cmsg);
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&bad));
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&got));
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&ser));
}

TEST_F(PubSub, TakeSequence)
{
  auto p = pub(basic_cpp(), "/seq");
  auto s = sub(basic_cpp(), "/seq");
  test_msgs::msg::BasicTypes m;
  for (int i = 0; i < 3; ++i) {
    m.int32_value = i;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  }
  test_msgs::msg::BasicTypes out[5];
  void * ptrs[5];
  for (int i = 0; i < 5; ++i) {
    ptrs[i] = &out[i];
  }
  rmw_message_sequence_t seq = rmw_get_zero_initialized_message_sequence();
  seq.data = ptrs;
  seq.capacity = 5;
  rmw_message_info_sequence_t infos = rmw_get_zero_initialized_message_info_sequence();
  ASSERT_EQ(
    RMW_RET_OK, rmw_message_info_sequence_init(&infos, 5, &context.options.allocator));
  size_t taken = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_take_sequence(s, 5, &seq, &infos, &taken, nullptr));
  EXPECT_EQ(3u, taken);
  EXPECT_EQ(3u, seq.size);
  EXPECT_EQ(3u, infos.size);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(i, out[i].int32_value);
    EXPECT_EQ(static_cast<std::uint64_t>(i + 1), infos.data[i].publication_sequence_number);
  }
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_take_sequence(s, 6, &seq, &infos, &taken, nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_message_info_sequence_fini(&infos));
}

namespace
{
struct CallbackLog
{
  std::atomic<std::size_t> calls{0};
  std::atomic<std::size_t> events{0};
};
void on_new(const void * user_data, size_t n)
{
  auto log = static_cast<CallbackLog *>(const_cast<void *>(user_data));
  ++log->calls;
  log->events += n;
}
}  // namespace

TEST_F(PubSub, OnNewMessageCallback)
{
  auto p = pub(basic_cpp(), "/cb");
  auto s = sub(basic_cpp(), "/cb");
  test_msgs::msg::BasicTypes m;
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  }
  CallbackLog log;
  // Setting the callback reports what is already queued, in one call.
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log));
  EXPECT_EQ(1u, log.calls.load());
  EXPECT_EQ(3u, log.events.load());
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  EXPECT_EQ(2u, log.calls.load());
  EXPECT_EQ(4u, log.events.load());
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, nullptr, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  EXPECT_EQ(2u, log.calls.load());

  // Only the sample that arrived without a callback is pending (5 are queued,
  // none taken). rclcpp sets each callback twice (temporary, then permanent):
  // the pending count is reported once, to the first.
  CallbackLog log2;
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log2));
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log2));
  EXPECT_EQ(1u, log2.calls.load());
  EXPECT_EQ(1u, log2.events.load());
  // Replacing the callback does not re-report anything.
  CallbackLog log3;
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log3));
  EXPECT_EQ(0u, log3.calls.load());
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  EXPECT_EQ(1u, log3.events.load());
  EXPECT_EQ(1u, log2.events.load());
  EXPECT_EQ(4u, log.events.load());
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, nullptr, nullptr));
}

TEST_F(PubSub, OnNewMessageCallbackReportsTransientLocalHistory)
{
  rmw_qos_profile_t tl = rmw_qos_profile_default;
  tl.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  auto p = pub(basic_cpp(), "/cb_tl", tl);
  test_msgs::msg::BasicTypes m;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
  auto s = sub(basic_cpp(), "/cb_tl", tl);
  CallbackLog log;
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log));
  EXPECT_EQ(1u, log.calls.load());
  EXPECT_EQ(2u, log.events.load());
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, nullptr, nullptr));
}

TEST_F(PubSub, ConcurrentPublishersOneSubscriber)
{
  rmw_qos_profile_t all = rmw_qos_profile_default;
  all.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  constexpr int kPublishers = 4;
  constexpr int kEach = 2500;
  std::vector<rmw_publisher_t *> ps;
  for (int i = 0; i < kPublishers; ++i) {
    ps.push_back(pub(basic_cpp(), "/many", all));
  }
  auto s = sub(basic_cpp(), "/many", all);
  std::vector<std::thread> threads;
  for (int t = 0; t < kPublishers; ++t) {
    threads.emplace_back(
      [&, t] {
        test_msgs::msg::BasicTypes m;
        m.int32_value = t;
        for (int i = 0; i < kEach; ++i) {
          m.int64_value = i;
          EXPECT_EQ(RMW_RET_OK, rmw_publish(ps[static_cast<std::size_t>(t)], &m, nullptr));
        }
      });
  }
  for (auto & th : threads) {
    th.join();
  }
  std::vector<std::int64_t> last(kPublishers, -1);
  int total = 0;
  test_msgs::msg::BasicTypes m;
  bool taken = false;
  while (rmw_take(s, &m, &taken, nullptr) == RMW_RET_OK && taken) {
    auto & l = last[static_cast<std::size_t>(m.int32_value)];
    EXPECT_EQ(l + 1, m.int64_value) << "per-publisher order";
    l = m.int64_value;
    ++total;
  }
  EXPECT_EQ(kPublishers * kEach, total);
}

TEST_F(PubSub, DestroySubscriptionWhilePublishing)
{
  rmw_publisher_t * p = pub(basic_cpp(), "/race");
  std::atomic<bool> stop{false};
  std::thread publisher([&] {
      test_msgs::msg::BasicTypes m;
      while (!stop) {
        EXPECT_EQ(RMW_RET_OK, rmw_publish(p, &m, nullptr));
      }
    });
  CallbackLog log;
  rmw_subscription_options_t o = rmw_get_default_subscription_options();
  for (int i = 0; i < 200; ++i) {
    rmw_subscription_t * s =
      rmw_create_subscription(node, basic_cpp(), "/race", &rmw_qos_profile_default, &o);
    ASSERT_NE(nullptr, s);
    ASSERT_EQ(RMW_RET_OK, rmw_subscription_set_on_new_message_callback(s, on_new, &log));
    std::this_thread::yield();
    ASSERT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, s));
  }
  // Once destroy returned, no callback may run for those subscriptions.
  const std::size_t calls = log.calls.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(calls, log.calls.load());
  stop = true;
  publisher.join();
}

TEST_F(PubSub, PublishBadArguments)
{
  auto p = pub(basic_cpp(), "/bad");
  auto s = sub(basic_cpp(), "/bad");
  test_msgs::msg::BasicTypes m;
  bool taken = false;
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_publish(nullptr, &m, nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_publish(p, nullptr, nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_take(s, nullptr, &taken, nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_take(s, &m, nullptr, nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_take_with_info(s, &m, &taken, nullptr, nullptr));
  rmw_reset_error();
  const char * id = p->implementation_identifier;
  p->implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_publish(p, &m, nullptr));
  rmw_reset_error();
  p->implementation_identifier = id;
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_wait_for_all_acked(p, rmw_time_t{1, 0}));
}
