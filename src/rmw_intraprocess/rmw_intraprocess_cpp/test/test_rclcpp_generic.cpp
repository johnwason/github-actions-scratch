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

// Generic (serialized) publishers and subscriptions (M6), run against
// rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
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

BasicTypes make(int32_t i)
{
  BasicTypes m;
  m.int32_value = i;
  m.int64_value = -1000 * static_cast<int64_t>(i);
  m.float64_value = 0.5 * i;
  m.bool_value = (i % 2) == 0;
  m.uint8_value = static_cast<uint8_t>(i);
  return m;
}

class RclcppGeneric : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    node = std::make_shared<rclcpp::Node>("m6_generic");
    executor.add_node(node);
    spinner = std::thread([this]() {executor.spin();});
  }

  void TearDown() override
  {
    executor.cancel();
    spinner.join();
    executor.remove_node(node);
  }

  std::string topic(const char * suffix) const
  {
    return std::string("/m6_") +
           ::testing::UnitTest::GetInstance()->current_test_info()->name() + suffix;
  }

  rclcpp::QoS qos() const {return rclcpp::QoS(rclcpp::KeepAll()).reliable();}

  rclcpp::Node::SharedPtr node;
  rclcpp::executors::SingleThreadedExecutor executor;
  std::thread spinner;
};

}  // namespace

TEST_F(RclcppGeneric, GenericSubscriptionReceivesTypedPublications)
{
  const std::string name = topic("");
  std::mutex mutex;
  std::vector<BasicTypes> received;
  rclcpp::Serialization<BasicTypes> serialization;
  auto sub = node->create_generic_subscription(
    name, "test_msgs/msg/BasicTypes", qos(),
    [&](std::shared_ptr<const rclcpp::SerializedMessage> msg) {
      BasicTypes m;
      serialization.deserialize_message(msg.get(), &m);
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(m);
    });
  auto pub = node->create_publisher<BasicTypes>(name, qos());
  ASSERT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 1u;}));

  for (int32_t i = 0; i < 20; ++i) {
    pub->publish(make(i));
  }
  ASSERT_TRUE(
    wait_until(
      [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size() == 20u;
      }));
  std::lock_guard<std::mutex> lock(mutex);
  for (int32_t i = 0; i < 20; ++i) {
    EXPECT_EQ(make(i), received[static_cast<size_t>(i)]) << i;
  }
}

TEST_F(RclcppGeneric, GenericRoundTripToTypedSubscription)
{
  // typed pub -> generic sub -> generic pub (same bytes) -> typed sub
  const std::string in = topic("_in");
  const std::string out = topic("_out");
  auto generic_pub = node->create_generic_publisher(out, "test_msgs/msg/BasicTypes", qos());
  auto generic_sub = node->create_generic_subscription(
    in, "test_msgs/msg/BasicTypes", qos(),
    [&](std::shared_ptr<const rclcpp::SerializedMessage> msg) {
      generic_pub->publish(*msg);
    });
  std::mutex mutex;
  std::vector<BasicTypes> received;
  auto typed_sub = node->create_subscription<BasicTypes>(
    out, qos(), [&](const BasicTypes & m) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(m);
    });
  auto typed_pub = node->create_publisher<BasicTypes>(in, qos());
  ASSERT_TRUE(wait_until([&]() {return typed_pub->get_subscription_count() == 1u;}));
  ASSERT_TRUE(wait_until([&]() {return generic_pub->get_subscription_count() == 1u;}));

  for (int32_t i = 0; i < 10; ++i) {
    typed_pub->publish(make(i));
  }
  ASSERT_TRUE(
    wait_until(
      [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size() == 10u;
      }));
  std::lock_guard<std::mutex> lock(mutex);
  for (int32_t i = 0; i < 10; ++i) {
    EXPECT_EQ(make(i), received[static_cast<size_t>(i)]) << i;
  }
}

TEST_F(RclcppGeneric, GenericPublisherSerializedBytesMatchTyped)
{
  // A GenericPublisher publishing rclcpp::Serialization output reaches a typed
  // subscription, and the graph reports the generic publisher's type.
  const std::string name = topic("");
  rclcpp::Serialization<BasicTypes> serialization;
  std::mutex mutex;
  std::vector<BasicTypes> received;
  auto typed_sub = node->create_subscription<BasicTypes>(
    name, qos(), [&](const BasicTypes & m) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(m);
    });
  auto generic_pub = node->create_generic_publisher(name, "test_msgs/msg/BasicTypes", qos());
  ASSERT_TRUE(wait_until([&]() {return generic_pub->get_subscription_count() == 1u;}));
  auto info = node->get_publishers_info_by_topic(name);
  ASSERT_EQ(1u, info.size());
  EXPECT_EQ("test_msgs/msg/BasicTypes", info[0].topic_type());

  rclcpp::SerializedMessage serialized;
  BasicTypes msg = make(42);
  serialization.serialize_message(&msg, &serialized);
  generic_pub->publish(serialized);
  ASSERT_TRUE(
    wait_until(
      [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size() == 1u;
      }));
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ(msg, received[0]);
}
