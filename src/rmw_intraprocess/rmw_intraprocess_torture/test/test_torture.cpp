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

// M8c torture driver: loads a mix of torture components through their
// component factories (an in-process rclcpp_components::ComponentManager and
// its load_node service), lets them run, stops them, waits until in-flight
// work has drained, collects every component's ~/get_stats and checks
// invariants and counts (never timing). Run for every executor type, with
// intra-process comms off and on, against rmw_intraprocess_cpp and the
// reference rmw_fastrtps_cpp.
//
// TORTURE_RUN_MS (default 3000) sets how long the mix runs before it is
// stopped; TORTURE_SEED (default 1) seeds every component. Failures print the
// seed. TORTURE_ONLY (comma-separated component names) loads a subset.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "composition_interfaces/srv/load_node.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"
#include "rclcpp_components/component_manager.hpp"
#include "lifecycle_msgs/msg/transition_event.h"
#include "rmw_intraprocess_torture/srv/get_stats.hpp"

using namespace std::chrono_literals;
using composition_interfaces::srv::LoadNode;
using rmw_intraprocess_torture::srv::GetStats;

namespace
{

enum class ExecutorKind {kSingleThreaded, kStaticSingleThreaded, kMultiThreaded, kEvents};

struct Config
{
  ExecutorKind executor;
  bool intra_process;
};

std::string config_name(const Config & c)
{
  const char * e = "";
  switch (c.executor) {
    case ExecutorKind::kSingleThreaded: e = "SingleThreaded"; break;
    case ExecutorKind::kStaticSingleThreaded: e = "StaticSingleThreaded"; break;
    case ExecutorKind::kMultiThreaded: e = "MultiThreaded"; break;
    case ExecutorKind::kEvents: e = "Events"; break;
  }
  return std::string(e) + (c.intra_process ? "_IntraProcess" : "_NoIntraProcess");
}

/// gtest parameter name printer (a functor: a lambda parameter named info
/// would shadow gtest's own).
struct ConfigName
{
  std::string operator()(const ::testing::TestParamInfo<Config> & p) const
  {
    return config_name(p.param);
  }
};

std::shared_ptr<rclcpp::Executor> make_executor(ExecutorKind kind)
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

/// The component library is unloaded when the container is destroyed. Type
/// support libraries that only the components use go with it, and
/// rosidl_typesupport_c's per-type handle cache, which lives in them, is then
/// reported by LeakSanitizer (same mechanism as the rosbag2 test in
/// rmw_intraprocess_cpp). rcl_lifecycle uses lifecycle_msgs' C type support
/// (LifecycleChurn); referencing it keeps that library mapped until exit.
const rosidl_message_type_support_t * keep_type_support_mapped()
{
  return ROSIDL_GET_MSG_TYPE_SUPPORT(lifecycle_msgs, msg, TransitionEvent);
}

int64_t env_int(const char * name, int64_t fallback)
{
  const char * v = std::getenv(name);
  return v != nullptr && *v != '\0' ? std::atoll(v) : fallback;
}

/// Hands a service response from the executor thread to the test thread
/// through an instrumented mutex (see rmw_intraprocess_cpp's
/// test/rclcpp_test_utils.hpp for why TSan needs this).
template<class ServiceT>
std::optional<typename ServiceT::Response> call(
  const typename rclcpp::Client<ServiceT>::SharedPtr & client,
  const typename ServiceT::Request::SharedPtr & request, std::chrono::milliseconds timeout)
{
  struct Box
  {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<typename ServiceT::Response> value;
  };
  auto box = std::make_shared<Box>();
  client->async_send_request(
    request, [box](typename rclcpp::Client<ServiceT>::SharedFuture f) {
      {
        std::lock_guard<std::mutex> lock(box->mutex);
        box->value = *f.get();
      }
      box->cv.notify_all();
    });
  std::unique_lock<std::mutex> lock(box->mutex);
  box->cv.wait_for(lock, timeout, [&box] {return box->value.has_value();});
  return box->value;
}

/// One component of the mix: class, node name and parameters.
struct Component
{
  std::string plugin;
  std::string name;
  std::vector<rclcpp::Parameter> parameters;
  /// false: never use intra-process comms for this component (see
  /// EntityChurn below).
  bool intra_process_allowed = true;
};

/// Stats of all components, summed by key, plus their errors.
struct Totals
{
  std::map<std::string, int64_t> sum;
  std::vector<std::string> errors;

  int64_t get(const std::string & key) const
  {
    auto it = sum.find(key);
    return it == sum.end() ? 0 : it->second;
  }
};

class Torture : public ::testing::TestWithParam<Config>
{
protected:
  void SetUp() override
  {
    ASSERT_NE(nullptr, keep_type_support_mapped());
    seed = env_int("TORTURE_SEED", 1);
    run_ms = env_int("TORTURE_RUN_MS", 3000);
    rclcpp::init(0, nullptr);
    executor = make_executor(GetParam().executor);
    container = std::make_shared<rclcpp_components::ComponentManager>(
      executor, "torture_container");
    executor->add_node(container);
    driver = std::make_shared<rclcpp::Node>("torture_driver");
    executor->add_node(driver);
    spin_thread = std::thread(
      [this]() {
        try {
          executor->spin();
        } catch (const std::exception & e) {
          if (rclcpp::ok()) {
            spin_error = e.what();
          } else {
            ++spin_exceptions_after_shutdown;
          }
        }
      });
    load = driver->create_client<LoadNode>("/torture_container/_container/load_node");
    ASSERT_TRUE(load->wait_for_service(10s));
  }

  void TearDown() override
  {
    // Stop the executor first, then destroy everything from this thread.
    // Unloading components through unload_node while a MultiThreadedExecutor
    // runs lets an executor thread drop the last reference to one of a
    // component's entities while the component's destructor finalizes the
    // node's other entities: concurrent rcl entity fini on one node, which
    // rcl does not support (per-node type cache; TSan reports it). The same
    // applies to the driver's own clients.
    executor->cancel();
    spin_thread.join();
    EXPECT_EQ("", spin_error) << "exception out of spin() while rclcpp was running";
    stats_clients.clear();
    load.reset();
    executor->remove_node(driver);
    executor->remove_node(container);
    // The executor goes before the container: destroying the container
    // unloads the component libraries, and the EventsExecutor's pending event
    // queue still holds references whose release code lives in them (SEGV).
    executor.reset();
    driver.reset();
    container.reset();  // destroys the components, which join their threads
    rclcpp::shutdown();
  }

  void load_mix(const std::vector<Component> & mix)
  {
    // TORTURE_ONLY=name1,name2 loads just those components of the mix (the
    // checks of components not loaded are skipped).
    const char * only = std::getenv("TORTURE_ONLY");
    const std::string filter = only != nullptr ? "," + std::string(only) + "," : "";
    for (const auto & c : mix) {
      if (!filter.empty() && filter.find("," + c.name + ",") == std::string::npos) {
        continue;
      }
      auto req = std::make_shared<LoadNode::Request>();
      req->package_name = "rmw_intraprocess_torture";
      req->plugin_name = "rmw_intraprocess_torture::" + c.plugin;
      req->node_name = c.name;
      for (const auto & p : c.parameters) {
        req->parameters.push_back(p.to_parameter_msg());
      }
      req->parameters.push_back(rclcpp::Parameter("seed", seed).to_parameter_msg());
      req->extra_arguments.push_back(
        rclcpp::Parameter(
          "use_intra_process_comms", GetParam().intra_process && c.intra_process_allowed)
        .to_parameter_msg());
      auto res = call<LoadNode>(load, req, 30000ms);
      ASSERT_TRUE(res.has_value()) << "load_node timed out: " << c.name;
      ASSERT_TRUE(res->success) << c.name << ": " << res->error_message;
      auto client = driver->create_client<GetStats>("/" + c.name + "/get_stats");
      ASSERT_TRUE(client->wait_for_service(10s)) << c.name;
      stats_clients.push_back(client);
      loaded_names.insert(c.name);
    }
  }

  bool has(const std::string & name) const {return loaded_names.count(name) > 0;}

  /// Collects (and with stop, stops) the components whose stats service name
  /// starts with prefix (all when empty).
  Totals collect(bool stop, const std::string & prefix = "")
  {
    Totals totals;
    for (const auto & client : stats_clients) {
      if (!prefix.empty() && std::string(client->get_service_name()).rfind(prefix, 0) != 0) {
        continue;
      }
      auto req = std::make_shared<GetStats::Request>();
      req->stop = stop;
      auto res = call<GetStats>(client, req, 60000ms);
      if (!res) {
        totals.errors.push_back("get_stats timed out: " + std::string(client->get_service_name()));
        continue;
      }
      for (size_t i = 0; i < res->keys.size(); ++i) {
        totals.sum[res->keys[i]] += res->values[i];
      }
      for (const auto & e : res->errors) {
        totals.errors.push_back(res->component + ": " + e);
      }
    }
    return totals;
  }

  int64_t seed = 1;
  int64_t run_ms = 3000;
  std::shared_ptr<rclcpp::Executor> executor;
  std::shared_ptr<rclcpp_components::ComponentManager> container;
  rclcpp::Node::SharedPtr driver;
  std::thread spin_thread;
  // An exception out of spin() while rclcpp is running fails the test. After
  // rclcpp::shutdown() rclcpp_action's server can throw from the executor
  // (rcl refuses its publishers once the context is invalid); that is counted
  // and allowed (docs/unsupported.md).
  std::string spin_error;
  int spin_exceptions_after_shutdown = 0;
  rclcpp::Client<LoadNode>::SharedPtr load;
  std::set<std::string> loaded_names;
  std::vector<rclcpp::Client<GetStats>::SharedPtr> stats_clients;
};

}  // namespace

/// The short mix (CI gate): every workload is bounded (see the comments).
static std::vector<Component> short_mix()
{
  using P = rclcpp::Parameter;
  // Reliable flood: 3 topics, each with 3 publishers (2 typed in flood_a, 1
  // generic in flood_b) and 2 subscriptions (one typed, one generic).
  const int64_t kMessages = 300;
  return {
    {"TopicFlood", "flood_a", {P("topic_prefix", "flood"), P("topics", 3), P("publishers", 2),
        P("messages", kMessages), P("max_size", 8192), P("wait_for_subscriptions", 2)}},
    {"TopicFlood", "flood_b", {P("topic_prefix", "flood"), P("topics", 3), P("publishers", 1),
        P("messages", kMessages), P("max_size", 8192), P("wait_for_subscriptions", 2),
        P("generic", true)}},
    // Large payloads (0.5-2 MB).
    {"TopicFlood", "flood_big", {P("topic_prefix", "big"), P("topics", 1), P("publishers", 1),
        P("messages", 20), P("min_size", 512 * 1024), P("max_size", 2 * 1024 * 1024)}},
    // Best effort, small depth: loss allowed, corruption/duplicates/reordering
    // not. Bounded: an endless firehose starves rclcpp's StaticSingleThreaded
    // executor, which runs one executable per wait, subscriptions first
    // (rclcpp behavior, any RMW); everything must drain once it ends.
    {"TopicFlood", "flood_be", {P("topic_prefix", "be"), P("topics", 2), P("publishers", 2),
        P("messages", 20000), P("max_size", 1024), P("reliable", false), P("depth", 5)}},
    // Without intra-process comms: with them, rclcpp's IntraProcessManager
    // aborts ("Resource deadlock avoided") when a publisher is destroyed while
    // another thread creates a subscription (upstream defect, RMW-independent;
    // docs/unsupported.md).
    {"EntityChurn", "churn", {P("threads", 2), P("graph_threads", 1)}, false},
    // Bounded for the same reason: endless back-to-back requests keep the
    // servers always ready, and the StaticSingleThreaded executor then never
    // reaches the stats service that would stop them (livelock).
    {"ServiceStorm", "storm_a", {P("servers", 2), P("clients", 3), P("requests", 1000)}},
    {"ServiceStorm", "storm_b", {P("serve", false), P("clients", 3), P("requests", 1000)}},
    {"ActionStorm", "actions_a", {P("clients", 3), P("goals", 50)}},
    {"ActionStorm", "actions_b", {P("serve", false), P("clients", 2), P("goals", 50)}},
    // Parameters set remotely on this node and on another component.
    {"ParamChurn", "params_a", {P("setters", 2), P("sets", 200)}},
    {"ParamChurn", "params_b", {P("setters", 2), P("sets", 200), P("target", "/params_a")}},
    // A lifecycle node driven through its services by a manager component.
    // Without intra-process comms, like EntityChurn: on_cleanup destroys the
    // lifecycle publisher, and with them rclcpp's IntraProcessManager can
    // deadlock on its own lock (upstream, docs/unsupported.md).
    {"LifecycleChurn", "lc_managed", {}, false},
    {"LifecycleChurn", "lc_manager", {P("manage", "/lc_managed"), P("cycles", 50)}, false},
    // Callbacks that publish, call services (deferred response), block, and
    // destroy their own entities.
    {"ReentrantCallbacks", "reentrant", {}},
    // Exact QoS delivery (late joiners, history, depth, lifespan) under load.
    {"QosMatrix", "qos", {P("threads", 2), P("loops", 10)}},
  };
}

TEST_P(Torture, ShortMix)
{
  SCOPED_TRACE("TORTURE_SEED=" + std::to_string(seed));
  const std::vector<Component> mix = short_mix();
  ASSERT_NO_FATAL_FAILURE(load_mix(mix));
  const bool floods = has("flood_a") && has("flood_b");
  const bool big = has("flood_big");
  const bool best_effort = has("flood_be");
  const bool churn = has("churn");
  const bool storms = has("storm_a");
  const bool actions = has("actions_a");
  const bool params = has("params_a");
  const bool lifecycle = has("lc_managed") && has("lc_manager");
  const bool reentrant = has("reentrant");
  const bool qos = has("qos");
  std::this_thread::sleep_for(std::chrono::milliseconds(run_ms));

  Totals totals = collect(true);
  // Drain: reliable topics deliver every published message to each of their
  // subscriptions (stopped publishers publish nothing new); every request and
  // goal in flight completes.
  auto drained = [&](const Totals & t) {
      for (int i = 0; floods && i < 3; ++i) {
        const std::string topic = "flood_" + std::to_string(i);
        if (t.get("received." + topic) != 2 * t.get("published." + topic)) {
          return false;
        }
      }
      return t.get("received.big_0") == t.get("published.big_0") &&
             t.get("responses") + t.get("timeouts") == t.get("requests") &&
             t.get("server_finished") == t.get("server_accepted") &&
             t.get("goals_sent") == t.get("succeeded") + t.get("canceled") +
             t.get("aborted") + t.get("rejected") + t.get("timeouts_goal") +
             t.get("result_unknown") &&
             t.get("setters_done") == t.get("setters") &&
             t.get("managers_done") == t.get("managers") &&
             t.get("reentrant_done") == t.get("reentrant") &&
             t.get("qos_threads_done") == t.get("qos_threads");
    };
  const auto deadline = std::chrono::steady_clock::now() + 300s;
  while (!drained(totals) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(100ms);
    totals = collect(false);
  }

  for (const auto & e : totals.errors) {
    ADD_FAILURE() << e;
  }
  EXPECT_EQ(0, totals.get("errors"));
  EXPECT_EQ(0, totals.get("corrupt"));
  EXPECT_EQ(0, totals.get("duplicate"));
  EXPECT_EQ(0, totals.get("out_of_order"));
  if (floods) {
    for (int i = 0; i < 3; ++i) {
      const std::string topic = "flood_" + std::to_string(i);
      EXPECT_GT(totals.get("published." + topic), 0) << topic;
      EXPECT_EQ(2 * totals.get("published." + topic), totals.get("received." + topic)) << topic;
    }
  }
  if (big) {
    EXPECT_GT(totals.get("published.big_0"), 0);
    EXPECT_EQ(totals.get("published.big_0"), totals.get("received.big_0"));
  }
  if (best_effort) {
    EXPECT_GT(totals.get("received.be_0") + totals.get("received.be_1"), 0);
  }
  if (storms) {
    EXPECT_GT(totals.get("requests"), 0);
    EXPECT_EQ(totals.get("requests"), totals.get("responses"));
    EXPECT_EQ(0, totals.get("timeouts"));
    EXPECT_EQ(0, totals.get("wrong_response"));
  }
  if (actions) {
    // Every goal a surviving client sent reached a terminal state with a
    // correct result; the server finished every goal it accepted (also those
    // of churned clients that are gone).
    EXPECT_GT(totals.get("succeeded"), 0);
    EXPECT_EQ(
      totals.get("goals_sent"),
      totals.get("succeeded") + totals.get("canceled") + totals.get("aborted") +
      totals.get("rejected") + totals.get("timeouts_goal") + totals.get("result_unknown"));
    EXPECT_EQ(0, totals.get("aborted"));
    EXPECT_EQ(0, totals.get("bad_result"));
    EXPECT_EQ(0, totals.get("bad_feedback"));
    EXPECT_EQ(totals.get("server_accepted"), totals.get("server_finished"));
    EXPECT_GT(totals.get("churn_clients"), 0);
  }
  if (params) {
    // Values reached the target in order (parameter events), and each
    // parameter holds the last value set (read back after stop).
    EXPECT_GT(totals.get("sets"), 0);
    EXPECT_GT(totals.get("events"), 0);
    EXPECT_EQ(totals.get("setters"), totals.get("setters_done"));
    EXPECT_EQ(0, totals.get("set_failed"));
    EXPECT_EQ(0, totals.get("event_out_of_order"));
    EXPECT_EQ(0, totals.get("final_mismatch"));
  }
  if (lifecycle) {
    EXPECT_GT(totals.get("cycles_done"), 0);
    EXPECT_EQ(totals.get("managers"), totals.get("managers_done"));
    EXPECT_EQ(0, totals.get("transition_failed"));
    EXPECT_EQ(0, totals.get("state_mismatch"));
    EXPECT_EQ(0, totals.get("event_chain_broken"));
    EXPECT_GT(totals.get("configured"), 0);
  }
  if (reentrant) {
    EXPECT_EQ(totals.get("reentrant"), totals.get("reentrant_done"));
    EXPECT_EQ(1, totals.get("pingpong_done"));
    EXPECT_EQ(0, totals.get("pingpong_bad"));
    EXPECT_EQ(1, totals.get("relay_done"));
    EXPECT_EQ(totals.get("relay_requests"), totals.get("relay_responses"));
    EXPECT_EQ(0, totals.get("relay_wrong"));
    EXPECT_EQ(1, totals.get("self_done"));
    EXPECT_GT(totals.get("blocked_calls"), 0);
  }
  if (qos) {
    EXPECT_EQ(totals.get("qos_threads"), totals.get("qos_threads_done"));
    EXPECT_GT(totals.get("qos_cases"), 0);
    EXPECT_EQ(0, totals.get("qos_mismatch"));
  }
  if (churn) {
    EXPECT_GT(totals.get("cycles"), 0);
    EXPECT_EQ(0, totals.get("live"));
    // The churned entities are gone from the graph.
    for (const auto & kv : driver->get_topic_names_and_types()) {
      EXPECT_EQ(std::string::npos, kv.first.find("/churn_")) << kv.first;
    }
    for (const auto & kv : driver->get_service_names_and_types()) {
      EXPECT_EQ(std::string::npos, kv.first.find("/churn_")) << kv.first;
    }
    for (const auto & n : driver->get_node_names()) {
      EXPECT_EQ(std::string::npos, n.find("/churn_node_")) << n;
    }
  }
  if (HasFailure()) {
    std::string dump;
    for (const auto & kv : totals.sum) {
      dump += "  " + kv.first + " = " + std::to_string(kv.second) + "\n";
    }
    ADD_FAILURE() << "counters (summed over components):\n" << dump;
  }
}

TEST_P(Torture, ShutdownDuringMix)
{
  // rclcpp::shutdown() while the components are busy (only the action storms
  // and entity churn are stopped first, see below; nothing is drained
  // otherwise), then teardown: the executor stops, the components are
  // destroyed (their threads give up waiting for replies that will never
  // come) and the context goes away. Each test re-initializes rclcpp, so the
  // suite also exercises repeated init/shutdown in one process. Passing means
  // no crash, hang, sanitizer report or leak; the counters cannot be read
  // after shutdown, so they are checked just before it.
  SCOPED_TRACE("TORTURE_SEED=" + std::to_string(seed));
  ASSERT_NO_FATAL_FAILURE(load_mix(short_mix()));
  std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(run_ms, 1000)));
  // Quiesce the action storms first: after rclcpp::shutdown() an action server
  // that still receives goal requests throws from the executor, and on a
  // MultiThreadedExecutor worker thread nothing can catch that, so the process
  // aborts (upstream, docs/unsupported.md). Everything else stays busy.
  if (has("actions_a")) {
    auto quiet = [](const Totals & t) {
        return t.get("server_finished") == t.get("server_accepted") &&
               t.get("goals_sent") == t.get("succeeded") + t.get("canceled") +
               t.get("aborted") + t.get("rejected") + t.get("timeouts_goal") +
               t.get("result_unknown") &&
               t.get("churn_done") == t.get("churns") &&
               // every goal request sent (real and churned) reached the server
               t.get("server_goal_requests") + t.get("server_churn_goal_requests") ==
               t.get("goals_sent") + t.get("abandoned_goals");
      };
    Totals actions = collect(true, "/actions_");
    const auto quiet_deadline = std::chrono::steady_clock::now() + 120s;
    while (!quiet(actions) && std::chrono::steady_clock::now() < quiet_deadline) {
      std::this_thread::sleep_for(50ms);
      actions = collect(false, "/actions_");
    }
    ASSERT_TRUE(quiet(actions)) << "action storms did not quiesce";
  }
  // Likewise EntityChurn (its stop joins the churn threads): rcl_shutdown
  // finalizes process-wide rcl state without locks (e.g. rosout's per-node
  // logger map) while churn threads create and destroy nodes and entities in
  // the same context (TSan, CI; docs/unsupported.md). Message traffic,
  // services, parameters and lifecycle transitions keep running.
  if (has("churn")) {
    const Totals churn = collect(true, "/churn");
    ASSERT_EQ(0, churn.get("live")) << "entity churn did not stop";
  }
  const Totals before = collect(false);
  for (const auto & e : before.errors) {
    ADD_FAILURE() << e;
  }
  EXPECT_EQ(0, before.get("errors"));
  EXPECT_TRUE(rclcpp::shutdown());
  // spin() returns on shutdown; TearDown joins and destroys everything.
  const auto deadline = std::chrono::steady_clock::now() + 60s;
  while (rclcpp::ok() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_FALSE(rclcpp::ok());
}

INSTANTIATE_TEST_SUITE_P(
  AllExecutors, Torture,
  ::testing::Values(
    Config{ExecutorKind::kSingleThreaded, false}, Config{ExecutorKind::kSingleThreaded, true},
    Config{ExecutorKind::kStaticSingleThreaded, false},
    Config{ExecutorKind::kStaticSingleThreaded, true},
    Config{ExecutorKind::kMultiThreaded, false}, Config{ExecutorKind::kMultiThreaded, true},
    Config{ExecutorKind::kEvents, false}, Config{ExecutorKind::kEvents, true}),
  ConfigName());
