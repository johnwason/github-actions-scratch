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

// rclcpp-level publish/subscribe tests. Built once and registered twice: with
// RMW_IMPLEMENTATION=rmw_intraprocess_cpp and with rmw_fastrtps_cpp (the
// reference RMW), so behavior differences show up as test failures.
// Does not link librmw_intraprocess_cpp (goes through rmw_implementation).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rmw/features.h"
#include "rmw/rmw.h"
#include "test_msgs/msg/basic_types.hpp"

using namespace std::chrono_literals;
using test_msgs::msg::BasicTypes;

namespace
{

rclcpp::NodeOptions node_options()
{
  // Services (parameter services, type descriptions) need M4.
  rclcpp::NodeOptions options;
  options.start_parameter_services(false)
  .start_parameter_event_publisher(false)
  .parameter_overrides({rclcpp::Parameter("start_type_description_service", false)});
  return options;
}

template<typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout = 5s)
{
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(5ms);
  }
  return pred();
}

class RclcppPubSub : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  /// Unique topic per test so RMWs with discovery caches don't cross-talk.
  std::string topic(const char * name) const
  {
    return std::string("m2_") + ::testing::UnitTest::GetInstance()->current_test_info()->name() +
           "_" + name;
  }
};

/// Composed talker + listener (separate nodes, one executor). The talker
/// publishes `count` messages from its own thread while the executor spins.
template<typename ExecutorT>
void run_talker_listener(const std::string & topic_name, int count)
{
  auto talker = std::make_shared<rclcpp::Node>("talker", "/m2", node_options());
  auto listener = std::make_shared<rclcpp::Node>("listener", "/m2", node_options());
  const auto qos = rclcpp::QoS(rclcpp::KeepAll()).reliable();
  auto pub = talker->create_publisher<BasicTypes>(topic_name, qos);

  std::mutex m;
  std::vector<std::int64_t> received;
  received.reserve(static_cast<std::size_t>(count));
  auto sub = listener->create_subscription<BasicTypes>(
    topic_name, qos, [&](const BasicTypes & msg) {
      std::lock_guard<std::mutex> lock(m);
      received.push_back(msg.int64_value);
    });

  ExecutorT exec;
  exec.add_node(talker);
  exec.add_node(listener);
  std::thread spinner([&exec] {exec.spin();});
  ASSERT_TRUE(wait_until([&] {return pub->get_subscription_count() == 1;}));

  std::thread publisher([&] {
      BasicTypes msg;
      for (int i = 0; i < count; ++i) {
        msg.int64_value = i;
        pub->publish(msg);
      }
    });
  publisher.join();
  const bool all = wait_until(
    [&] {
      std::lock_guard<std::mutex> lock(m);
      return received.size() == static_cast<std::size_t>(count);
    }, 60s);
  exec.cancel();
  spinner.join();

  ASSERT_TRUE(all) << "received " << received.size() << " of " << count;
  for (int i = 0; i < count; ++i) {
    ASSERT_EQ(i, received[static_cast<std::size_t>(i)]) << "out of order at " << i;
  }
}

/// 10k messages on rmw_intraprocess_cpp. The reference RMW (rmw_fastrtps_cpp) runs the
/// same test with 1000: at 10k its default KEEP_ALL resource limits drop samples
/// under the multi-threaded executor (observed 5769/10000), which is not what
/// this comparison is about.
int stress_count()
{
  return std::string(rmw_get_implementation_identifier()) == "rmw_intraprocess_cpp" ? 10000 : 1000;
}

}  // namespace

TEST_F(RclcppPubSub, ComposedTalkerListener10kSingleThreaded)
{
  run_talker_listener<rclcpp::executors::SingleThreadedExecutor>(topic("chatter"), stress_count());
}

TEST_F(RclcppPubSub, ComposedTalkerListener10kMultiThreaded)
{
  run_talker_listener<rclcpp::executors::MultiThreadedExecutor>(topic("chatter"), stress_count());
}

TEST_F(RclcppPubSub, ComposedTalkerListener10kEventsExecutor)
{
  run_talker_listener<rclcpp::experimental::executors::EventsExecutor>(
    topic("chatter"), stress_count());
}

TEST_F(RclcppPubSub, BestEffortPublisherAndReliableSubscriptionDoNotMatch)
{
  auto node = std::make_shared<rclcpp::Node>("qos_mismatch", "/m2", node_options());
  auto pub = node->create_publisher<BasicTypes>(topic("t"), rclcpp::QoS(10).best_effort());
  std::atomic<int> received{0};
  auto sub = node->create_subscription<BasicTypes>(
    topic("t"), rclcpp::QoS(10).reliable(), [&](const BasicTypes &) {++received;});
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  for (int i = 0; i < 5; ++i) {
    pub->publish(BasicTypes());
    exec.spin_some(50ms);
  }
  exec.spin_some(200ms);
  EXPECT_EQ(0u, pub->get_subscription_count());
  EXPECT_EQ(0u, sub->get_publisher_count());
  EXPECT_EQ(0, received.load());
}

TEST_F(RclcppPubSub, KeepLastDropsOldest)
{
  auto node = std::make_shared<rclcpp::Node>("keep_last", "/m2", node_options());
  const auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();
  auto pub = node->create_publisher<BasicTypes>(topic("t"), qos);
  std::vector<std::int64_t> received;
  auto sub = node->create_subscription<BasicTypes>(
    topic("t"), qos, [&](const BasicTypes & msg) {received.push_back(msg.int64_value);});
  ASSERT_TRUE(wait_until([&] {return pub->get_subscription_count() == 1;}));
  BasicTypes msg;
  for (int i = 1; i <= 25; ++i) {
    msg.int64_value = i;
    pub->publish(msg);
  }
  ASSERT_TRUE(wait_until([&] {return sub->get_publisher_count() == 1;}));
  std::this_thread::sleep_for(200ms);  // let a networked reference RMW deliver everything
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  const auto end = std::chrono::steady_clock::now() + 2s;
  while (received.size() < 10 && std::chrono::steady_clock::now() < end) {
    exec.spin_some(50ms);
  }
  exec.spin_some(100ms);
  ASSERT_EQ(10u, received.size());
  for (std::size_t i = 0; i < received.size(); ++i) {
    EXPECT_EQ(static_cast<std::int64_t>(16 + i), received[i]);
  }
}

TEST_F(RclcppPubSub, MessageInfoIdentifiesPublisher)
{
  auto node = std::make_shared<rclcpp::Node>("info", "/m2", node_options());
  auto pub = node->create_publisher<BasicTypes>(topic("t"), 10);
  std::vector<rmw_message_info_t> infos;
  auto sub = node->create_subscription<BasicTypes>(
    topic("t"), 10, [&](const BasicTypes &, const rclcpp::MessageInfo & info) {
      infos.push_back(info.get_rmw_message_info());
    });
  ASSERT_TRUE(wait_until([&] {return pub->get_subscription_count() == 1;}));
  pub->publish(BasicTypes());
  pub->publish(BasicTypes());
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  const auto end = std::chrono::steady_clock::now() + 2s;
  while (infos.size() < 2 && std::chrono::steady_clock::now() < end) {
    exec.spin_some(50ms);
  }
  ASSERT_EQ(2u, infos.size());
  const rmw_gid_t & pub_gid = pub->get_gid();
  for (const auto & info : infos) {
    EXPECT_EQ(0, std::memcmp(pub_gid.data, info.publisher_gid.data, RMW_GID_STORAGE_SIZE));
    EXPECT_GT(info.source_timestamp, 0);
    EXPECT_GE(info.received_timestamp, info.source_timestamp);
  }
  if (rmw_feature_supported(RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER)) {
    EXPECT_LT(infos[0].publication_sequence_number, infos[1].publication_sequence_number);
  }
}
