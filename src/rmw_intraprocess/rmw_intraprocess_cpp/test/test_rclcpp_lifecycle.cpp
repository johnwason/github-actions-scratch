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

// M8: a lifecycle node driven through its lifecycle services by a client
// node in the same process (as a lifecycle manager does), with transition
// events and a lifecycle publisher, under every executor type. Run against
// rmw_intraprocess_cpp and the reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "lifecycle_msgs/msg/transition_event.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_available_transitions.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "./rclcpp_test_utils.hpp"

using namespace std::chrono_literals;
using lifecycle_msgs::msg::State;
using lifecycle_msgs::msg::Transition;
using m8::ExecutorKind;
using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

namespace
{

/// A managed node with a lifecycle publisher; on_configure fails once when
/// fail_next_configure is set.
class ManagedTalker : public rclcpp_lifecycle::LifecycleNode
{
public:
  ManagedTalker(const std::string & name, const std::string & ns, const std::string & topic)
  : rclcpp_lifecycle::LifecycleNode(name, ns), topic_(topic) {}

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    if (fail_next_configure.exchange(false)) {
      return CallbackReturn::FAILURE;
    }
    pub = create_publisher<test_msgs::msg::BasicTypes>(topic_, rclcpp::QoS(100).reliable());
    return CallbackReturn::SUCCESS;
  }
  CallbackReturn on_activate(const rclcpp_lifecycle::State & s) override
  {
    return rclcpp_lifecycle::LifecycleNode::on_activate(s);  // activates pub
  }
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & s) override
  {
    return rclcpp_lifecycle::LifecycleNode::on_deactivate(s);
  }
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    pub.reset();
    return CallbackReturn::SUCCESS;
  }
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override
  {
    pub.reset();
    return CallbackReturn::SUCCESS;
  }

  std::atomic<bool> fail_next_configure{false};
  rclcpp_lifecycle::LifecyclePublisher<test_msgs::msg::BasicTypes>::SharedPtr pub;

private:
  std::string topic_;
};

class Lifecycle : public ::testing::TestWithParam<ExecutorKind>
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    ns = "/" + m8::unique_name("m8_lifecycle");
    topic = ns + "/data";
    managed = std::make_shared<ManagedTalker>("talker", ns, topic);
    manager = std::make_shared<rclcpp::Node>("manager", ns);
    const std::string base = managed->get_fully_qualified_name();
    change_state = manager->create_client<lifecycle_msgs::srv::ChangeState>(base + "/change_state");
    get_state = manager->create_client<lifecycle_msgs::srv::GetState>(base + "/get_state");
    available = manager->create_client<lifecycle_msgs::srv::GetAvailableTransitions>(
      base + "/get_available_transitions");
    transition_events = manager->create_subscription<lifecycle_msgs::msg::TransitionEvent>(
      base + "/transition_event", rclcpp::QoS(100).reliable(),
      [this](const lifecycle_msgs::msg::TransitionEvent & e) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(e);
      });
    data = manager->create_subscription<test_msgs::msg::BasicTypes>(
      topic, rclcpp::QoS(100).reliable(),
      [this](const test_msgs::msg::BasicTypes & m) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(m.int32_value);
      });
    spinner = std::make_unique<m8::Spinner>(GetParam());
    spinner->add(managed);
    spinner->add(manager);
    spinner->start();
    ASSERT_TRUE(change_state->wait_for_service(10s));
    ASSERT_TRUE(get_state->wait_for_service(10s));
    ASSERT_TRUE(available->wait_for_service(10s));
  }

  void TearDown() override {spinner->stop();}

  bool transition(uint8_t id)
  {
    auto req = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
    req->transition.id = id;
    auto res = m8::call<lifecycle_msgs::srv::ChangeState>(change_state, req);
    EXPECT_TRUE(res.has_value()) << "change_state timed out";
    return res.has_value() && res->success;
  }

  uint8_t state()
  {
    auto req = std::make_shared<lifecycle_msgs::srv::GetState::Request>();
    auto res = m8::call<lifecycle_msgs::srv::GetState>(get_state, req);
    EXPECT_TRUE(res.has_value()) << "get_state timed out";
    return res.has_value() ? res->current_state.id : State::PRIMARY_STATE_UNKNOWN;
  }

  void publish(int32_t v)
  {
    test_msgs::msg::BasicTypes m;
    m.int32_value = v;
    managed->pub->publish(m);
  }

  std::string ns;
  std::string topic;
  std::shared_ptr<ManagedTalker> managed;
  rclcpp::Node::SharedPtr manager;
  rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedPtr change_state;
  rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr get_state;
  rclcpp::Client<lifecycle_msgs::srv::GetAvailableTransitions>::SharedPtr available;
  rclcpp::Subscription<lifecycle_msgs::msg::TransitionEvent>::SharedPtr transition_events;
  rclcpp::Subscription<test_msgs::msg::BasicTypes>::SharedPtr data;
  std::mutex mutex;
  std::vector<lifecycle_msgs::msg::TransitionEvent> events;
  std::vector<int32_t> received;
  std::unique_ptr<m8::Spinner> spinner;
};

}  // namespace

TEST_P(Lifecycle, FullCycleThroughServices)
{
  EXPECT_EQ(State::PRIMARY_STATE_UNCONFIGURED, state());
  ASSERT_TRUE(transition(Transition::TRANSITION_CONFIGURE));
  EXPECT_EQ(State::PRIMARY_STATE_INACTIVE, state());
  ASSERT_TRUE(
    m8::wait_until([&] {return managed->pub->get_subscription_count() == 1u;}));

  // Inactive: the lifecycle publisher drops messages.
  publish(1);
  ASSERT_TRUE(transition(Transition::TRANSITION_ACTIVATE));
  EXPECT_EQ(State::PRIMARY_STATE_ACTIVE, state());
  publish(2);
  publish(3);
  ASSERT_TRUE(
    m8::wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return received.size() >= 2u;
      }));
  ASSERT_TRUE(transition(Transition::TRANSITION_DEACTIVATE));
  publish(4);
  ASSERT_TRUE(transition(Transition::TRANSITION_CLEANUP));
  EXPECT_EQ(State::PRIMARY_STATE_UNCONFIGURED, state());
  ASSERT_TRUE(transition(Transition::TRANSITION_UNCONFIGURED_SHUTDOWN));
  EXPECT_EQ(State::PRIMARY_STATE_FINALIZED, state());

  // configure, activate, deactivate, cleanup, shutdown: two events each
  // (start -> transition state, transition state -> goal).
  ASSERT_TRUE(
    m8::wait_until(
      [&] {
        std::lock_guard<std::mutex> lock(mutex);
        return events.size() >= 10u;
      }));
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ((std::vector<int32_t>{2, 3}), received);
  ASSERT_EQ(10u, events.size());
  EXPECT_EQ(State::PRIMARY_STATE_UNCONFIGURED, events.front().start_state.id);
  EXPECT_EQ(State::TRANSITION_STATE_CONFIGURING, events.front().goal_state.id);
  EXPECT_EQ(State::PRIMARY_STATE_FINALIZED, events.back().goal_state.id);
  for (size_t i = 1; i < events.size(); ++i) {
    EXPECT_EQ(events[i - 1].goal_state.id, events[i].start_state.id) << i;
  }
}

TEST_P(Lifecycle, FailedTransitionAndAvailableTransitions)
{
  managed->fail_next_configure = true;
  EXPECT_FALSE(transition(Transition::TRANSITION_CONFIGURE));
  EXPECT_EQ(State::PRIMARY_STATE_UNCONFIGURED, state());
  // An invalid transition from this state is refused.
  EXPECT_FALSE(transition(Transition::TRANSITION_ACTIVATE));

  const auto response = m8::call<lifecycle_msgs::srv::GetAvailableTransitions>(
    available, std::make_shared<lifecycle_msgs::srv::GetAvailableTransitions::Request>());
  ASSERT_TRUE(response.has_value());
  std::vector<uint8_t> ids;
  for (const auto & t : response->available_transitions) {
    ids.push_back(t.transition.id);
  }
  EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), Transition::TRANSITION_CONFIGURE));
  EXPECT_EQ(ids.end(), std::find(ids.begin(), ids.end(), Transition::TRANSITION_ACTIVATE));

  ASSERT_TRUE(transition(Transition::TRANSITION_CONFIGURE));
  EXPECT_EQ(State::PRIMARY_STATE_INACTIVE, state());
}

INSTANTIATE_TEST_SUITE_P(
  AllExecutors, Lifecycle,
  ::testing::Values(
    ExecutorKind::kSingleThreaded, ExecutorKind::kStaticSingleThreaded,
    ExecutorKind::kMultiThreaded, ExecutorKind::kEvents),
  m8::ExecutorKindName());
