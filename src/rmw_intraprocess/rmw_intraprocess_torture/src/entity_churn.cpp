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

// EntityChurn: worker threads create and destroy publishers, subscriptions,
// services, clients, timers, guard conditions and whole nodes, at random,
// while they publish on the live publishers and other threads query the graph.
// Each churn thread only touches nodes it created itself (up to 3, created and
// dropped at random): rcl documents entity init/fini as not thread-safe per
// node (its per-node type description cache has no lock), and concurrent
// create/destroy on one node corrupts that cache. These nodes are not spun.
//
// Parameters:
//   threads (int, 2): churn threads; graph_threads (int, 1): graph query threads
//   max_live (int, 20): live entities per churn thread
//   seed (int, 1); prefix (string, "churn"): names <prefix>_<thread>_<n>
//
// Stats: cycles, created, destroyed, published, graph_queries, live (0 after
// stop). On stop every entity is destroyed; the driver then checks that the
// graph no longer lists any <prefix>_ topic, service or node.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

class EntityChurn : public TortureNode
{
public:
  explicit EntityChurn(const rclcpp::NodeOptions & options)
  : TortureNode("entity_churn", options)
  {
    const auto threads = declare_parameter("threads", 2);
    const auto graph_threads = declare_parameter("graph_threads", 1);
    max_live_ = declare_parameter("max_live", 20);
    seed_ = declare_parameter("seed", 1);
    prefix_ = declare_parameter("prefix", std::string("churn"));
    for (int64_t i = 0; i < threads; ++i) {
      threads_.emplace_back([this, i]() {churn(i);});
    }
    for (int64_t i = 0; i < graph_threads; ++i) {
      threads_.emplace_back([this]() {query_graph();});
    }
  }

  ~EntityChurn() override
  {
    abort_ = true;
    stop_ = true;
    join();
  }

protected:
  void on_stop() override
  {
    stop_ = true;
    join();
  }

private:
  void join()
  {
    std::lock_guard<std::mutex> lock(join_mutex_);
    for (auto & t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

  /// One live entity: a holder that keeps it alive and, for publishers, a
  /// publish action.
  struct Entity
  {
    std::shared_ptr<void> holder;
    std::function<void()> publish;
  };

  void churn(int64_t index)
  {
    std::mt19937 rng(static_cast<uint32_t>(seed_ + index));
    std::vector<Entity> live;
    std::vector<rclcpp::Node::SharedPtr> extra_nodes;
    uint64_t n = 0;
    try {
      while (!stop_) {
        add("cycles");
        if (static_cast<int64_t>(live.size()) >= max_live_ || (!live.empty() && rng() % 3 == 0)) {
          live.erase(live.begin() + static_cast<std::ptrdiff_t>(rng() % live.size()));
          add("destroyed");
          add("live", -1);
          continue;
        }
        if (extra_nodes.empty() || (extra_nodes.size() < 3 && rng() % 10 == 0)) {
          extra_nodes.push_back(
            std::make_shared<rclcpp::Node>(
              prefix_ + "_node_" + std::to_string(index) + "_" + std::to_string(n++),
              get_namespace(),
              rclcpp::NodeOptions().context(get_node_base_interface()->get_context())
              .start_parameter_services(false).start_parameter_event_publisher(false)));
          add("nodes_created");
        } else if (extra_nodes.size() > 1 && rng() % 20 == 0) {
          // Entities on the node keep it alive until they are destroyed.
          extra_nodes.erase(extra_nodes.begin());
          add("nodes_dropped");
        }
        // Only this thread's own nodes: rcl entity init/fini on one node is not
        // thread-safe (rcl's node type cache), so a node's entities must not
        // be created or destroyed concurrently from several threads.
        rclcpp::Node * node = extra_nodes[rng() % extra_nodes.size()].get();
        const std::string name =
          prefix_ + "_" + std::to_string(index) + "_" + std::to_string(rng() % 8);
        live.push_back(create(*node, name, static_cast<int>(rng() % 6)));
        add("created");
        add("live");
        for (auto & e : live) {
          if (e.publish && rng() % 4 == 0) {
            e.publish();
            add("published");
          }
        }
      }
    } catch (const std::exception & e) {
      error(std::string("churn: ") + e.what());
    }
    add("live", -static_cast<int64_t>(live.size()));
    live.clear();
    extra_nodes.clear();
  }

  Entity create(rclcpp::Node & node, const std::string & name, int kind)
  {
    Entity e;
    switch (kind) {
      case 0: {
          auto pub = node.create_publisher<test_msgs::msg::BasicTypes>(name, 10);
          e.publish = [pub]() {pub->publish(test_msgs::msg::BasicTypes());};
          e.holder = pub;
          break;
        }
      case 1:
        e.holder = node.create_subscription<test_msgs::msg::BasicTypes>(
          name, 10, [this](const test_msgs::msg::BasicTypes &) {add("received");});
        break;
      case 2:
        e.holder = node.create_service<test_msgs::srv::BasicTypes>(
          name + "_srv",
          [](const std::shared_ptr<test_msgs::srv::BasicTypes::Request>,
          std::shared_ptr<test_msgs::srv::BasicTypes::Response>) {});
        break;
      case 3:
        e.holder = node.create_client<test_msgs::srv::BasicTypes>(name + "_srv");
        break;
      case 4:
        e.holder = node.create_wall_timer(
          std::chrono::milliseconds(1), [this]() {add("timer_fired");});
        break;
      default:
        e.holder = std::make_shared<rclcpp::GuardCondition>(
          node.get_node_base_interface()->get_context());
        break;
    }
    return e;
  }

  void query_graph()
  {
    try {
      while (!stop_) {
        const auto topics = get_topic_names_and_types();
        const auto services = get_service_names_and_types();
        const auto nodes = get_node_names();
        (void)count_publishers(prefix_ + "_0_0");
        (void)count_subscribers(prefix_ + "_0_0");
        if (nodes.empty()) {
          error("graph lists no nodes (this node exists)");
        }
        (void)topics;
        (void)services;
        add("graph_queries");
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    } catch (const std::exception & e) {
      error(std::string("graph: ") + e.what());
    }
  }

  int64_t max_live_ = 20;
  int64_t seed_ = 1;
  std::string prefix_;
  std::atomic<bool> stop_{false};
  std::mutex join_mutex_;
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::EntityChurn)
