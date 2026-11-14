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

// ReentrantCallbacks: callbacks that publish, call services, block, and
// destroy and recreate their own entities.
//   - Ping-pong: the ~/ping callback publishes the next hop on ~/pong and
//     vice versa; the hop counter must never skip or repeat.
//   - Relay: ~/relay answers with a deferred response: its callback forwards
//     the request to ~/echo and responds from the client's response callback.
//     A caller thread checks every answer.
//   - Blocking: a callback in its own group blocks on every message of
//     ~/slow while the rest goes on (with a single-threaded executor the
//     rest waits; nothing may deadlock).
//   - Self-destruction (on a private node with its own single-threaded
//     executor thread, so one thread owns all its entities: rcl entity
//     init/fini on one node is not thread-safe): a timer creates a publisher
//     and a subscription; the subscription callback destroys itself and the
//     publisher; one-shot timers destroy themselves.
// No callback throws: rclcpp propagates a callback's exception out of spin().
//
// Parameters: hops (int, 2000), relays (int, 200), slow (int, 20),
//   self_cycles (int, 200).
// Stats: pingpong_hops, pingpong_bad, pingpong_done, relay_requests,
// relay_responses, relay_wrong, relay_done, blocked_calls, self_cycles,
// self_done; reentrant / reentrant_done (the driver waits for them).

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

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

using test_msgs::msg::BasicTypes;
using Srv = test_msgs::srv::BasicTypes;

class ReentrantCallbacks : public TortureNode
{
public:
  explicit ReentrantCallbacks(const rclcpp::NodeOptions & options)
  : TortureNode("reentrant_callbacks", options)
  {
    hops_ = declare_parameter("hops", 2000);
    relays_ = declare_parameter("relays", 200);
    slow_ = declare_parameter("slow", 20);
    self_cycles_ = declare_parameter("self_cycles", 200);
    set("reentrant", 4);  // ping-pong, relay, blocking, self-destruction

    const auto qos = rclcpp::QoS(100).reliable();
    ping_pub_ = create_publisher<BasicTypes>("~/ping", qos);
    pong_pub_ = create_publisher<BasicTypes>("~/pong", qos);
    ping_sub_ = create_subscription<BasicTypes>(
      "~/ping", qos, [this](const BasicTypes & m) {hop(m, pong_pub_);});
    pong_sub_ = create_subscription<BasicTypes>(
      "~/pong", qos, [this](const BasicTypes & m) {hop(m, ping_pub_);});

    echo_ = create_service<Srv>(
      "~/echo", [](const std::shared_ptr<Srv::Request> req, std::shared_ptr<Srv::Response> res) {
        res->int64_value = req->int64_value * 2;
      });
    echo_client_ = create_client<Srv>("~/echo");
    relay_ = create_service<Srv>(
      "~/relay",
      [this](
        std::shared_ptr<rclcpp::Service<Srv>> service, std::shared_ptr<rmw_request_id_t> header,
        const std::shared_ptr<Srv::Request> req) {
        // Deferred response: forward, answer from the client callback.
        echo_client_->async_send_request(
          req, [this, service, header](rclcpp::Client<Srv>::SharedFuture f) {
            Srv::Response res;
            res.int64_value = f.get()->int64_value + 1;
            service->send_response(*header, res);
            add("relay_forwarded");
          });
      });
    relay_client_ = create_client<Srv>("~/relay");

    slow_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    rclcpp::SubscriptionOptions slow_options;
    slow_options.callback_group = slow_group_;
    slow_pub_ = create_publisher<BasicTypes>("~/slow", qos);
    slow_sub_ = create_subscription<BasicTypes>(
      "~/slow", qos, [this](const BasicTypes &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        add("blocked_calls");
        if (blocked_.fetch_add(1) + 1 == slow_) {
          add("reentrant_done");
        }
      }, slow_options);

    threads_.emplace_back([this]() {start();});
    threads_.emplace_back([this]() {relay_loop();});
    threads_.emplace_back([this]() {self_destruction();});
  }

  ~ReentrantCallbacks() override
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
  void hop(const BasicTypes & m, const rclcpp::Publisher<BasicTypes>::SharedPtr & next)
  {
    const int64_t expected = last_hop_.load() + 1;
    if (m.int64_value != expected) {
      add("pingpong_bad");
      error("hop " + std::to_string(m.int64_value) + ", expected " + std::to_string(expected));
    }
    last_hop_ = m.int64_value;
    add("pingpong_hops");
    if (m.int64_value >= hops_) {
      add("pingpong_done");
      add("reentrant_done");
      return;
    }
    BasicTypes out;
    out.int64_value = m.int64_value + 1;
    next->publish(out);  // from inside a subscription callback
  }

  void start()
  {
    // Ping-pong and the slow topic start once the subscriptions match.
    while (!stop_ && (ping_pub_->get_subscription_count() == 0 ||
      pong_pub_->get_subscription_count() == 0 || slow_pub_->get_subscription_count() == 0))
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (stop_) {
      return;
    }
    BasicTypes first;
    first.int64_value = 1;
    ping_pub_->publish(first);
    for (int64_t i = 0; i < slow_; ++i) {
      slow_pub_->publish(BasicTypes());
    }
  }

  void relay_loop()
  {
    try {
      while (!stop_ && !relay_client_->wait_for_service(std::chrono::milliseconds(100))) {
      }
      for (int64_t i = 1; !stop_ && i <= relays_; ++i) {
        struct Box
        {
          std::mutex mutex;
          std::condition_variable cv;
          std::optional<int64_t> value;
        };
        auto box = std::make_shared<Box>();
        auto req = std::make_shared<Srv::Request>();
        req->int64_value = i;
        relay_client_->async_send_request(
          req, [box](rclcpp::Client<Srv>::SharedFuture f) {
            {
              std::lock_guard<std::mutex> lock(box->mutex);
              box->value = f.get()->int64_value;
            }
            box->cv.notify_all();
          });
        add("relay_requests");
        std::unique_lock<std::mutex> lock(box->mutex);
        if (!wait_or_stop(
            box->cv, lock, std::chrono::seconds(60), abort_,
            [&box] {return box->value.has_value();}))
        {
          if (abort_) {
            break;  // torn down: no reply will come
          }
          error("relay request " + std::to_string(i) + " timed out");
          return;
        }
        add("relay_responses");
        if (*box->value != 2 * i + 1) {
          add("relay_wrong");
          error("relay answered " + std::to_string(*box->value) + " for " + std::to_string(i));
        }
      }
      add("relay_done");
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("relay: ") + e.what());
      }
    }
    add("reentrant_done");
  }

  void self_destruction()
  {
    try {
      auto node = std::make_shared<rclcpp::Node>(
        std::string(get_name()) + "_self", get_namespace(),
        rclcpp::NodeOptions().context(get_node_base_interface()->get_context())
        .start_parameter_services(false).start_parameter_event_publisher(false));
      rclcpp::ExecutorOptions exec_options;
      exec_options.context = get_node_base_interface()->get_context();
      rclcpp::executors::SingleThreadedExecutor exec(exec_options);
      exec.add_node(node);
      int64_t cycles = 0;
      rclcpp::Publisher<BasicTypes>::SharedPtr pub;
      rclcpp::Subscription<BasicTypes>::SharedPtr sub;
      rclcpp::TimerBase::SharedPtr one_shot;
      const std::string topic = std::string(get_name()) + "_self_topic";
      auto driver = node->create_wall_timer(
        std::chrono::milliseconds(1), [&]() {
          if (sub || cycles >= self_cycles_) {
            return;
          }
          pub = node->create_publisher<BasicTypes>(topic, 10);
          // The subscription destroys itself (and the publisher) in its
          // own callback.
          sub = node->create_subscription<BasicTypes>(
            topic, 10, [&](const BasicTypes &) {
              sub.reset();
              pub.reset();
              ++cycles;
              add("self_cycles");
            });
          // A one-shot timer that destroys itself.
          one_shot = node->create_wall_timer(
            std::chrono::milliseconds(0), [&]() {one_shot.reset();});
          pub->publish(BasicTypes());
        });
      const auto context = node->get_node_base_interface()->get_context();
      while (!stop_ && cycles < self_cycles_ && rclcpp::ok(context)) {
        exec.spin_some(std::chrono::milliseconds(10));
      }
      driver.reset();
      one_shot.reset();
      sub.reset();
      pub.reset();
      exec.remove_node(node);
      if (cycles >= self_cycles_) {
        add("self_done");
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("self destruction: ") + e.what());
      }
    }
    add("reentrant_done");
  }

  int64_t hops_ = 2000;
  int64_t relays_ = 200;
  int64_t slow_ = 20;
  int64_t self_cycles_ = 200;
  std::atomic<int64_t> last_hop_{0};
  std::atomic<int64_t> blocked_{0};
  rclcpp::Publisher<BasicTypes>::SharedPtr ping_pub_;
  rclcpp::Publisher<BasicTypes>::SharedPtr pong_pub_;
  rclcpp::Subscription<BasicTypes>::SharedPtr ping_sub_;
  rclcpp::Subscription<BasicTypes>::SharedPtr pong_sub_;
  rclcpp::Service<Srv>::SharedPtr echo_;
  rclcpp::Client<Srv>::SharedPtr echo_client_;
  rclcpp::Service<Srv>::SharedPtr relay_;
  rclcpp::Client<Srv>::SharedPtr relay_client_;
  rclcpp::CallbackGroup::SharedPtr slow_group_;
  rclcpp::Publisher<BasicTypes>::SharedPtr slow_pub_;
  rclcpp::Subscription<BasicTypes>::SharedPtr slow_sub_;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::ReentrantCallbacks)
