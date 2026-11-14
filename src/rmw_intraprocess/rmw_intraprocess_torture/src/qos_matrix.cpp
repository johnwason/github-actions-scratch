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

// QosMatrix: exact delivery for every reliability x durability x history
// combination with a late-joining subscription, plus lifespan expiry, while
// the rest of the mix loads the process. Expectations follow this RMW's
// documented semantics (docs/design.md, "QoS behavior"):
//   - the publisher publishes kBefore samples, then the subscription is
//     created (same QoS), then kAfter more samples are published;
//   - TRANSIENT_LOCAL: the late joiner first gets the publisher's history
//     (KEEP_LAST d: the last min(d, kBefore); KEEP_ALL: all), VOLATILE: none;
//   - the subscription queue keeps the newest d samples (KEEP_ALL: all);
//   - so exactly the newest q samples are taken, in order, ending at the last
//     one published (q = KEEP_ALL ? h + kAfter : min(d, h + kAfter)).
//   - Lifespan (TRANSIENT_LOCAL, KEEP_LAST 5, lifespan 1 s, once per
//     component; a sample taken right after publishing must still be alive,
//     1 s leaves ample margin even under sanitizers): expired
//     samples are neither replayed to a late joiner nor taken.
// Each worker thread uses its own node, which is never spun: samples are
// taken directly, so no executor timing is involved (rcl entity init/fini on
// one node from one thread only).
//
// Parameters: threads (int, 2), loops (int, 10): matrix runs per thread.
// Stats: qos_cases, qos_mismatch, qos_threads / qos_threads_done.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

using test_msgs::msg::BasicTypes;

class QosMatrix : public TortureNode
{
public:
  explicit QosMatrix(const rclcpp::NodeOptions & options)
  : TortureNode("qos_matrix", options)
  {
    const auto threads = declare_parameter("threads", 2);
    loops_ = declare_parameter("loops", 10);
    set("qos_threads", threads);
    for (int64_t i = 0; i < threads; ++i) {
      threads_.emplace_back(
        [this, i]() {
          run(i);
          add("qos_threads_done");
        });
    }
  }

  ~QosMatrix() override
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
  static constexpr int64_t kBefore = 8;
  static constexpr int64_t kAfter = 8;

  /// Takes every sample now in the subscription's queue.
  static std::vector<int64_t> take_all(rclcpp::Subscription<BasicTypes> & sub)
  {
    std::vector<int64_t> seqs;
    BasicTypes m;
    rclcpp::MessageInfo info;
    while (sub.take(m, info)) {
      seqs.push_back(m.int64_value);
    }
    return seqs;
  }

  void publish(rclcpp::Publisher<BasicTypes> & pub, int64_t first, int64_t count)
  {
    for (int64_t s = first; s < first + count; ++s) {
      BasicTypes m;
      m.int64_value = s;
      pub.publish(m);
    }
  }

  void check(
    const std::string & name, const std::vector<int64_t> & got, int64_t expected,
    int64_t last)
  {
    add("qos_cases");
    bool ok = static_cast<int64_t>(got.size()) == expected;
    for (size_t i = 0; ok && i < got.size(); ++i) {
      ok = got[i] == last - expected + 1 + static_cast<int64_t>(i);
    }
    if (!ok) {
      add("qos_mismatch");
      std::string seqs;
      for (int64_t s : got) {
        seqs += " " + std::to_string(s);
      }
      error(name + ": expected the newest " + std::to_string(expected) + " samples ending at " +
        std::to_string(last) + ", got" + seqs);
    }
  }

  void run(int64_t index)
  {
    try {
      auto node = std::make_shared<rclcpp::Node>(
        std::string(get_name()) + "_" + std::to_string(index), get_namespace(),
        rclcpp::NodeOptions().context(get_node_base_interface()->get_context())
        .start_parameter_services(false).start_parameter_event_publisher(false)
        .use_intra_process_comms(false));
      for (int64_t loop = 0; !stop_ && loop < loops_; ++loop) {
        for (bool reliable : {true, false}) {
          for (bool transient : {false, true}) {
            for (int64_t depth : {int64_t{1}, int64_t{5}, int64_t{0}}) {  // 0 = KEEP_ALL
              matrix_case(*node, index, loop, reliable, transient, depth);
            }
          }
        }
        if (index == 0 && loop == 0) {
          lifespan_case(*node, index, loop);  // once: it sleeps
        }
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("qos matrix: ") + e.what());
      }
    }
  }

  void matrix_case(
    rclcpp::Node & node, int64_t index, int64_t loop, bool reliable, bool transient,
    int64_t depth)
  {
    rclcpp::QoS qos = depth == 0 ? rclcpp::QoS(rclcpp::KeepAll()) :
      rclcpp::QoS(static_cast<size_t>(depth));
    if (reliable) {
      qos.reliable();
    } else {
      qos.best_effort();
    }
    if (transient) {
      qos.transient_local();
    } else {
      qos.durability_volatile();
    }
    const std::string name = std::string(get_name()) + "_" + std::to_string(index) + "_" +
      std::to_string(loop) + (reliable ? "_rel" : "_be") + (transient ? "_tl" : "_vol") +
      (depth == 0 ? "_all" : "_d" + std::to_string(depth));
    auto pub = node.create_publisher<BasicTypes>(name, qos);
    publish(*pub, 1, kBefore);
    auto sub = node.create_subscription<BasicTypes>(name, qos, [](const BasicTypes &) {});
    publish(*pub, kBefore + 1, kAfter);
    const int64_t history = !transient ? 0 : (depth == 0 ? kBefore : std::min(depth, kBefore));
    const int64_t queued = depth == 0 ? history + kAfter : std::min(depth, history + kAfter);
    check(name, take_all(*sub), queued, kBefore + kAfter);
  }

  void lifespan_case(rclcpp::Node & node, int64_t index, int64_t loop)
  {
    rclcpp::QoS qos(5);
    qos.reliable().transient_local().lifespan(std::chrono::milliseconds(1000));
    const std::string name = std::string(get_name()) + "_" + std::to_string(index) + "_" +
      std::to_string(loop) + "_lifespan";
    auto pub = node.create_publisher<BasicTypes>(name, qos);
    publish(*pub, 1, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    // History expired: nothing replayed to the late joiner.
    auto sub = node.create_subscription<BasicTypes>(name, qos, [](const BasicTypes &) {});
    check(name + "_replay", take_all(*sub), 0, 3);
    // Fresh samples are taken; samples left to expire in the queue are not.
    publish(*pub, 4, 1);
    check(name + "_fresh", take_all(*sub), 1, 4);
    publish(*pub, 5, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    check(name + "_expired", take_all(*sub), 0, 6);
  }

  int64_t loops_ = 10;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::QosMatrix)
