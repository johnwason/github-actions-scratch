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

#include "proxy_executor.hpp"

#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

// How long the thread waits for ROS events before looking at its queue.
constexpr std::chrono::milliseconds kSpinSlice{10};

}  // namespace

ProxyExecutor::ProxyExecutor(rclcpp::Node & bridge)
{
  // Made here, before the thread starts (which orders it before all use).
  const auto context = bridge.get_node_base_interface()->get_context();
  rclcpp::NodeOptions options;
  options.context(context).start_parameter_services(false)
  .start_parameter_event_publisher(false).use_global_arguments(false);
  node_ = std::make_shared<rclcpp::Node>(
    std::string(bridge.get_name()) + "_proxies", bridge.get_namespace(), options);
  group_ = node_->get_node_base_interface()->get_default_callback_group();
  rclcpp::ExecutorOptions exec_options;
  exec_options.context = context;
  executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(exec_options);
  executor_->add_node(node_);
  thread_ = std::thread([this]() {loop();});
}

ProxyExecutor::~ProxyExecutor() {stop();}

void ProxyExecutor::post(std::function<void()> f)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_) {
    throw std::runtime_error("the bridge's ROS thread is stopped");
  }
  queue_.push_back(std::move(f));
}

void ProxyExecutor::loop()
{
  for (;; ) {
    std::deque<std::function<void()>> work;
    bool stop_now = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      work.swap(queue_);
      stop_now = stopping_;
    }
    while (!work.empty()) {
      {
        std::function<void()> f = std::move(work.front());
        work.pop_front();
        try {
          f();
        } catch (const std::exception &) {
          // a task's failure must not end the thread (and the process)
        }
      }  // f, and what it captured, destroyed here, on this thread
    }
    if (stop_now) {
      bool empty = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        empty = queue_.empty();
      }
      if (empty) {
        break;
      }
      continue;
    }
    try {
      executor_->spin_once(kSpinSlice);
    } catch (const std::exception &) {
      // for example the ROS context shut down (SIGINT): keep serving the queue
      std::this_thread::sleep_for(kSpinSlice);
    }
    run_pollers();
  }
  run_pollers();  // they see stopping() and finish
  pollers_.clear();
  // The node goes here too, on this thread.
  executor_->remove_node(node_);
  executor_.reset();
  group_.reset();
  node_.reset();
}

void ProxyExecutor::run_pollers()
{
  for (size_t i = 0; i < pollers_.size(); ) {
    bool done = true;
    try {
      done = pollers_[i]();
    } catch (const std::exception &) {
    }
    if (done) {
      pollers_.erase(pollers_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
}

void ProxyExecutor::add_poller(std::function<bool()> p)
{
  post([this, p = std::move(p)]() {pollers_.push_back(p);});
}

bool ProxyExecutor::stopping()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return stopping_;
}

void ProxyExecutor::stop()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

}  // namespace robotraconteur_ros2_debug_bridge
