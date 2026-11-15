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

// rclcpp-level QoS and event tests, run against rmw_intraprocess_cpp and the
// reference rmw_fastrtps_cpp (registered twice in CMakeLists.txt).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
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

class RclcppQos : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    node = std::make_shared<rclcpp::Node>("qos", "/m3", node_options());
    exec.add_node(node);
  }

  std::string topic(const char * name) const
  {
    return std::string("m3_") + ::testing::UnitTest::GetInstance()->current_test_info()->name() +
           "_" + name;
  }

  template<typename Pred>
  bool spin_until(Pred pred, std::chrono::milliseconds timeout = 5s)
  {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!pred() && std::chrono::steady_clock::now() < end) {
      exec.spin_some(20ms);
      std::this_thread::sleep_for(5ms);
    }
    return pred();
  }

  rclcpp::Node::SharedPtr node;
  rclcpp::executors::SingleThreadedExecutor exec;
};

}  // namespace

TEST_F(RclcppQos, TfStaticStyleLatchedDeliveryToLateJoiner)
{
  // /tf_static: KeepLast(1), RELIABLE, TRANSIENT_LOCAL on both sides.
  const auto qos = rclcpp::QoS(1).transient_local();
  auto pub = node->create_publisher<BasicTypes>(topic("tf_static"), qos);
  BasicTypes msg;
  msg.int64_value = 42;
  pub->publish(msg);
  std::this_thread::sleep_for(100ms);

  std::atomic<int64_t> got{-1};
  auto sub = node->create_subscription<BasicTypes>(
    topic("tf_static"), qos, [&](const BasicTypes & m) {got = m.int64_value;});
  ASSERT_TRUE(spin_until([&] {return got.load() == 42;})) << "got " << got.load();

  // A late VOLATILE subscription does not get the latched sample.
  std::atomic<int> volatile_count{0};
  auto vsub = node->create_subscription<BasicTypes>(
    topic("tf_static"), rclcpp::QoS(1), [&](const BasicTypes &) {++volatile_count;});
  spin_until([] {return false;}, 300ms);
  EXPECT_EQ(0, volatile_count.load());
}

TEST_F(RclcppQos, MatchedEventCallbacks)
{
  std::atomic<size_t> current{0};
  std::atomic<int> calls{0};
  rclcpp::PublisherOptions popts;
  popts.event_callbacks.matched_callback = [&](rclcpp::MatchedInfo & info) {
      current = info.current_count;
      ++calls;
    };
  auto pub = node->create_publisher<BasicTypes>(topic("t"), 10, popts);
  auto sub = node->create_subscription<BasicTypes>(topic("t"), 10, [](const BasicTypes &) {});
  ASSERT_TRUE(spin_until([&] {return current.load() == 1;}));
  sub.reset();
  ASSERT_TRUE(spin_until([&] {return current.load() == 0 && calls.load() >= 2;}));
}

TEST_F(RclcppQos, IncompatibleQosCallbacksOnBothSides)
{
  std::atomic<int> pub_policy{-1};
  std::atomic<int> sub_policy{-1};
  rclcpp::PublisherOptions popts;
  popts.event_callbacks.incompatible_qos_callback = [&](rclcpp::QOSOfferedIncompatibleQoSInfo & i) {
      pub_policy = static_cast<int>(i.last_policy_kind);
    };
  rclcpp::SubscriptionOptions sopts;
  sopts.event_callbacks.incompatible_qos_callback =
    [&](rclcpp::QOSRequestedIncompatibleQoSInfo & i) {
      sub_policy = static_cast<int>(i.last_policy_kind);
    };
  auto pub = node->create_publisher<BasicTypes>(
    topic("t"), rclcpp::QoS(10).best_effort(), popts);
  auto sub = node->create_subscription<BasicTypes>(
    topic("t"), rclcpp::QoS(10).reliable(), [](const BasicTypes &) {}, sopts);
  ASSERT_TRUE(
    spin_until(
      [&] {
        return pub_policy.load() == RMW_QOS_POLICY_RELIABILITY &&
               sub_policy.load() == RMW_QOS_POLICY_RELIABILITY;
      })) << "pub " << pub_policy.load() << " sub " << sub_policy.load();
}
