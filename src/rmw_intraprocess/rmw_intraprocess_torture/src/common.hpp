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

// Shared pieces of the torture components (M8c): the statistics service every
// component offers (~/get_stats), error recording, payload checksums and
// process-unique ids.

#ifndef COMMON_HPP_
#define COMMON_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rmw_intraprocess_torture/msg/payload.hpp"
#include "rmw_intraprocess_torture/srv/get_stats.hpp"

namespace rmw_intraprocess_torture
{

/// FNV-1a over the payload fields that the checksum covers.
inline uint32_t checksum(const msg::Payload & p)
{
  uint32_t h = 2166136261u;
  auto mix = [&h](uint8_t b) {
      h ^= b;
      h *= 16777619u;
    };
  for (int i = 0; i < 4; ++i) {
    mix(static_cast<uint8_t>(p.publisher_id >> (8 * i)));
  }
  for (int i = 0; i < 8; ++i) {
    mix(static_cast<uint8_t>(p.seq >> (8 * i)));
  }
  for (uint8_t b : p.data) {
    mix(b);
  }
  for (char c : p.text) {
    mix(static_cast<uint8_t>(c));
  }
  return h;
}

/// Ids unique within the process (publishers, clients), so receivers can
/// tell senders apart across components.
inline uint32_t next_id()
{
  static std::atomic<uint32_t> counter{1};
  return counter.fetch_add(1);
}

/// Waits on cv (lock held) until pred() holds, `stop` is set, or the timeout
/// expires; returns pred(). The stop flag is polled every 50 ms, so a stopped
/// component never sits out a long timeout (e.g. a reply that will never come
/// after rclcpp::shutdown()).
template<class Pred>
bool wait_or_stop(
  std::condition_variable & cv, std::unique_lock<std::mutex> & lock,
  std::chrono::milliseconds timeout, const std::atomic<bool> & stop, Pred pred)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && !stop.load() && std::chrono::steady_clock::now() < deadline) {
    cv.wait_for(lock, std::chrono::milliseconds(50));
  }
  return pred();
}

/// The same for a std::future.
template<class T>
bool wait_or_stop(
  std::future<T> & future, std::chrono::milliseconds timeout, const std::atomic<bool> & stop)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
    if (future.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) {
      return true;
    }
  }
  return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

/// Base of every torture component: counters, errors and ~/get_stats.
/// BaseNode is rclcpp::Node or rclcpp_lifecycle::LifecycleNode.
template<class BaseNode>
class TortureNodeT : public BaseNode
{
public:
  TortureNodeT(const std::string & name, const rclcpp::NodeOptions & options)
  : BaseNode(name, options)
  {
    // rclcpp::create_service works for both node types (their member
    // create_service overloads differ).
    stats_service_ = rclcpp::create_service<srv::GetStats>(
      this->get_node_base_interface(), this->get_node_services_interface(), "~/get_stats",
      [this](const std::shared_ptr<srv::GetStats::Request> req,
      std::shared_ptr<srv::GetStats::Response> res) {
        if (req->stop && !stopped_.exchange(true)) {
          on_stop();
        }
        fill(*res);
      },
      rclcpp::ServicesQoS(), stats_group_);
  }

protected:
  /// Stops generating new work; called once, from the stats service.
  virtual void on_stop() {}

  bool stopped() const {return stopped_.load();}

  /// Set by a component's destructor before it joins its threads: waits
  /// give up (wait_or_stop) instead of sitting out their timeouts, and a
  /// wait abandoned this way is not an error. A normal stop (~/get_stats with
  /// stop) does not set it: in-flight work completes.
  std::atomic<bool> abort_{false};

  void add(const std::string & key, int64_t n = 1)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[key] += n;
  }

  void set(const std::string & key, int64_t v)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[key] = v;
  }

  /// Records an invariant violation (kept to the first 100).
  void error(const std::string & what)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (errors_.size() < 100) {
      errors_.push_back(what);
    }
    ++counters_["errors"];
  }

private:
  void fill(srv::GetStats::Response & res)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    res.component = this->get_fully_qualified_name();
    for (const auto & kv : counters_) {
      res.keys.push_back(kv.first);
      res.values.push_back(kv.second);
    }
    res.errors = errors_;
  }

  // The stats service gets its own group so a MultiThreadedExecutor answers
  // it while other callbacks of the component are busy.
  rclcpp::CallbackGroup::SharedPtr stats_group_ =
    this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  rclcpp::Service<srv::GetStats>::SharedPtr stats_service_;
  std::atomic<bool> stopped_{false};
  std::mutex mutex_;
  std::map<std::string, int64_t> counters_;
  std::vector<std::string> errors_;
};

using TortureNode = TortureNodeT<rclcpp::Node>;

}  // namespace rmw_intraprocess_torture

#endif  // COMMON_HPP_
