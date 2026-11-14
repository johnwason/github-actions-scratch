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

// rclcpp-level service tests, run against rmw_intraprocess_cpp and the
// reference rmw_fastrtps_cpp (registered twice in CMakeLists.txt). Nodes use
// default NodeOptions (parameter and type description services enabled).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "test_msgs/srv/basic_types.hpp"

using namespace std::chrono_literals;
using test_msgs::srv::BasicTypes;

namespace
{

class RclcppServices : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  std::string service_name() const
  {
    return std::string("m4_") + ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }

  /// "add two ints" on BasicTypes: response.int64 = request.int32 + request.int64.
  rclcpp::Service<BasicTypes>::SharedPtr make_adder(
    const rclcpp::Node::SharedPtr & node, rclcpp::CallbackGroup::SharedPtr group = nullptr,
    const rclcpp::QoS & qos = rclcpp::ServicesQoS())
  {
    return node->create_service<BasicTypes>(
      service_name(),
      [](const std::shared_ptr<BasicTypes::Request> req,
      std::shared_ptr<BasicTypes::Response> res) {
        res->int64_value = req->int32_value + req->int64_value;
      }, qos, group);
  }
};

}  // namespace

TEST_F(RclcppServices, AddTwoIntsSingleThreaded)
{
  auto server_node = std::make_shared<rclcpp::Node>("server", "/m4");
  auto client_node = std::make_shared<rclcpp::Node>("client", "/m4");
  auto service = make_adder(server_node);
  auto client = client_node->create_client<BasicTypes>(service_name());
  ASSERT_TRUE(client->wait_for_service(5s));

  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(server_node);
  exec.add_node(client_node);
  for (int i = 0; i < 100; ++i) {
    auto req = std::make_shared<BasicTypes::Request>();
    req->int32_value = i;
    req->int64_value = 1000;
    auto future = client->async_send_request(req);
    ASSERT_EQ(rclcpp::FutureReturnCode::SUCCESS, exec.spin_until_future_complete(future, 5s));
    EXPECT_EQ(1000 + i, future.get()->int64_value);
  }
}

TEST_F(RclcppServices, ConcurrentClientsMultiThreaded)
{
  auto server_node = std::make_shared<rclcpp::Node>("server", "/m4");
  // Nodes hold callback groups weakly: keep the group alive for the test.
  auto reentrant = server_node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  // 1000 requests are sent in a burst: KEEP_ALL on both sides, otherwise the
  // default KEEP_LAST(10) service QoS drops requests (on any RMW).
  const auto qos = rclcpp::ServicesQoS().keep_all();
  auto service = make_adder(server_node, reentrant, qos);
  constexpr int kClients = 4;
  constexpr int kEach = 250;
  std::vector<rclcpp::Node::SharedPtr> client_nodes;
  std::vector<rclcpp::Client<BasicTypes>::SharedPtr> clients;
  rclcpp::executors::MultiThreadedExecutor exec(rclcpp::ExecutorOptions(), 8);
  exec.add_node(server_node);
  for (int c = 0; c < kClients; ++c) {
    client_nodes.push_back(
      std::make_shared<rclcpp::Node>("client" + std::to_string(c), "/m4"));
    clients.push_back(client_nodes.back()->create_client<BasicTypes>(service_name(), qos));
    exec.add_node(client_nodes.back());
  }
  std::thread spinner([&exec] {exec.spin();});
  for (auto & cl : clients) {
    ASSERT_TRUE(cl->wait_for_service(5s));
  }

  // Each caller thread sends its requests with a response callback (run by the
  // executor); the main thread waits for all responses with a deadline.
  std::atomic<int> correct{0};
  std::atomic<int> done{0};
  std::vector<std::thread> callers;
  for (int c = 0; c < kClients; ++c) {
    callers.emplace_back(
      [&, c] {
        for (int i = 0; i < kEach; ++i) {
          auto req = std::make_shared<BasicTypes::Request>();
          req->int32_value = c;
          req->int64_value = i;
          const int64_t expected = c + i;
          clients[static_cast<std::size_t>(c)]->async_send_request(
            req, [&, expected](rclcpp::Client<BasicTypes>::SharedFuture f) {
              if (f.get()->int64_value == expected) {
                ++correct;
              }
              ++done;
            });
        }
      });
  }
  for (auto & t : callers) {
    t.join();
  }
  const auto end = std::chrono::steady_clock::now() + 30s;
  while (done.load() < kClients * kEach && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(10ms);
  }
  exec.cancel();
  spinner.join();
  EXPECT_EQ(kClients * kEach, done.load());
  EXPECT_EQ(kClients * kEach, correct.load());
}

TEST_F(RclcppServices, WaitForServiceFollowsServerLifetime)
{
  auto node = std::make_shared<rclcpp::Node>("waiter", "/m4");
  auto client = node->create_client<BasicTypes>(service_name());
  EXPECT_FALSE(client->wait_for_service(100ms));
  auto server_node = std::make_shared<rclcpp::Node>("late_server", "/m4");
  auto service = make_adder(server_node);
  EXPECT_TRUE(client->wait_for_service(5s));
  service.reset();
  const auto end = std::chrono::steady_clock::now() + 5s;
  while (client->service_is_ready() && std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_FALSE(client->service_is_ready());
}

TEST_F(RclcppServices, ParametersOverServicesWithDefaultNodeOptions)
{
  auto target = std::make_shared<rclcpp::Node>("param_target", "/m4");
  target->declare_parameter("answer", 41);
  auto caller = std::make_shared<rclcpp::Node>("param_caller", "/m4");
  auto params = std::make_shared<rclcpp::AsyncParametersClient>(caller, "/m4/param_target");
  ASSERT_TRUE(params->wait_for_service(5s));
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(target);
  exec.add_node(caller);
  auto set = params->set_parameters({rclcpp::Parameter("answer", 42)});
  ASSERT_EQ(rclcpp::FutureReturnCode::SUCCESS, exec.spin_until_future_complete(set, 5s));
  ASSERT_EQ(1u, set.get().size());
  EXPECT_TRUE(set.get()[0].successful);
  auto get = params->get_parameters({"answer"});
  ASSERT_EQ(rclcpp::FutureReturnCode::SUCCESS, exec.spin_until_future_complete(get, 5s));
  ASSERT_EQ(1u, get.get().size());
  EXPECT_EQ(42, get.get()[0].as_int());
}
