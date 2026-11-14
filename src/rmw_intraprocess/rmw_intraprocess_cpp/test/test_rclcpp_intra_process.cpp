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

// rclcpp intra-process communication (use_intra_process_comms) on top of the
// RMW, run against rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.
// rclcpp passes pointers between intra-process endpoints of one context and
// uses the RMW for every other subscription; the RMW must not deliver a second
// copy to intra-process subscriptions (ignore_local_publications) and must
// count them in the matched count.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "test_msgs/msg/basic_types.hpp"

using test_msgs::msg::BasicTypes;

namespace
{

bool wait_until(
  const std::function<bool()> & pred,
  std::chrono::milliseconds timeout = std::chrono::milliseconds(10000))
{
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

/// Spins one executor on a thread for the lifetime of the object.
class Spinner
{
public:
  explicit Spinner(const rclcpp::Context::SharedPtr & context)
  : executor_(make_options(context))
  {
  }
  ~Spinner() {stop();}

  void add(const rclcpp::Node::SharedPtr & node) {executor_.add_node(node);}
  void start() {thread_ = std::thread([this]() {executor_.spin();});}
  void stop()
  {
    if (thread_.joinable()) {
      executor_.cancel();
      thread_.join();
    }
  }

private:
  static rclcpp::ExecutorOptions make_options(const rclcpp::Context::SharedPtr & context)
  {
    rclcpp::ExecutorOptions options;
    options.context = context;
    return options;
  }

  rclcpp::executors::SingleThreadedExecutor executor_;
  std::thread thread_;
};

class RclcppIntraProcess : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  std::string topic() const
  {
    return std::string("/ipc_") +
           ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }

  static rclcpp::QoS qos() {return rclcpp::QoS(rclcpp::KeepLast(100)).reliable();}
};

}  // namespace

TEST_F(RclcppIntraProcess, UniquePtrIsPassedWithoutCopy)
{
  auto node = std::make_shared<rclcpp::Node>(
    "ipc_unique", rclcpp::NodeOptions().use_intra_process_comms(true));
  std::atomic<std::uintptr_t> received{0};
  auto sub = node->create_subscription<BasicTypes>(
    topic(), qos(), [&](std::unique_ptr<BasicTypes> msg) {
      received = reinterpret_cast<std::uintptr_t>(msg.get());
    });
  auto pub = node->create_publisher<BasicTypes>(topic(), qos());
  ASSERT_TRUE(wait_until([&]() {return pub->get_intra_process_subscription_count() == 1u;}));
  // The RMW counts the intra-process subscription too.
  ASSERT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 1u;}));

  Spinner spinner(rclcpp::contexts::get_global_default_context());
  spinner.add(node);
  spinner.start();
  auto msg = std::make_unique<BasicTypes>();
  msg->int32_value = 7;
  const auto sent = reinterpret_cast<std::uintptr_t>(msg.get());
  pub->publish(std::move(msg));
  ASSERT_TRUE(wait_until([&]() {return received.load() != 0;}));
  // Same object: rclcpp moved the pointer, nothing was serialized.
  EXPECT_EQ(sent, received.load());
}

/// Publishes kCount messages from an intra-process publisher to an
/// intra-process subscription and a plain subscription in the same context,
/// and (if `second_context`) a subscription in another context; checks that each
/// gets every message exactly once.
void check_each_subscription_once(const std::string & topic, bool second_context)
{
  auto ctx2 = std::make_shared<rclcpp::Context>();
  ctx2->init(0, nullptr);
  {
    auto ipc_node = std::make_shared<rclcpp::Node>(
      "ipc_pub_node", rclcpp::NodeOptions().use_intra_process_comms(true));
    auto plain_node = std::make_shared<rclcpp::Node>(
      "ipc_plain_node", rclcpp::NodeOptions().use_intra_process_comms(false));
    auto other_node = std::make_shared<rclcpp::Node>(
      "ipc_other_node", rclcpp::NodeOptions().context(ctx2));
    const rclcpp::QoS qos = rclcpp::QoS(rclcpp::KeepLast(100)).reliable();

    std::atomic<int> ipc_count{0};
    std::atomic<int> plain_count{0};
    std::atomic<int> other_count{0};
    auto ipc_sub = ipc_node->create_subscription<BasicTypes>(
      topic, qos, [&](BasicTypes::ConstSharedPtr) {++ipc_count;});
    auto plain_sub = plain_node->create_subscription<BasicTypes>(
      topic, qos, [&](BasicTypes::ConstSharedPtr) {++plain_count;});
    rclcpp::Subscription<BasicTypes>::SharedPtr other_sub;
    if (second_context) {
      other_sub = other_node->create_subscription<BasicTypes>(
        topic, qos, [&](BasicTypes::ConstSharedPtr) {++other_count;});
    }
    auto pub = ipc_node->create_publisher<BasicTypes>(topic, qos);

    // All subscriptions are counted; 1 of them is intra-process.
    const size_t total = second_context ? 3u : 2u;
    ASSERT_TRUE(wait_until([&]() {return pub->get_subscription_count() == total;}));
    EXPECT_EQ(1u, pub->get_intra_process_subscription_count());

    Spinner spinner(rclcpp::contexts::get_global_default_context());
    spinner.add(ipc_node);
    spinner.add(plain_node);
    spinner.start();
    Spinner spinner2(ctx2);
    spinner2.add(other_node);
    spinner2.start();

    constexpr int kCount = 20;
    const int other_expected = second_context ? kCount : 0;
    for (int i = 0; i < kCount; ++i) {
      BasicTypes m;
      m.int32_value = i;
      pub->publish(m);
    }
    ASSERT_TRUE(
      wait_until(
        [&]() {
          return ipc_count == kCount && plain_count == kCount && other_count == other_expected;
        }))
      << "ipc " << ipc_count << " plain " << plain_count << " other " << other_count;
    // Any duplicate (e.g. the RMW also delivering to the intra-process
    // subscription) would show up as an extra message.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(kCount, ipc_count.load());
    EXPECT_EQ(kCount, plain_count.load());
    EXPECT_EQ(other_expected, other_count.load());
  }
  ctx2->shutdown("test done");
}

/// The reference RMW gives each context its own DDS participant, and in the
/// --network=none test container data does not flow between participants
/// (discovery does; checked: SecondContextWithoutIntraProcess also gets 0
/// messages without intra-process comms). Cross-context delivery is this RMW's
/// single-domain feature, so those checks run on rmw_intraprocess_cpp only.
bool reference_rmw_cannot_cross_contexts()
{
  const char * rmw = std::getenv("RMW_IMPLEMENTATION");
  return rmw != nullptr && std::string(rmw) == "rmw_fastrtps_cpp";
}

TEST_F(RclcppIntraProcess, EachSubscriptionGetsEachMessageOnce)
{
  check_each_subscription_once(topic(), false);
}

TEST_F(RclcppIntraProcess, EachSubscriptionGetsEachMessageOnceWithSecondContext)
{
  if (reference_rmw_cannot_cross_contexts()) {
    GTEST_SKIP() << "rmw_fastrtps_cpp: no data between contexts in the --network=none container";
  }
  check_each_subscription_once(topic(), true);
}

TEST_F(RclcppIntraProcess, SecondContextWithoutIntraProcess)
{
  if (reference_rmw_cannot_cross_contexts()) {
    GTEST_SKIP() << "rmw_fastrtps_cpp: no data between contexts in the --network=none container";
  }
  // Plain publisher (no intra-process comms) and a subscription in a second
  // context: every message arrives once through the RMW.
  auto ctx2 = std::make_shared<rclcpp::Context>();
  ctx2->init(0, nullptr);
  {
    auto pub_node = std::make_shared<rclcpp::Node>(
      "ctx_pub_node", rclcpp::NodeOptions().use_intra_process_comms(false));
    auto other_node = std::make_shared<rclcpp::Node>(
      "ctx_other_node", rclcpp::NodeOptions().context(ctx2));
    std::atomic<int> other_count{0};
    auto other_sub = other_node->create_subscription<BasicTypes>(
      topic(), qos(), [&](BasicTypes::ConstSharedPtr) {++other_count;});
    auto pub = pub_node->create_publisher<BasicTypes>(topic(), qos());
    ASSERT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 1u;}));
    Spinner spinner2(ctx2);
    spinner2.add(other_node);
    spinner2.start();
    constexpr int kCount = 20;
    for (int i = 0; i < kCount; ++i) {
      pub->publish(BasicTypes());
    }
    EXPECT_TRUE(wait_until([&]() {return other_count == kCount;})) << "other " << other_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(kCount, other_count.load());
  }
  ctx2->shutdown("test done");
}
