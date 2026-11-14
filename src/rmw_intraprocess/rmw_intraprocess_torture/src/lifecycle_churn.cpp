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

// LifecycleChurn: a lifecycle node (managed side) that, when configured,
// owns a lifecycle publisher and a timer publishing on it while active; and,
// when `manage` names another LifecycleChurn, a manager thread that drives
// that node through configure -> activate -> deactivate -> cleanup in a loop
// over its lifecycle services (as a lifecycle manager does), checking every
// transition result, the state after it, and that the target's transition
// events form a chain (each starts where the previous one ended).
//
// Parameters:
//   manage (string, ""): fully qualified name of the node to manage
//   cycles (int, 50): full cycles; 0 = until stopped
//   period_ms (int, 1): publish period while active
//
// Stats: transitions, transition_failed, state_mismatch, events,
// event_chain_broken, cycles_done, managers / managers_done (manager);
// published, configured (managed).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "lifecycle_msgs/msg/transition.hpp"
#include "lifecycle_msgs/msg/transition_event.hpp"
#include "lifecycle_msgs/srv/change_state.hpp"
#include "lifecycle_msgs/srv/get_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

using lifecycle_msgs::msg::State;
using lifecycle_msgs::msg::Transition;
using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class LifecycleChurn : public TortureNodeT<rclcpp_lifecycle::LifecycleNode>
{
public:
  explicit LifecycleChurn(const rclcpp::NodeOptions & options)
  : TortureNodeT<rclcpp_lifecycle::LifecycleNode>("lifecycle_churn", options)
  {
    target_ = declare_parameter("manage", std::string(""));
    cycles_ = declare_parameter("cycles", 50);
    period_ms_ = declare_parameter("period_ms", 1);
    if (!target_.empty()) {
      change_state_ = create_client<lifecycle_msgs::srv::ChangeState>(target_ + "/change_state");
      get_state_ = create_client<lifecycle_msgs::srv::GetState>(target_ + "/get_state");
      events_ = create_subscription<lifecycle_msgs::msg::TransitionEvent>(
        target_ + "/transition_event", rclcpp::QoS(1000).reliable(),
        [this](const lifecycle_msgs::msg::TransitionEvent & e) {on_event(e);});
      set("managers", 1);
      thread_ = std::thread(
        [this]() {
          manage();
          add("managers_done");
        });
    }
  }

  ~LifecycleChurn() override
  {
    abort_ = true;
    stop_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State &) override
  {
    pub_ = create_publisher<test_msgs::msg::BasicTypes>("~/data", 10);
    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms_), [this]() {
        if (pub_ && pub_->is_activated()) {
          pub_->publish(test_msgs::msg::BasicTypes());
          add("published");
        }
      });
    add("configured");
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State &) override
  {
    timer_.reset();
    pub_.reset();
    return CallbackReturn::SUCCESS;
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State &) override
  {
    timer_.reset();
    pub_.reset();
    return CallbackReturn::SUCCESS;
  }

protected:
  void on_stop() override {stop_ = true;}

private:
  template<class ServiceT>
  std::optional<typename ServiceT::Response> call(
    const typename rclcpp::Client<ServiceT>::SharedPtr & client,
    const typename ServiceT::Request::SharedPtr & req)
  {
    struct Box
    {
      std::mutex mutex;
      std::condition_variable cv;
      std::optional<typename ServiceT::Response> value;
    };
    auto box = std::make_shared<Box>();
    client->async_send_request(
      req, [box](typename rclcpp::Client<ServiceT>::SharedFuture f) {
        {
          std::lock_guard<std::mutex> lock(box->mutex);
          box->value = *f.get();
        }
        box->cv.notify_all();
      });
    std::unique_lock<std::mutex> lock(box->mutex);
    wait_or_stop(
      box->cv, lock, std::chrono::seconds(60), abort_, [&box] {return box->value.has_value();});
    return box->value;
  }

  bool transition(uint8_t id, uint8_t expected_state)
  {
    auto req = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
    req->transition.id = id;
    auto res = call<lifecycle_msgs::srv::ChangeState>(change_state_, req);
    if (abort_) {
      return false;  // torn down: no reply will come
    }
    add("transitions");
    if (!res || !res->success) {
      add("transition_failed");
      error("transition " + std::to_string(id) + (res ? " failed" : " timed out"));
      return false;
    }
    auto state = call<lifecycle_msgs::srv::GetState>(
      get_state_, std::make_shared<lifecycle_msgs::srv::GetState::Request>());
    if (abort_) {
      return false;
    }
    if (!state || state->current_state.id != expected_state) {
      add("state_mismatch");
      error("state after transition " + std::to_string(id) + " is not " +
        std::to_string(expected_state));
      return false;
    }
    return true;
  }

  void manage()
  {
    try {
      while (!stop_ && !change_state_->wait_for_service(std::chrono::milliseconds(100))) {
      }
      while (!stop_ && !get_state_->wait_for_service(std::chrono::milliseconds(100))) {
      }
      for (int64_t c = 0; !stop_ && (cycles_ == 0 || c < cycles_); ++c) {
        if (!transition(Transition::TRANSITION_CONFIGURE, State::PRIMARY_STATE_INACTIVE) ||
          !transition(Transition::TRANSITION_ACTIVATE, State::PRIMARY_STATE_ACTIVE) ||
          !transition(Transition::TRANSITION_DEACTIVATE, State::PRIMARY_STATE_INACTIVE) ||
          !transition(Transition::TRANSITION_CLEANUP, State::PRIMARY_STATE_UNCONFIGURED))
        {
          return;
        }
        add("cycles_done");
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("manager: ") + e.what());
      }
    }
  }

  void on_event(const lifecycle_msgs::msg::TransitionEvent & e)
  {
    std::lock_guard<std::mutex> lock(event_mutex_);
    add("events");
    if (last_goal_ && *last_goal_ != e.start_state.id) {
      add("event_chain_broken");
      error("transition event starts at " + std::to_string(e.start_state.id) +
        ", previous ended at " + std::to_string(*last_goal_));
    }
    last_goal_ = e.goal_state.id;
  }

  std::string target_;
  int64_t cycles_ = 50;
  int64_t period_ms_ = 1;
  rclcpp_lifecycle::LifecyclePublisher<test_msgs::msg::BasicTypes>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedPtr change_state_;
  rclcpp::Client<lifecycle_msgs::srv::GetState>::SharedPtr get_state_;
  rclcpp::Subscription<lifecycle_msgs::msg::TransitionEvent>::SharedPtr events_;
  std::mutex event_mutex_;
  std::optional<uint8_t> last_goal_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::LifecycleChurn)
