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

// Helpers shared by the rclcpp-level tests (M8): the executor types under test
// and a background spinner.

#ifndef RCLCPP_TEST_UTILS_HPP_
#define RCLCPP_TEST_UTILS_HPP_

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"

namespace m8
{

/// The executor types of rclcpp (Jazzy). StaticSingleThreaded is a variant of
/// SingleThreaded with a fixed entity collection.
enum class ExecutorKind {kSingleThreaded, kStaticSingleThreaded, kMultiThreaded, kEvents};

inline const char * executor_name(ExecutorKind kind)
{
  switch (kind) {
    case ExecutorKind::kSingleThreaded: return "SingleThreaded";
    case ExecutorKind::kStaticSingleThreaded: return "StaticSingleThreaded";
    case ExecutorKind::kMultiThreaded: return "MultiThreaded";
    case ExecutorKind::kEvents: return "Events";
  }
  return "unknown";
}

inline std::shared_ptr<rclcpp::Executor> make_executor(ExecutorKind kind)
{
  switch (kind) {
    case ExecutorKind::kSingleThreaded:
      return std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    case ExecutorKind::kStaticSingleThreaded:
      return std::make_shared<rclcpp::executors::StaticSingleThreadedExecutor>();
    case ExecutorKind::kMultiThreaded:
      return std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
        rclcpp::ExecutorOptions(), 4);
    case ExecutorKind::kEvents:
      return std::make_shared<rclcpp::experimental::executors::EventsExecutor>();
  }
  return nullptr;
}

/// gtest parameter name printer for ExecutorKind.
struct ExecutorKindName
{
  template<class ParamInfo>
  std::string operator()(const ParamInfo & info) const {return executor_name(info.param);}
};

inline bool wait_until(
  const std::function<bool()> & pred,
  std::chrono::milliseconds timeout = std::chrono::milliseconds(15000))
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return true;
}

/// Spins an executor on a background thread for the lifetime of the object.
/// Nodes must be added before start() (StaticSingleThreaded fixes its
/// collection only when entities change, which it also handles, but adding
/// first keeps every executor type on the same path).
class Spinner
{
public:
  explicit Spinner(ExecutorKind kind)
  : executor_(make_executor(kind)) {}
  ~Spinner() {stop();}
  Spinner(const Spinner &) = delete;
  Spinner & operator=(const Spinner &) = delete;

  template<class NodeT>
  void add(const std::shared_ptr<NodeT> & node)
  {
    executor_->add_node(node->get_node_base_interface());
  }

  void start()
  {
    returned_ = false;
    thread_ = std::thread(
      [this]() {
        executor_->spin();
        returned_ = true;
      });
  }

  /// A cancel() issued before spin() has started is lost, so it is repeated
  /// until spin() returns.
  void stop()
  {
    if (thread_.joinable()) {
      while (!returned_.load()) {
        executor_->cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      thread_.join();
    }
  }

  rclcpp::Executor & executor() {return *executor_;}

private:
  std::shared_ptr<rclcpp::Executor> executor_;
  std::thread thread_;
  std::atomic<bool> returned_{false};
};

/// Hands one value from an executor callback to the test thread.
///
/// Futures that rclcpp completes inside librclcpp (AsyncParametersClient), or
/// responses whose last reference is dropped there, synchronize through code
/// TSan does not instrument, so reading them on the test thread is reported as
/// a race. Instead, the callback copies what the test needs into a Mailbox
/// (instrumented mutex/condition variable) on the executor thread.
template<class T>
class Mailbox
{
public:
  void put(T value)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      value_ = std::move(value);
    }
    cv_.notify_all();
  }

  /// Waits for the value; std::nullopt on timeout.
  std::optional<T> take(std::chrono::milliseconds timeout = std::chrono::milliseconds(10000))
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] {return value_.has_value();})) {
      return std::nullopt;
    }
    std::optional<T> out = std::move(value_);
    value_.reset();
    return out;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<T> value_;
};

/// Calls a service and returns a copy of the response, made in the response
/// callback (see Mailbox); std::nullopt on timeout.
template<class ServiceT>
std::optional<typename ServiceT::Response> call(
  typename rclcpp::Client<ServiceT>::SharedPtr client,
  typename ServiceT::Request::SharedPtr request,
  std::chrono::milliseconds timeout = std::chrono::milliseconds(10000))
{
  auto box = std::make_shared<Mailbox<typename ServiceT::Response>>();
  client->async_send_request(
    request, [box](typename rclcpp::Client<ServiceT>::SharedFuture f) {box->put(*f.get());});
  return box->take(timeout);
}

/// AsyncParametersClient::set_parameters, results copied in the callback.
inline std::optional<std::vector<rcl_interfaces::msg::SetParametersResult>> set_parameters(
  rclcpp::AsyncParametersClient & client, const std::vector<rclcpp::Parameter> & parameters)
{
  using Results = std::vector<rcl_interfaces::msg::SetParametersResult>;
  auto box = std::make_shared<Mailbox<Results>>();
  client.set_parameters(
    parameters, [box](std::shared_future<Results> f) {box->put(f.get());});
  return box->take();
}

/// AsyncParametersClient::get_parameters, values copied in the callback.
inline std::optional<std::vector<rclcpp::Parameter>> get_parameters(
  rclcpp::AsyncParametersClient & client, const std::vector<std::string> & names)
{
  using Values = std::vector<rclcpp::Parameter>;
  auto box = std::make_shared<Mailbox<Values>>();
  client.get_parameters(names, [box](std::shared_future<Values> f) {box->put(f.get());});
  return box->take();
}

/// A test-unique name ("<prefix>_<test>_<param>"; gtest names parameterized
/// tests "<test>/<param>"), so tests in one
/// process never share topics, services or actions.
inline std::string unique_name(const std::string & prefix)
{
  const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::string name = prefix + "_" + info->name();
  for (char & c : name) {
    if (c == '/') {
      c = '_';
    }
  }
  return name;
}

}  // namespace m8

#endif  // RCLCPP_TEST_UTILS_HPP_
