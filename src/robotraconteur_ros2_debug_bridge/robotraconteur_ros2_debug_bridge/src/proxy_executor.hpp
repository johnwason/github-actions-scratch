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

// One thread, and one ROS node of its own, for the bridge's proxy entities
// (B4).
//
// rcl (Jazzy) keeps a per-node type description cache (an rcutils hash
// map) that entity creation and destruction write and the node's
// ~/get_type_description service reads, without locking. With the proxies
// on the bridge node, Robot Raconteur threads and the host's executor
// threads touched that cache at the same time (ThreadSanitizer). So the
// proxies' entities belong to a separate node, "<bridge>_proxies", that only
// this thread creates, spins (its own executor) and destroys: everything
// that touches its cache runs here.

#ifndef PROXY_EXECUTOR_HPP_
#define PROXY_EXECUTOR_HPP_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"

namespace robotraconteur_ros2_debug_bridge
{

class ProxyExecutor
{
public:
  /// Creates the proxies' node ("<bridge name>_proxies" in the bridge's
  /// namespace and context) and the thread.
  explicit ProxyExecutor(rclcpp::Node & bridge);
  ~ProxyExecutor();
  ProxyExecutor(const ProxyExecutor &) = delete;
  ProxyExecutor & operator=(const ProxyExecutor &) = delete;

  /// The proxies' node: create entities on it only through run()/post().
  rclcpp::Node & node() {return *node_;}
  /// Its default callback group.
  rclcpp::CallbackGroup::SharedPtr group() const {return group_;}

  /// Runs `f` on the thread; the work (and destruction of whatever it
  /// captures) happens there.
  void post(std::function<void()> f);

  /// Calls `p` on the thread after each spin until it returns true (for
  /// futures and deadlines; the bridge never waits for them). At stop, each
  /// is called once more with stopping() true and then dropped.
  void add_poller(std::function<bool()> p);
  bool stopping();

  /// Runs `f` on the thread and waits for its result (or exception).
  /// Throws std::runtime_error if the thread is stopped or does not answer
  /// within `timeout`.
  template<class F>
  auto run(F f, std::chrono::milliseconds timeout = std::chrono::seconds(10))
  -> decltype(f())
  {
    using R = decltype(f());
    auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
    auto result = task->get_future();
    post([task]() {(*task)();});
    if (result.wait_for(timeout) != std::future_status::ready) {
      throw std::runtime_error("the bridge's ROS thread did not answer");
    }
    return result.get();
  }

  /// Runs what is queued, then stops the thread. Idempotent.
  void stop();

private:
  void loop();
  void run_pollers();

  rclcpp::Node::SharedPtr node_;
  rclcpp::CallbackGroup::SharedPtr group_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::mutex mutex_;
  std::deque<std::function<void()>> queue_;
  std::vector<std::function<bool()>> pollers_;  // touched on the thread only
  bool stopping_ = false;
  std::thread thread_;
};

/// Drops the last reference to a ROS entity on the proxy thread (or here,
/// once that thread is stopped: shutdown, nothing else runs then).
template<class T>
void destroy_on_proxy_thread(const std::shared_ptr<ProxyExecutor> & proxy, std::shared_ptr<T> p)
{
  if (!p) {
    return;
  }
  if (proxy) {
    try {
      proxy->post([q = std::move(p)]() mutable {q.reset();});
      return;
    } catch (const std::exception &) {
    }
  }
  p.reset();
}

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // PROXY_EXECUTOR_HPP_
