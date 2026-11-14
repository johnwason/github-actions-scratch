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

// M8: parameters through the parameter services (remote get/set/list/
// describe from another node in the same process) and parameter events, under
// every executor type. Run against rmw_intraprocess_cpp and the reference
// rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"

#include "./rclcpp_test_utils.hpp"

using namespace std::chrono_literals;
using m8::ExecutorKind;
using rcl_interfaces::msg::ParameterEvent;

namespace
{

class Parameters : public ::testing::TestWithParam<ExecutorKind>
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    // A distinct namespace per test keeps the nodes' parameter services apart.
    ns = "/" + m8::unique_name("m8_params");
    target = std::make_shared<rclcpp::Node>("target", ns);
    observer = std::make_shared<rclcpp::Node>("observer", ns);
  }

  std::string ns;
  rclcpp::Node::SharedPtr target;
  rclcpp::Node::SharedPtr observer;
};

}  // namespace

TEST_P(Parameters, RemoteGetSetListDescribe)
{
  target->declare_parameter("count", 1);
  target->declare_parameter("name", std::string("initial"));
  rcl_interfaces::msg::ParameterDescriptor ro;
  ro.read_only = true;
  ro.description = "fixed";
  target->declare_parameter("fixed", 3.5, ro);

  m8::Spinner spinner(GetParam());
  spinner.add(target);
  spinner.start();

  // The synchronous client spins its own node internally (observer is not on
  // the spinner).
  auto client = std::make_shared<rclcpp::SyncParametersClient>(
    observer, target->get_fully_qualified_name());
  ASSERT_TRUE(client->wait_for_service(10s));

  auto values = client->get_parameters({"count", "name", "fixed"}, 10s);
  ASSERT_EQ(3u, values.size());
  EXPECT_EQ(1, values[0].as_int());
  EXPECT_EQ("initial", values[1].as_string());
  EXPECT_DOUBLE_EQ(3.5, values[2].as_double());

  auto results = client->set_parameters(
    {rclcpp::Parameter("count", 7), rclcpp::Parameter("name", "changed"),
      rclcpp::Parameter("fixed", 1.0)}, 10s);
  ASSERT_EQ(3u, results.size());
  EXPECT_TRUE(results[0].successful);
  EXPECT_TRUE(results[1].successful);
  EXPECT_FALSE(results[2].successful);  // read-only
  EXPECT_EQ(7, target->get_parameter("count").as_int());
  EXPECT_EQ("changed", target->get_parameter("name").as_string());

  auto listed = client->list_parameters({}, 0, 10s);
  for (const char * n : {"count", "name", "fixed"}) {
    EXPECT_NE(
      listed.names.end(), std::find(listed.names.begin(), listed.names.end(), n)) << n;
  }
  auto described = client->describe_parameters({"fixed"}, 10s);
  ASSERT_EQ(1u, described.size());
  EXPECT_TRUE(described[0].read_only);
  EXPECT_EQ("fixed", described[0].description);
  spinner.stop();
}

TEST_P(Parameters, SetCallbackRejectionReachesRemoteClient)
{
  target->declare_parameter("even", 2);
  auto handle = target->add_on_set_parameters_callback(
    [](const std::vector<rclcpp::Parameter> & params) {
      rcl_interfaces::msg::SetParametersResult r;
      r.successful = true;
      for (const auto & p : params) {
        if (p.get_name() == "even" && p.as_int() % 2 != 0) {
          r.successful = false;
          r.reason = "odd";
        }
      }
      return r;
    });
  m8::Spinner spinner(GetParam());
  spinner.add(target);
  spinner.start();
  auto client = std::make_shared<rclcpp::SyncParametersClient>(
    observer, target->get_fully_qualified_name());
  ASSERT_TRUE(client->wait_for_service(10s));
  auto ok = client->set_parameters({rclcpp::Parameter("even", 4)}, 10s);
  ASSERT_EQ(1u, ok.size());
  EXPECT_TRUE(ok[0].successful);
  auto bad = client->set_parameters({rclcpp::Parameter("even", 5)}, 10s);
  ASSERT_EQ(1u, bad.size());
  EXPECT_FALSE(bad[0].successful);
  EXPECT_EQ("odd", bad[0].reason);
  EXPECT_EQ(4, target->get_parameter("even").as_int());
  spinner.stop();
}

TEST_P(Parameters, ParameterEventsInOrder)
{
  // /parameter_events from the target node: new, changed (x3), new, in
  // publication order.
  std::mutex mutex;
  std::vector<ParameterEvent> events;
  const std::string target_name = target->get_fully_qualified_name();
  auto sub = observer->create_subscription<ParameterEvent>(
    "/parameter_events", rclcpp::ParameterEventsQoS(),
    [&](const ParameterEvent & e) {
      if (e.node == target_name) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(e);
      }
    });
  m8::Spinner spinner(GetParam());
  spinner.add(observer);
  spinner.start();
  // Each node has its own parameter event publisher on /parameter_events.
  ASSERT_TRUE(m8::wait_until([&] {return sub->get_publisher_count() >= 2u;}));

  rcl_interfaces::msg::ParameterDescriptor dynamic;
  dynamic.dynamic_typing = true;
  target->declare_parameter("p", 0, dynamic);
  for (int i = 1; i <= 3; ++i) {
    target->set_parameter(rclcpp::Parameter("p", i));
  }
  // (rclcpp's undeclare_parameter publishes no event, so the sequence ends
  // with a second declaration.)
  target->declare_parameter("q", std::string("last"));
  ASSERT_TRUE(
    m8::wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return events.size() >= 5u;
      }));
  spinner.stop();

  std::lock_guard<std::mutex> lock(mutex);
  ASSERT_EQ(5u, events.size());
  ASSERT_EQ(1u, events[0].new_parameters.size());
  EXPECT_EQ("p", events[0].new_parameters[0].name);
  EXPECT_EQ(0, events[0].new_parameters[0].value.integer_value);
  for (int i = 1; i <= 3; ++i) {
    const auto & e = events[static_cast<size_t>(i)];
    ASSERT_EQ(1u, e.changed_parameters.size());
    EXPECT_EQ(i, e.changed_parameters[0].value.integer_value);
  }
  ASSERT_EQ(1u, events[4].new_parameters.size());
  EXPECT_EQ("q", events[4].new_parameters[0].name);
  EXPECT_EQ("last", events[4].new_parameters[0].value.string_value);
}

TEST_P(Parameters, AsyncClientAndEventHandler)
{
  // AsyncParametersClient on a spun node, plus rclcpp's ParameterEventHandler
  // filtering events for one parameter.
  target->declare_parameter("speed", 1.0);
  std::mutex mutex;
  std::vector<double> seen;
  auto handler = std::make_shared<rclcpp::ParameterEventHandler>(observer);
  auto cb = handler->add_parameter_callback(
    "speed", [&](const rclcpp::Parameter & p) {
      std::lock_guard<std::mutex> lock(mutex);
      seen.push_back(p.as_double());
    }, target->get_fully_qualified_name());
  auto client = std::make_shared<rclcpp::AsyncParametersClient>(
    observer, target->get_fully_qualified_name());

  m8::Spinner spinner(GetParam());
  spinner.add(target);
  spinner.add(observer);
  spinner.start();
  ASSERT_TRUE(client->wait_for_service(10s));
  for (double v : {2.0, 3.0, 4.0}) {
    auto results = m8::set_parameters(*client, {rclcpp::Parameter("speed", v)});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(1u, results->size());
    EXPECT_TRUE(results->at(0).successful);
  }
  EXPECT_TRUE(
    m8::wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return seen.size() >= 3u;
      }));
  spinner.stop();
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ((std::vector<double>{2.0, 3.0, 4.0}), seen);
}

INSTANTIATE_TEST_SUITE_P(
  AllExecutors, Parameters,
  ::testing::Values(
    ExecutorKind::kSingleThreaded, ExecutorKind::kStaticSingleThreaded,
    ExecutorKind::kMultiThreaded, ExecutorKind::kEvents),
  m8::ExecutorKindName());
