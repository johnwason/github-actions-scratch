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

// ParamChurn: setter threads set their own parameter p_<n> on a target node
// (this node or another component) to 1, 2, 3, ... through the remote
// parameter services, as fast as the responses come back. A subscription to
// /parameter_events checks that each parameter's values arrive in order
// (strictly increasing, no repeats). After stop, each setter reads its
// parameter back: it must hold the last value set.
//
// Parameters:
//   target (string, ""): fully qualified node name; "" = this node
//   setters (int, 2), sets (int, 200): per setter; 0 = until stopped
//   declare (bool, true): declare p_* on this node (allow undeclared otherwise)
//
// Stats: sets, set_failed, events, event_out_of_order, final_mismatch,
// setters / setters_done (the driver waits for setters_done == setters).
// Keys are per component; several ParamChurn components may target the same
// node (setter parameters are named after the setting component).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

class ParamChurn : public TortureNode
{
public:
  explicit ParamChurn(const rclcpp::NodeOptions & options)
  : TortureNode(
      "param_churn",
      rclcpp::NodeOptions(options).allow_undeclared_parameters(true)
      .automatically_declare_parameters_from_overrides(false))
  {
    target_ = declare_parameter("target", std::string(""));
    const auto setters = declare_parameter("setters", 2);
    sets_ = declare_parameter("sets", 200);
    if (target_.empty()) {
      target_ = get_fully_qualified_name();
    }
    events_ = create_subscription<rcl_interfaces::msg::ParameterEvent>(
      "/parameter_events", rclcpp::ParameterEventsQoS(),
      [this](const rcl_interfaces::msg::ParameterEvent & e) {on_event(e);});
    client_ = std::make_shared<rclcpp::AsyncParametersClient>(this, target_);
    for (int64_t i = 0; i < setters; ++i) {
      names_.push_back(std::string("p_") + get_name() + "_" + std::to_string(i));
    }
    set("setters", setters);
    for (const auto & name : names_) {
      threads_.emplace_back(
        [this, name]() {
          set_loop(name);
          add("setters_done");
        });
    }
  }

  ~ParamChurn() override
  {
    abort_ = true;
    stop_ = true;
    for (auto & t : threads_) {
      t.join();
    }
  }

protected:
  void on_stop() override {stop_ = true;}

private:
  template<class T>
  struct Box
  {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<T> value;

    void put(T v)
    {
      {
        std::lock_guard<std::mutex> lock(mutex);
        value = std::move(v);
      }
      cv.notify_all();
    }

    std::optional<T> take(std::chrono::seconds timeout, const std::atomic<bool> & abort)
    {
      std::unique_lock<std::mutex> lock(mutex);
      wait_or_stop(cv, lock, timeout, abort, [this] {return value.has_value();});
      return value;
    }
  };

  void set_loop(const std::string & name)
  {
    using Results = std::vector<rcl_interfaces::msg::SetParametersResult>;
    try {
      while (!stop_ && !client_->wait_for_service(std::chrono::milliseconds(100))) {
      }
      int64_t last = 0;
      for (int64_t v = 1; !stop_ && (sets_ == 0 || v <= sets_); ++v) {
        auto box = std::make_shared<Box<Results>>();
        client_->set_parameters(
          {rclcpp::Parameter(name, v)},
          [box](std::shared_future<Results> f) {box->put(f.get());});
        auto results = box->take(std::chrono::seconds(60), abort_);
        if (abort_) {
          return;  // torn down: no reply will come
        }
        add("sets");
        if (!results || results->size() != 1 || !results->at(0).successful) {
          add("set_failed");
          error("set " + name + "=" + std::to_string(v) + " failed or timed out");
          return;
        }
        last = v;
      }
      if (last == 0) {
        return;
      }
      // Read back: the parameter holds the last value set.
      using Values = std::vector<rclcpp::Parameter>;
      auto box = std::make_shared<Box<Values>>();
      client_->get_parameters({name}, [box](std::shared_future<Values> f) {box->put(f.get());});
      auto values = box->take(std::chrono::seconds(60), abort_);
      if (abort_) {
        return;  // torn down: no reply will come
      }
      if (!values || values->size() != 1 || values->at(0).get_type() != rclcpp::PARAMETER_INTEGER ||
        values->at(0).as_int() != last)
      {
        add("final_mismatch");
        error(name + " does not hold its last value " + std::to_string(last));
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("setter: ") + e.what());
      }
    }
  }

  void on_event(const rcl_interfaces::msg::ParameterEvent & e)
  {
    if (e.node != target_) {
      return;
    }
    std::lock_guard<std::mutex> lock(event_mutex_);
    auto check = [this](const rcl_interfaces::msg::Parameter & p) {
        auto it = last_event_.find(p.name);
        if (it == last_event_.end()) {
          return;  // not one of this component's parameters
        }
        add("events");
        if (p.value.integer_value <= it->second) {
          add("event_out_of_order");
          error("event for " + p.name + ": " + std::to_string(p.value.integer_value) +
            " after " + std::to_string(it->second));
        }
        it->second = p.value.integer_value;
      };
    if (last_event_.empty()) {
      for (const auto & n : names_) {
        last_event_[n] = 0;
      }
    }
    for (const auto & p : e.new_parameters) {
      check(p);
    }
    for (const auto & p : e.changed_parameters) {
      check(p);
    }
  }

  std::string target_;
  int64_t sets_ = 200;
  std::vector<std::string> names_;
  rclcpp::Subscription<rcl_interfaces::msg::ParameterEvent>::SharedPtr events_;
  rclcpp::AsyncParametersClient::SharedPtr client_;
  std::mutex event_mutex_;
  std::map<std::string, int64_t> last_event_;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::ParamChurn)
