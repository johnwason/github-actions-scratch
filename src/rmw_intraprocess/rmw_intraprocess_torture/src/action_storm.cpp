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

// ActionStorm: a Fibonacci action server whose goals run on a worker pool
// (feedback every step), client threads sending goals back to back and
// cancelling some at random, and a churn thread whose action clients (on a
// private node) are destroyed with goals in flight.
//
// Parameters:
//   action (string, "action_storm"), serve (bool, true), call (bool, true)
//   clients (int, 3), goals (int, 50): per client; 0 = until stopped
//   workers (int, 2): server worker threads; max_order (int, 20)
//   cancel_percent (int, 20), churn (bool, true), seed (int, 1)
//
// Stats: goals_sent, accepted, rejected, succeeded, canceled, aborted,
// feedback, bad_result, bad_feedback, timeouts_goal (clients); server_accepted,
// server_finished, server_canceled (server); churn_clients. Server side:
// server_finished == server_accepted once drained (every accepted goal
// reaches a terminal state, including goals of clients that are gone).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/exceptions.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "test_msgs/action/fibonacci.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

using Fibonacci = test_msgs::action::Fibonacci;
using ServerGoal = rclcpp_action::ServerGoalHandle<Fibonacci>;
using ClientGoal = rclcpp_action::ClientGoalHandle<Fibonacci>;

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
}  // namespace

class ActionStorm : public TortureNode
{
public:
  explicit ActionStorm(const rclcpp::NodeOptions & options)
  : TortureNode("action_storm", options)
  {
    action_ = declare_parameter("action", std::string("action_storm"));
    const bool serve = declare_parameter("serve", true);
    const bool call = declare_parameter("call", true);
    const auto clients = declare_parameter("clients", 3);
    goals_ = declare_parameter("goals", 50);
    const auto workers = declare_parameter("workers", 2);
    max_order_ = declare_parameter("max_order", 20);
    cancel_percent_ = declare_parameter("cancel_percent", 20);
    const bool churn = declare_parameter("churn", true);
    churn_iterations_ = declare_parameter("churn_iterations", 200);
    seed_ = declare_parameter("seed", 1);

    if (serve) {
      server_ = rclcpp_action::create_server<Fibonacci>(
        this, action_,
        [this](const rclcpp_action::GoalUUID &, std::shared_ptr<const Fibonacci::Goal> goal) {
          // Churned clients send order 1; real clients order >= 2.
          add(goal->order == 1 ? "server_churn_goal_requests" : "server_goal_requests");
          if (goal->order < 0) {
            return rclcpp_action::GoalResponse::REJECT;
          }
          return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
        },
        [](const std::shared_ptr<ServerGoal>) {return rclcpp_action::CancelResponse::ACCEPT;},
        [this](const std::shared_ptr<ServerGoal> handle) {
          add("server_accepted");
          {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push_back(handle);
          }
          queue_cv_.notify_one();
        },
        server_options(), group_);
      for (int64_t i = 0; i < workers; ++i) {
        workers_.emplace_back([this]() {work();});
      }
    }
    if (call) {
      for (int64_t i = 0; i < clients; ++i) {
        auto client =
          rclcpp_action::create_client<Fibonacci>(this, action_, client_group_, client_options());
        clients_.push_back(client);
        threads_.emplace_back([this, client, i]() {call_loop(client, i);});
      }
      if (churn) {
        set("churns", 1);
        threads_.emplace_back(
          [this]() {
            churn_loop();
            add("churn_done");
          });
      }
    }
  }

  ~ActionStorm() override
  {
    abort_ = true;
    stop_ = true;
    for (auto & t : threads_) {
      t.join();
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      workers_stop_ = true;
    }
    queue_cv_.notify_all();
    for (auto & t : workers_) {
      t.join();
    }
  }

protected:
  void on_stop() override {stop_ = true;}

private:
  // Goal/cancel/result service queues deep enough for every request in flight.
  // rcl_action's default (rmw_qos_profile_services_default, KEEP_LAST 10)
  // drops the oldest goal requests when the server is busy (seen under the
  // StaticSingleThreaded executor, which serves waitables last), which this
  // test would report as lost goals. Feedback may still drop (only its order
  // and content are checked).
  static constexpr size_t kServiceDepth = 10000;

  static rcl_action_server_options_t server_options()
  {
    auto o = rcl_action_server_get_default_options();
    o.goal_service_qos.depth = kServiceDepth;
    o.cancel_service_qos.depth = kServiceDepth;
    o.result_service_qos.depth = kServiceDepth;
    return o;
  }

  static rcl_action_client_options_t client_options()
  {
    auto o = rcl_action_client_get_default_options();
    o.goal_service_qos.depth = kServiceDepth;
    o.cancel_service_qos.depth = kServiceDepth;
    o.result_service_qos.depth = kServiceDepth;
    return o;
  }

  /// Server worker: executes queued goals to a terminal state.
  void work()
  {
    while (true) {
      std::shared_ptr<ServerGoal> handle;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] {return workers_stop_ || !queue_.empty();});
        if (queue_.empty()) {
          return;
        }
        handle = queue_.front();
        queue_.pop_front();
      }
      try {
        execute(handle);
      } catch (const std::exception & e) {
        error(std::string("server execute: ") + e.what());
      }
    }
  }

  void execute(const std::shared_ptr<ServerGoal> & handle)
  {
    const auto goal = handle->get_goal();
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    auto result = std::make_shared<Fibonacci::Result>();
    feedback->sequence = {0, 1};
    for (int32_t i = 1; i < goal->order; ++i) {
      if (handle->is_canceling()) {
        result->sequence = feedback->sequence;
        handle->canceled(result);
        add("server_canceled");
        add("server_finished");
        return;
      }
      const auto & seq = feedback->sequence;
      const int32_t next = seq[static_cast<size_t>(i)] + seq[static_cast<size_t>(i - 1)];
      feedback->sequence.push_back(next);
      handle->publish_feedback(feedback);
      std::this_thread::yield();
    }
    result->sequence = feedback->sequence;
    handle->succeed(result);
    add("server_finished");
  }

  /// One goal from a client thread; returns false on a timeout.
  void call_loop(const rclcpp_action::Client<Fibonacci>::SharedPtr & client, int64_t index)
  {
    std::mt19937 rng(static_cast<uint32_t>(seed_ * 7919 + index));
    try {
      while (!stop_ && !client->wait_for_action_server(std::chrono::milliseconds(100))) {
      }
      for (int64_t n = 1; !stop_ && (goals_ == 0 || n <= goals_); ++n) {
        const auto order = static_cast<int32_t>(2 + rng() % static_cast<uint32_t>(max_order_));
        const bool cancel = static_cast<int64_t>(rng() % 100) < cancel_percent_;
        one_goal(*client, order, cancel);
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("action client: ") + e.what());
      }
    }
  }

  void one_goal(rclcpp_action::Client<Fibonacci> & client, int32_t order, bool cancel)
  {
    // Callbacks run on the executor; they record into this state (test code,
    // instrumented) and the thread waits on a condition variable.
    struct State
    {
      std::mutex mutex;
      std::condition_variable cv;
      bool responded = false;
      bool accepted = false;
      bool done = false;
      rclcpp_action::ResultCode code = rclcpp_action::ResultCode::UNKNOWN;
      std::vector<int32_t> result;
      size_t last_feedback = 0;
      bool bad_feedback = false;
      // Weak: the goal handle owns the callbacks, which own this state; a
      // strong reference here would be a cycle (every goal handle leaks).
      std::weak_ptr<ClientGoal> handle;
    };
    auto st = std::make_shared<State>();
    const auto expected = fibonacci(order);
    rclcpp_action::Client<Fibonacci>::SendGoalOptions options;
    options.goal_response_callback = [st](ClientGoal::SharedPtr handle) {
        std::lock_guard<std::mutex> lock(st->mutex);
        st->responded = true;
        st->accepted = handle != nullptr;
        st->handle = handle;
        st->cv.notify_all();
      };
    options.feedback_callback =
      [st, expected](ClientGoal::SharedPtr, const std::shared_ptr<const Fibonacci::Feedback> fb) {
        std::lock_guard<std::mutex> lock(st->mutex);
        const auto & s = fb->sequence;
        if (s.size() <= st->last_feedback || s.size() > expected.size() ||
          !std::equal(s.begin(), s.end(), expected.begin()))
        {
          st->bad_feedback = true;
        }
        st->last_feedback = s.size();
      };
    options.result_callback = [st](const ClientGoal::WrappedResult & r) {
        std::lock_guard<std::mutex> lock(st->mutex);
        st->done = true;
        st->code = r.code;
        if (r.result) {
          st->result = r.result->sequence;
        }
        st->cv.notify_all();
      };
    Fibonacci::Goal goal;
    goal.order = order;
    client.async_send_goal(goal, options);
    add("goals_sent");

    // Generous timeouts: they detect lost goals/deadlocks, not slowness (the
    // StaticSingleThreaded executor serves waitables last and can starve them
    // for a long time under load).
    std::unique_lock<std::mutex> lock(st->mutex);
    if (!wait_or_stop(
        st->cv, lock, std::chrono::seconds(60), abort_, [&st] {return st->responded;}))
    {
      if (abort_) {
        return;  // torn down: no reply will come
      }
      add("timeouts_goal");
      error("goal response timed out");
      return;
    }
    if (!st->accepted) {
      add("rejected");
      error("goal rejected");
      return;
    }
    add("accepted");
    if (cancel) {
      auto handle = st->handle.lock();
      lock.unlock();
      try {
        if (handle) {
          client.async_cancel_goal(handle);
        }
      } catch (const rclcpp_action::exceptions::UnknownGoalHandleError &) {
        // The result arrived first and the client already forgot the goal.
        add("cancel_too_late");
      }
      lock.lock();
    }
    if (!wait_or_stop(st->cv, lock, std::chrono::seconds(180), abort_, [&st] {return st->done;})) {
      if (abort_) {
        return;
      }
      add("timeouts_goal");
      error("goal result timed out");
      return;
    }
    if (st->bad_feedback) {
      add("bad_feedback");
      error("feedback out of order or not a prefix of the result");
    }
    switch (st->code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        add("succeeded");
        if (st->result != expected) {
          add("bad_result");
          error("wrong result for order " + std::to_string(order));
        }
        break;
      case rclcpp_action::ResultCode::CANCELED:
        add("canceled");
        if (!cancel) {
          error("goal canceled without a cancel request");
        }
        if (st->result.size() > expected.size() ||
          !std::equal(st->result.begin(), st->result.end(), expected.begin()))
        {
          add("bad_result");
          error("canceled result is not a prefix");
        }
        break;
      case rclcpp_action::ResultCode::UNKNOWN:
        // rclcpp_action race (docs/unsupported.md): the server sends the goal
        // response before it registers the goal in rcl, so a result request
        // handled in between (MultiThreadedExecutor) is answered UNKNOWN.
        // The server still finishes the goal (server_finished counts it).
        add("result_unknown");
        break;
      default:
        add("aborted");
        error("unexpected result code " + std::to_string(static_cast<int>(st->code)));
        break;
    }
  }

  void churn_loop()
  {
    try {
      // Private node, used by this thread only (rcl entity init/fini on one
      // node is not thread-safe). It is not spun: its goals are accepted and
      // executed by the server, and the client is gone before the results.
      auto node = std::make_shared<rclcpp::Node>(
        std::string(get_name()) + "_churn", get_namespace(),
        rclcpp::NodeOptions().context(get_node_base_interface()->get_context())
        .start_parameter_services(false).start_parameter_event_publisher(false));
      for (int64_t i = 0; !stop_ && (churn_iterations_ == 0 || i < churn_iterations_); ++i) {
        auto client =
          rclcpp_action::create_client<Fibonacci>(node, action_, nullptr, client_options());
        add("churn_clients");
        if (client->wait_for_action_server(std::chrono::milliseconds(100))) {
          Fibonacci::Goal goal;
          goal.order = 1;  // tells the server this is a churned client
          client->async_send_goal(goal);
          add("abandoned_goals");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        client.reset();
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("churn: ") + e.what());
      }
    }
  }

  std::string action_;
  int64_t goals_ = 50;
  int64_t max_order_ = 20;
  int64_t cancel_percent_ = 20;
  int64_t seed_ = 1;
  rclcpp::CallbackGroup::SharedPtr group_ =
    create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  // Clients in a mutually exclusive group: in a reentrant one a
  // MultiThreadedExecutor runs one client's feedback callbacks on several
  // threads at once, which reorders them (seen in CI); the RMW delivers them
  // in order.
  rclcpp::CallbackGroup::SharedPtr client_group_ =
    create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
  std::vector<rclcpp_action::Client<Fibonacci>::SharedPtr> clients_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<std::shared_ptr<ServerGoal>> queue_;
  bool workers_stop_ = false;
  std::vector<std::thread> workers_;
  int64_t churn_iterations_ = 200;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::ActionStorm)
