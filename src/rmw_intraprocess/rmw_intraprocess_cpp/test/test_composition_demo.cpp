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

// M8 acceptance: a component container (rclcpp_components::ComponentManager,
// the class behind `component_container_mt`) in this process loads the
// standard ROS demo components through their component factories, by
// load_node service calls from another node in the same process, and runs
// them together:
//   composition::Talker / Listener / Server / Client,
//   demo_nodes_cpp::ParameterBlackboard,
//   action_tutorials_cpp::FibonacciActionServer / FibonacciActionClient.
// The test checks each one from outside (topics, services, parameters,
// actions), unloads one, and lets the action client end the demo: it calls
// rclcpp::shutdown() when its goal succeeds. Run against
// rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "action_msgs/msg/goal_status.hpp"
#include "action_msgs/msg/goal_status_array.hpp"
#include "action_tutorials_interfaces/action/fibonacci.hpp"
#include "composition_interfaces/srv/list_nodes.hpp"
#include "composition_interfaces/srv/load_node.hpp"
#include "composition_interfaces/srv/unload_node.hpp"
#include "example_interfaces/srv/add_two_ints.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_components/component_manager.hpp"
#include "std_msgs/msg/string.hpp"

#include "./rclcpp_test_utils.hpp"

using namespace std::chrono_literals;
using composition_interfaces::srv::ListNodes;
using composition_interfaces::srv::LoadNode;
using composition_interfaces::srv::UnloadNode;
using Fibonacci = action_tutorials_interfaces::action::Fibonacci;

namespace
{

constexpr char kContainer[] = "/m8_container";
constexpr char kActionTutorials[] = "action_tutorials_cpp";

class CompositionDemo : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
      rclcpp::ExecutorOptions(), 4);
    container = std::make_shared<rclcpp_components::ComponentManager>(
      executor, kContainer + 1);
    executor->add_node(container);
    tester = std::make_shared<rclcpp::Node>("m8_demo_tester");
    executor->add_node(tester);
    spin_thread = std::thread([this]() {executor->spin();});

    load = tester->create_client<LoadNode>(std::string(kContainer) + "/_container/load_node");
    unload = tester->create_client<UnloadNode>(std::string(kContainer) + "/_container/unload_node");
    list = tester->create_client<ListNodes>(std::string(kContainer) + "/_container/list_nodes");
    ASSERT_TRUE(load->wait_for_service(10s));
    ASSERT_TRUE(unload->wait_for_service(10s));
    ASSERT_TRUE(list->wait_for_service(10s));
  }

  void TearDown() override
  {
    // Normally the action client component has shut rclcpp down already.
    // spin() returns on shutdown of the context.
    rclcpp::shutdown();
    executor->cancel();
    spin_thread.join();
    // Entities of the test body die only after the executor threads are gone.
    keep_alive.clear();
    load.reset();
    unload.reset();
    list.reset();
    executor->remove_node(tester);
    executor->remove_node(container);
    tester.reset();
    container.reset();
    executor.reset();
  }

  /// Loads a component through the container's load_node service.
  std::optional<LoadNode::Response> load_component(
    const std::string & package, const std::string & plugin,
    const std::vector<rclcpp::Parameter> & parameters = {})
  {
    auto req = std::make_shared<LoadNode::Request>();
    req->package_name = package;
    req->plugin_name = plugin;
    for (const auto & p : parameters) {
      req->parameters.push_back(p.to_parameter_msg());
    }
    auto res = m8::call<LoadNode>(load, req, 30000ms);
    if (!res) {
      ADD_FAILURE() << "load_node timed out for " << plugin;
      return std::nullopt;
    }
    EXPECT_TRUE(res->success) << plugin << ": " << res->error_message;
    return res;
  }

  /// Keeps an entity created by the test body alive until TearDown has
  /// joined the executor threads (also on an early ASSERT return).
  template<class T>
  std::shared_ptr<T> keep(std::shared_ptr<T> entity)
  {
    keep_alive.push_back(entity);
    return entity;
  }

  std::vector<std::string> list_components()
  {
    auto res = m8::call<ListNodes>(list, std::make_shared<ListNodes::Request>());
    EXPECT_TRUE(res.has_value()) << "list_nodes timed out";
    return res ? res->full_node_names : std::vector<std::string>{};
  }

  std::shared_ptr<rclcpp::executors::MultiThreadedExecutor> executor;
  std::shared_ptr<rclcpp_components::ComponentManager> container;
  rclcpp::Node::SharedPtr tester;
  std::thread spin_thread;
  rclcpp::Client<LoadNode>::SharedPtr load;
  rclcpp::Client<UnloadNode>::SharedPtr unload;
  rclcpp::Client<ListNodes>::SharedPtr list;
  std::vector<std::shared_ptr<void>> keep_alive;
};

}  // namespace

TEST_F(CompositionDemo, StandardDemoComponentsRunTogether)
{
  // Observers first, so nothing published after a load is missed.
  std::atomic<int> chatter{0};
  auto chatter_sub = tester->create_subscription<std_msgs::msg::String>(
    "chatter", rclcpp::QoS(10), [&](const std_msgs::msg::String & m) {
      if (m.data.rfind("Hello World", 0) == 0) {
        ++chatter;
      }
    });
  keep(chatter_sub);
  std::mutex mutex;
  // Goal statuses published by the action server, except for our own goal.
  std::array<uint8_t, 16> own_goal{};
  std::vector<int8_t> final_statuses;
  auto status_sub = tester->create_subscription<action_msgs::msg::GoalStatusArray>(
    "fibonacci/_action/status", rclcpp::QoS(10).reliable().transient_local(),
    [&](const action_msgs::msg::GoalStatusArray & a) {
      std::lock_guard<std::mutex> lock(mutex);
      for (const auto & s : a.status_list) {
        if (s.goal_info.goal_id.uuid != own_goal) {
          final_statuses.push_back(s.status);
        }
      }
    });
  keep(status_sub);

  auto talker = load_component("composition", "composition::Talker");
  ASSERT_TRUE(talker && talker->success);
  ASSERT_TRUE(load_component("composition", "composition::Listener"));
  ASSERT_TRUE(load_component("composition", "composition::Server"));
  ASSERT_TRUE(load_component("composition", "composition::Client"));
  ASSERT_TRUE(
    load_component(
      "demo_nodes_cpp", "demo_nodes_cpp::ParameterBlackboard",
      {rclcpp::Parameter("loaded_with", 42)}));
  ASSERT_TRUE(load_component(kActionTutorials, "action_tutorials_cpp::FibonacciActionServer"));

  const auto names = list_components();
  EXPECT_EQ(6u, names.size());
  for (const char * n : {"/talker", "/listener", "/Server", "/Client", "/parameter_blackboard",
      "/fibonacci_action_server"})
  {
    EXPECT_NE(names.end(), std::find(names.begin(), names.end(), n)) << n;
  }

  // Talker -> chatter (also received by the Listener component).
  EXPECT_TRUE(m8::wait_until([&] {return chatter.load() >= 2;}, 15000ms));

  // Server component answers add_two_ints.
  auto adder = tester->create_client<example_interfaces::srv::AddTwoInts>("add_two_ints");
  keep(adder);
  ASSERT_TRUE(adder->wait_for_service(10s));
  auto sum_req = std::make_shared<example_interfaces::srv::AddTwoInts::Request>();
  sum_req->a = 40;
  sum_req->b = 2;
  auto sum = m8::call<example_interfaces::srv::AddTwoInts>(adder, sum_req);
  ASSERT_TRUE(sum.has_value());
  EXPECT_EQ(42, sum->sum);

  // ParameterBlackboard got the load_node parameter and accepts new ones.
  auto params = std::make_shared<rclcpp::AsyncParametersClient>(tester, "/parameter_blackboard");
  keep(params);
  ASSERT_TRUE(params->wait_for_service(10s));
  auto set = m8::set_parameters(*params, {rclcpp::Parameter("written", std::string("by test"))});
  ASSERT_TRUE(set.has_value());
  ASSERT_EQ(1u, set->size());
  EXPECT_TRUE(set->at(0).successful);
  auto got = m8::get_parameters(*params, {"loaded_with", "written"});
  ASSERT_TRUE(got.has_value());
  ASSERT_EQ(2u, got->size());
  EXPECT_EQ(42, got->at(0).as_int());
  EXPECT_EQ("by test", got->at(1).as_string());

  // The Fibonacci action server component serves our own goal.
  auto action_client = rclcpp_action::create_client<Fibonacci>(tester, "fibonacci");
  keep(action_client);
  ASSERT_TRUE(action_client->wait_for_action_server(10s));
  Fibonacci::Goal goal;
  goal.order = 3;
  auto goal_future = action_client->async_send_goal(goal);
  ASSERT_EQ(std::future_status::ready, goal_future.wait_for(10s));
  ASSERT_NE(nullptr, goal_future.get());
  {
    std::lock_guard<std::mutex> lock(mutex);
    own_goal = goal_future.get()->get_goal_id();
  }
  auto result_future = action_client->async_get_result(goal_future.get());
  ASSERT_EQ(std::future_status::ready, result_future.wait_for(30s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, result_future.get().code);
  EXPECT_EQ((std::vector<int32_t>{0, 1, 1, 2}), result_future.get().result->sequence);

  // Unload the talker: its publisher leaves the graph.
  auto unload_req = std::make_shared<UnloadNode::Request>();
  unload_req->unique_id = talker->unique_id;
  auto unloaded = m8::call<UnloadNode>(unload, unload_req);
  ASSERT_TRUE(unloaded.has_value());
  EXPECT_TRUE(unloaded->success) << unloaded->error_message;
  EXPECT_TRUE(m8::wait_until([&] {return chatter_sub->get_publisher_count() == 0u;}));
  EXPECT_EQ(5u, list_components().size());

  // Last, the action client component: it sends its own goal (order 10) and
  // calls rclcpp::shutdown() when the result arrives, which ends the demo.
  {
    std::lock_guard<std::mutex> lock(mutex);
    final_statuses.clear();
  }
  ASSERT_TRUE(load_component(kActionTutorials, "action_tutorials_cpp::FibonacciActionClient"));
  EXPECT_TRUE(m8::wait_until([] {return !rclcpp::ok();}, 60000ms))
    << "the action client component did not finish its goal";
  // The client component calls rclcpp::shutdown() only after a SUCCEEDED
  // result (its other result branches return first), so the shutdown above is
  // the success evidence. The server's final SUCCEEDED status can still be in
  // flight when the executor stops (seen with rmw_fastrtps_cpp on Windows),
  // so only require that the server reported the goal at all (it executes for
  // about 10 s before it succeeds).
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_FALSE(final_statuses.empty()) << "no status for the client component's goal";
}
