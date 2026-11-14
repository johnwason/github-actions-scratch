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

// rclcpp-level graph tests (M5 acceptance), run against rmw_intraprocess_cpp
// and the reference rmw_fastrtps_cpp (registered twice in CMakeLists.txt).
// Discovery is asynchronous on the reference RMW, so checks poll with a deadline.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/srv/basic_types.hpp"

using namespace std::chrono_literals;

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
    std::this_thread::sleep_for(10ms);
  }
  return true;
}

class RclcppGraph : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    observer = std::make_shared<rclcpp::Node>("m5_observer");
    actor = std::make_shared<rclcpp::Node>("m5_actor", "/m5_ns");
  }

  std::string unique_name(const char * suffix) const
  {
    return std::string("/m5_") +
           ::testing::UnitTest::GetInstance()->current_test_info()->name() + suffix;
  }

  bool has_topic(const std::string & topic, const std::string & type) const
  {
    auto all = observer->get_topic_names_and_types();
    auto it = all.find(topic);
    return it != all.end() &&
           std::find(it->second.begin(), it->second.end(), type) != it->second.end();
  }

  rclcpp::Node::SharedPtr observer;
  rclcpp::Node::SharedPtr actor;
};

}  // namespace

TEST_F(RclcppGraph, TopicNamesAndTypesComeAndGo)
{
  const std::string topic = unique_name("_topic");
  const std::string type = "test_msgs/msg/BasicTypes";
  EXPECT_FALSE(has_topic(topic, type));
  auto pub = actor->create_publisher<test_msgs::msg::BasicTypes>(topic, 10);
  EXPECT_TRUE(wait_until([&]() {return has_topic(topic, type);}));
  pub.reset();
  EXPECT_TRUE(wait_until([&]() {return !has_topic(topic, type);}));
}

TEST_F(RclcppGraph, CountsAndMatchedCountsComeAndGo)
{
  const std::string topic = unique_name("_topic");
  auto pub = actor->create_publisher<test_msgs::msg::BasicTypes>(topic, 10);
  EXPECT_TRUE(wait_until([&]() {return observer->count_publishers(topic) == 1u;}));
  EXPECT_EQ(0u, observer->count_subscribers(topic));
  EXPECT_EQ(0u, pub->get_subscription_count());

  auto sub = observer->create_subscription<test_msgs::msg::BasicTypes>(
    topic, 10, [](test_msgs::msg::BasicTypes::ConstSharedPtr) {});
  auto sub2 = actor->create_subscription<test_msgs::msg::BasicTypes>(
    topic, 10, [](test_msgs::msg::BasicTypes::ConstSharedPtr) {});
  EXPECT_TRUE(wait_until([&]() {return observer->count_subscribers(topic) == 2u;}));
  EXPECT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 2u;}));
  EXPECT_TRUE(wait_until([&]() {return sub->get_publisher_count() == 1u;}));

  sub2.reset();
  EXPECT_TRUE(wait_until([&]() {return observer->count_subscribers(topic) == 1u;}));
  EXPECT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 1u;}));
  pub.reset();
  EXPECT_TRUE(wait_until([&]() {return observer->count_publishers(topic) == 0u;}));
  EXPECT_TRUE(wait_until([&]() {return sub->get_publisher_count() == 0u;}));
}

TEST_F(RclcppGraph, WaitForServiceAsServicesComeAndGo)
{
  const std::string service = unique_name("_srv");
  auto client = observer->create_client<test_msgs::srv::BasicTypes>(service);
  EXPECT_FALSE(client->wait_for_service(100ms));
  EXPECT_FALSE(client->service_is_ready());

  auto srv = actor->create_service<test_msgs::srv::BasicTypes>(
    service, [](const std::shared_ptr<test_msgs::srv::BasicTypes::Request>,
    std::shared_ptr<test_msgs::srv::BasicTypes::Response>) {});
  EXPECT_TRUE(client->wait_for_service(10s));
  EXPECT_TRUE(client->service_is_ready());
  EXPECT_EQ(1u, observer->count_services(service));
  EXPECT_TRUE(wait_until([&]() {return observer->count_clients(service) == 1u;}));
  auto services = observer->get_service_names_and_types();
  ASSERT_NE(services.end(), services.find(service));
  EXPECT_EQ(std::vector<std::string>{"test_msgs/srv/BasicTypes"}, services[service]);

  srv.reset();
  EXPECT_TRUE(wait_until([&]() {return !client->service_is_ready();}));
  EXPECT_TRUE(wait_until([&]() {return observer->count_services(service) == 0u;}));
  EXPECT_FALSE(client->wait_for_service(100ms));
}

TEST_F(RclcppGraph, NodeNamesAndEndpointInfo)
{
  EXPECT_TRUE(
    wait_until(
      [&]() {
        auto names = observer->get_node_names();
        return std::find(names.begin(), names.end(), "/m5_ns/m5_actor") != names.end();
      }));

  const std::string topic = unique_name("_topic");
  auto pub = actor->create_publisher<test_msgs::msg::BasicTypes>(
    topic, rclcpp::QoS(7).transient_local());
  std::vector<rclcpp::TopicEndpointInfo> info;
  EXPECT_TRUE(
    wait_until(
      [&]() {
        info = observer->get_publishers_info_by_topic(topic);
        return info.size() == 1u;
      }));
  ASSERT_EQ(1u, info.size());
  EXPECT_EQ("m5_actor", info[0].node_name());
  EXPECT_EQ("/m5_ns", info[0].node_namespace());
  EXPECT_EQ("test_msgs/msg/BasicTypes", info[0].topic_type());
  EXPECT_EQ(rclcpp::EndpointType::Publisher, info[0].endpoint_type());
  EXPECT_EQ(rclcpp::DurabilityPolicy::TransientLocal, info[0].qos_profile().durability());
  EXPECT_EQ(rclcpp::ReliabilityPolicy::Reliable, info[0].qos_profile().reliability());
  const rosidl_type_hash_t & hash = info[0].topic_type_hash();
  EXPECT_EQ(1, hash.version);

  const std::string service = unique_name("_srv");
  auto srv = actor->create_service<test_msgs::srv::BasicTypes>(
    service, [](const std::shared_ptr<test_msgs::srv::BasicTypes::Request>,
    std::shared_ptr<test_msgs::srv::BasicTypes::Response>) {});
  EXPECT_TRUE(
    wait_until(
      [&]() {
        auto names = observer->get_service_names_and_types_by_node("m5_actor", "/m5_ns");
        return names.find(service) != names.end();
      }));
  EXPECT_THROW(
    observer->get_service_names_and_types_by_node("no_such_node", "/"), std::runtime_error);
}

TEST_F(RclcppGraph, GraphEventOnChange)
{
  auto event = observer->get_graph_event();
  const std::string topic = unique_name("_topic");
  auto pub = actor->create_publisher<test_msgs::msg::BasicTypes>(topic, 10);
  bool seen = wait_until(
    [&]() {
      observer->wait_for_graph_change(event, 100ms);
      return event->check_and_clear() && observer->count_publishers(topic) == 1u;
    });
  EXPECT_TRUE(seen || observer->count_publishers(topic) == 1u);
  EXPECT_EQ(1u, observer->count_publishers(topic));
}
