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

// M8: rclcpp_action client and server in one process (goal, feedback,
// result, cancel, reject, concurrent goals), under every executor type. Run
// against rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "test_msgs/action/fibonacci.hpp"

#include "./rclcpp_test_utils.hpp"

using namespace std::chrono_literals;
using m8::ExecutorKind;
using Fibonacci = test_msgs::action::Fibonacci;
using GoalHandle = rclcpp_action::ServerGoalHandle<Fibonacci>;
using ClientGoalHandle = rclcpp_action::ClientGoalHandle<Fibonacci>;

namespace
{

std::vector<int32_t> fibonacci(int32_t order)
{
  std::vector<int32_t> seq{0, 1};
  for (int32_t i = 1; i < order; ++i) {
    seq.push_back(seq[static_cast<size_t>(i)] + seq[static_cast<size_t>(i - 1)]);
  }
  return seq;
}

/// A Fibonacci server: rejects negative orders, executes each goal on its own
/// thread with one feedback per step, and honours cancel requests.
class FibonacciServer
{
public:
  FibonacciServer(const rclcpp::Node::SharedPtr & node, const std::string & name)
  {
    server_ = rclcpp_action::create_server<Fibonacci>(
      node, name,
      [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Fibonacci::Goal> goal) {
        if (goal->order < 0) {
          return rclcpp_action::GoalResponse::REJECT;
        }
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<GoalHandle>) {return rclcpp_action::CancelResponse::ACCEPT;},
      [this](const std::shared_ptr<GoalHandle> handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_.emplace_back([this, handle]() {execute(handle);});
      });
  }

  ~FibonacciServer()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto & t : threads_) {
      t.join();
    }
  }

  /// Delay between steps (cancel tests use a slow server).
  std::chrono::milliseconds step{1};

private:
  void execute(const std::shared_ptr<GoalHandle> & handle)
  {
    const auto goal = handle->get_goal();
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    auto result = std::make_shared<Fibonacci::Result>();
    feedback->sequence = {0, 1};
    for (int32_t i = 1; i < goal->order; ++i) {
      if (handle->is_canceling()) {
        result->sequence = feedback->sequence;
        handle->canceled(result);
        return;
      }
      const auto & seq = feedback->sequence;
      const int32_t next = seq[static_cast<size_t>(i)] + seq[static_cast<size_t>(i - 1)];
      feedback->sequence.push_back(next);
      handle->publish_feedback(feedback);
      std::this_thread::sleep_for(step);
    }
    result->sequence = feedback->sequence;
    handle->succeed(result);
  }

  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
  std::mutex mutex_;
  std::vector<std::thread> threads_;
};

class Actions : public ::testing::TestWithParam<ExecutorKind>
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    action_name = m8::unique_name("m8_fibonacci");
    server_node = std::make_shared<rclcpp::Node>("action_server", "/m8_actions");
    client_node = std::make_shared<rclcpp::Node>("action_client", "/m8_actions");
    server = std::make_unique<FibonacciServer>(server_node, action_name);
    client = rclcpp_action::create_client<Fibonacci>(client_node, action_name);
    spinner = std::make_unique<m8::Spinner>(GetParam());
    spinner->add(server_node);
    spinner->add(client_node);
    spinner->start();
    ASSERT_TRUE(client->wait_for_action_server(10s));
  }

  void TearDown() override
  {
    spinner->stop();
    client.reset();
    server.reset();
  }

  /// Waits for a std::shared_future without spinning (the spinner does).
  template<class T>
  static bool ready(const std::shared_future<T> & f, std::chrono::seconds timeout = 15s)
  {
    return f.wait_for(timeout) == std::future_status::ready;
  }

  std::string action_name;
  rclcpp::Node::SharedPtr server_node;
  rclcpp::Node::SharedPtr client_node;
  std::unique_ptr<FibonacciServer> server;
  rclcpp_action::Client<Fibonacci>::SharedPtr client;
  std::unique_ptr<m8::Spinner> spinner;
};

}  // namespace

TEST_P(Actions, GoalFeedbackResult)
{
  std::mutex mutex;
  std::vector<std::vector<int32_t>> feedback;
  rclcpp_action::Client<Fibonacci>::SendGoalOptions options;
  options.feedback_callback =
    [&](ClientGoalHandle::SharedPtr, const std::shared_ptr<const Fibonacci::Feedback> fb) {
      std::lock_guard<std::mutex> lock(mutex);
      feedback.push_back(fb->sequence);
    };
  Fibonacci::Goal goal;
  goal.order = 10;
  auto goal_future = client->async_send_goal(goal, options);
  ASSERT_TRUE(ready(goal_future));
  auto handle = goal_future.get();
  ASSERT_NE(nullptr, handle);
  auto result_future = client->async_get_result(handle);
  ASSERT_TRUE(ready(result_future));
  const auto result = result_future.get();
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, result.code);
  EXPECT_EQ(fibonacci(10), result.result->sequence);
  // Feedback is published before the result on the same server; every
  // feedback message that arrives is a prefix of the result and in order.
  std::lock_guard<std::mutex> lock(mutex);
  size_t last = 0;
  for (const auto & seq : feedback) {
    EXPECT_GT(seq.size(), last);
    last = seq.size();
    EXPECT_TRUE(std::equal(seq.begin(), seq.end(), result.result->sequence.begin()));
  }
}

TEST_P(Actions, GoalIsRejected)
{
  Fibonacci::Goal goal;
  goal.order = -1;
  auto goal_future = client->async_send_goal(goal);
  ASSERT_TRUE(ready(goal_future));
  EXPECT_EQ(nullptr, goal_future.get());
}

TEST_P(Actions, CancelRunningGoal)
{
  server->step = 50ms;
  Fibonacci::Goal goal;
  goal.order = 1000;
  auto goal_future = client->async_send_goal(goal);
  ASSERT_TRUE(ready(goal_future));
  auto handle = goal_future.get();
  ASSERT_NE(nullptr, handle);
  auto result_future = client->async_get_result(handle);
  auto cancel_future = client->async_cancel_goal(handle);
  ASSERT_TRUE(ready(cancel_future));
  EXPECT_EQ(action_msgs::srv::CancelGoal::Response::ERROR_NONE, cancel_future.get()->return_code);
  ASSERT_TRUE(ready(result_future));
  EXPECT_EQ(rclcpp_action::ResultCode::CANCELED, result_future.get().code);
}

TEST_P(Actions, ConcurrentGoals)
{
  constexpr int kGoals = 10;
  std::vector<std::shared_future<ClientGoalHandle::SharedPtr>> goals;
  for (int i = 0; i < kGoals; ++i) {
    Fibonacci::Goal goal;
    goal.order = 5 + i;
    goals.push_back(client->async_send_goal(goal));
  }
  std::vector<std::shared_future<ClientGoalHandle::WrappedResult>> results;
  for (auto & g : goals) {
    ASSERT_TRUE(ready(g));
    ASSERT_NE(nullptr, g.get());
    results.push_back(client->async_get_result(g.get()));
  }
  for (int i = 0; i < kGoals; ++i) {
    auto & r = results[static_cast<size_t>(i)];
    ASSERT_TRUE(ready(r));
    EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, r.get().code);
    EXPECT_EQ(fibonacci(5 + i), r.get().result->sequence);
  }
}

INSTANTIATE_TEST_SUITE_P(
  AllExecutors, Actions,
  ::testing::Values(
    ExecutorKind::kSingleThreaded, ExecutorKind::kStaticSingleThreaded,
    ExecutorKind::kMultiThreaded, ExecutorKind::kEvents),
  m8::ExecutorKindName());
