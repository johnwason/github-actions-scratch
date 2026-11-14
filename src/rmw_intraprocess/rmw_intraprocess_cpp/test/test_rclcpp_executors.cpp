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

// M8: every rclcpp executor type (SingleThreaded, StaticSingleThreaded,
// MultiThreaded, Events) drives topics, services, timers and waitables of
// several manually composed nodes (one process, one executor). Run against
// rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "./rclcpp_test_utils.hpp"

using namespace std::chrono_literals;
using m8::ExecutorKind;

namespace
{

class Executors : public ::testing::TestWithParam<ExecutorKind>
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  std::string name(const std::string & prefix) const
  {
    return m8::unique_name(prefix);
  }
};

}  // namespace

TEST_P(Executors, PubSubAcrossNodesInOrder)
{
  auto pub_node = std::make_shared<rclcpp::Node>("pub", "/m8_exec");
  auto sub_node = std::make_shared<rclcpp::Node>("sub", "/m8_exec");
  const std::string topic = name("/m8_exec_topic");
  constexpr int kCount = 500;
  std::mutex mutex;
  std::vector<int32_t> received;
  auto sub = sub_node->create_subscription<test_msgs::msg::BasicTypes>(
    topic, rclcpp::QoS(rclcpp::KeepAll()).reliable(),
    [&](const test_msgs::msg::BasicTypes & m) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(m.int32_value);
    });
  auto pub = pub_node->create_publisher<test_msgs::msg::BasicTypes>(
    topic, rclcpp::QoS(rclcpp::KeepAll()).reliable());

  m8::Spinner spinner(GetParam());
  spinner.add(pub_node);
  spinner.add(sub_node);
  spinner.start();
  ASSERT_TRUE(m8::wait_until([&] {return pub->get_subscription_count() == 1u;}));
  for (int32_t i = 0; i < kCount; ++i) {
    test_msgs::msg::BasicTypes m;
    m.int32_value = i;
    pub->publish(m);
  }
  ASSERT_TRUE(
    m8::wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size() >= static_cast<size_t>(kCount);
      }));
  spinner.stop();
  std::lock_guard<std::mutex> lock(mutex);
  ASSERT_EQ(static_cast<size_t>(kCount), received.size());
  for (int32_t i = 0; i < kCount; ++i) {
    EXPECT_EQ(i, received[static_cast<size_t>(i)]);
  }
}

TEST_P(Executors, TimerDrivenServiceCalls)
{
  // A timer on one node sends requests; a service on another node answers;
  // the client callbacks run on the same executor.
  auto server_node = std::make_shared<rclcpp::Node>("server", "/m8_exec");
  auto client_node = std::make_shared<rclcpp::Node>("client", "/m8_exec");
  const std::string service_name = name("m8_exec_srv");
  auto service = server_node->create_service<test_msgs::srv::BasicTypes>(
    service_name,
    [](const std::shared_ptr<test_msgs::srv::BasicTypes::Request> req,
    std::shared_ptr<test_msgs::srv::BasicTypes::Response> res) {
      res->int64_value = 2 * static_cast<int64_t>(req->int32_value);
    });
  auto client = client_node->create_client<test_msgs::srv::BasicTypes>(service_name);
  ASSERT_TRUE(client->wait_for_service(5s));
  constexpr int kCalls = 50;
  std::atomic<int> sent{0};
  std::atomic<int> answered{0};
  std::atomic<bool> all_correct{true};
  auto timer = client_node->create_wall_timer(
    1ms, [&]() {
      if (sent.load() >= kCalls) {
        return;
      }
      auto req = std::make_shared<test_msgs::srv::BasicTypes::Request>();
      req->int32_value = sent.fetch_add(1);
      const int32_t v = req->int32_value;
      client->async_send_request(
        req, [&, v](rclcpp::Client<test_msgs::srv::BasicTypes>::SharedFuture f) {
          if (f.get()->int64_value != 2 * static_cast<int64_t>(v)) {
            all_correct = false;
          }
          ++answered;
        });
    });

  m8::Spinner spinner(GetParam());
  spinner.add(server_node);
  spinner.add(client_node);
  spinner.start();
  EXPECT_TRUE(m8::wait_until([&] {return answered.load() == kCalls;}));
  spinner.stop();
  EXPECT_EQ(kCalls, answered.load());
  EXPECT_TRUE(all_correct.load());
}

TEST_P(Executors, SpinUntilFutureCompleteAndCancel)
{
  auto node = std::make_shared<rclcpp::Node>("future", "/m8_exec");
  auto executor = m8::make_executor(GetParam());
  executor->add_node(node);
  // A one-shot timer completes the future from inside the executor.
  std::promise<int> promise;
  auto future = promise.get_future().share();
  rclcpp::TimerBase::SharedPtr timer;
  timer = node->create_wall_timer(
    20ms, [&]() {
      timer->cancel();
      promise.set_value(42);
    });
  ASSERT_EQ(rclcpp::FutureReturnCode::SUCCESS, executor->spin_until_future_complete(future, 10s));
  EXPECT_EQ(42, future.get());

  // cancel() from another thread wakes a spin() that has nothing to do.
  // (A cancel() issued before spin() starts is lost, so repeat it until spin()
  // has returned.)
  std::atomic<bool> returned{false};
  std::thread t([&]() {
      executor->spin();
      returned = true;
    });
  EXPECT_TRUE(
    m8::wait_until(
      [&] {
        executor->cancel();
        return returned.load();
      }));
  t.join();
  executor->remove_node(node);
}

TEST_P(Executors, NodesAddedWhileSpinning)
{
  // Entities created after spin() started must be picked up (the executor's
  // collection is rebuilt on its notify guard condition).
  m8::Spinner spinner(GetParam());
  auto first = std::make_shared<rclcpp::Node>("first", "/m8_exec");
  spinner.add(first);
  spinner.start();
  const std::string topic = name("/m8_exec_late");
  std::atomic<int> got{0};
  auto late = std::make_shared<rclcpp::Node>("late", "/m8_exec");
  auto sub = late->create_subscription<test_msgs::msg::BasicTypes>(
    topic, rclcpp::QoS(10).reliable(), [&](const test_msgs::msg::BasicTypes &) {++got;});
  spinner.add(late);
  auto pub = first->create_publisher<test_msgs::msg::BasicTypes>(topic, rclcpp::QoS(10).reliable());
  ASSERT_TRUE(m8::wait_until([&] {return pub->get_subscription_count() == 1u;}));
  // Retry until the executor has added the late node (each publish is a new
  // sample; any one of them proves delivery).
  EXPECT_TRUE(
    m8::wait_until(
      [&] {
        pub->publish(test_msgs::msg::BasicTypes());
        return got.load() > 0;
      }));
  spinner.stop();
}

INSTANTIATE_TEST_SUITE_P(
  AllExecutors, Executors,
  ::testing::Values(
    ExecutorKind::kSingleThreaded, ExecutorKind::kStaticSingleThreaded,
    ExecutorKind::kMultiThreaded, ExecutorKind::kEvents),
  m8::ExecutorKindName());
