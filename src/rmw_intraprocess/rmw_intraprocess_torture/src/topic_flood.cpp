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

// TopicFlood: publishers on dedicated threads publish Payload messages as fast
// as they can (sizes random in [min_size, max_size]); subscriptions check every
// message: checksum, per-publisher order, duplicates and gaps.
//
// Parameters:
//   topic_prefix (string, "flood"), topics (int, 2): topics <prefix>_<i>
//   publishers (int, 1): publishers per topic in this component (0 = none)
//   subscribe (bool, true): one subscription per topic in this component
//   messages (int, 1000): messages per publisher; 0 = until stopped
//   min_size / max_size (int, 0 / 4096): data size range in bytes
//   seed (int, 1): random seed (sizes and contents)
//   generic (bool, false): GenericPublisher/GenericSubscription (serialized)
//   reliable (bool, true): RELIABLE + KEEP_LAST(reliable_depth), else
//     BEST_EFFORT + KEEP_LAST(depth). (rclcpp intra-process comms reject
//     KEEP_ALL; a reliable_depth that covers every message in flight is
//     loss-free in both modes.)
//   reliable_depth (int, 1000), depth (int, 10)
//   wait_for_subscriptions (int, 1): each publisher waits until its topic has
//     this many matched subscriptions before publishing (volatile topics)
//
// Stats (per topic t): published.t, received.t, bytes_published.t; and corrupt,
// duplicate, out_of_order, gaps over all topics. With reliable = true every
// subscription must receive every message of every publisher on its topic,
// in order (the driver checks received.t == published.t * subscriptions(t)).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp_components/register_node_macro.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

class TopicFlood : public TortureNode
{
public:
  explicit TopicFlood(const rclcpp::NodeOptions & options)
  : TortureNode("topic_flood", options)
  {
    const auto prefix = declare_parameter("topic_prefix", std::string("flood"));
    const auto topics = declare_parameter("topics", 2);
    const auto publishers = declare_parameter("publishers", 1);
    const bool subscribe = declare_parameter("subscribe", true);
    messages_ = declare_parameter("messages", 1000);
    min_size_ = declare_parameter("min_size", 0);
    max_size_ = declare_parameter("max_size", 4096);
    seed_ = declare_parameter("seed", 1);
    generic_ = declare_parameter("generic", false);
    const bool reliable = declare_parameter("reliable", true);
    const auto depth = declare_parameter("depth", 10);
    const auto reliable_depth = declare_parameter("reliable_depth", 1000);
    wait_for_subscriptions_ = declare_parameter("wait_for_subscriptions", 1);
    reliable_ = reliable;
    qos_ = reliable ? rclcpp::QoS(static_cast<size_t>(reliable_depth)).reliable() :
      rclcpp::QoS(static_cast<size_t>(depth)).best_effort();

    for (int64_t t = 0; t < topics; ++t) {
      const std::string topic = prefix + "_" + std::to_string(t);
      if (subscribe) {
        make_subscription(topic);
      }
      for (int64_t p = 0; p < publishers; ++p) {
        make_publisher(topic);
      }
    }
    for (auto & pub : publishers_) {
      threads_.emplace_back([this, &pub]() {run(pub);});
    }
  }

  ~TopicFlood() override
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
  struct Publisher
  {
    std::string topic;
    uint32_t id;
    rclcpp::Publisher<msg::Payload>::SharedPtr typed;
    rclcpp::GenericPublisher::SharedPtr generic;
  };

  void make_publisher(const std::string & topic)
  {
    Publisher pub;
    pub.topic = topic;
    pub.id = next_id();
    if (generic_) {
      pub.generic = create_generic_publisher(topic, "rmw_intraprocess_torture/msg/Payload", qos_);
    } else {
      pub.typed = create_publisher<msg::Payload>(topic, qos_);
    }
    publishers_.push_back(std::move(pub));
  }

  void make_subscription(const std::string & topic)
  {
    if (generic_) {
      generic_subs_.push_back(
        create_generic_subscription(
          topic, "rmw_intraprocess_torture/msg/Payload", qos_,
          [this, topic](std::shared_ptr<const rclcpp::SerializedMessage> serialized) {
            msg::Payload m;
            serialization_.deserialize_message(serialized.get(), &m);
            check(topic, m);
          }));
    } else {
      subs_.push_back(
        create_subscription<msg::Payload>(
          topic, qos_, [this, topic](const msg::Payload & m) {check(topic, m);}));
    }
  }

  size_t matched(const Publisher & pub) const
  {
    return pub.generic ? pub.generic->get_subscription_count() :
           pub.typed->get_subscription_count();
  }

  void run(Publisher & pub)
  {
    try {
      while (!stop_ && matched(pub) < static_cast<size_t>(wait_for_subscriptions_)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      std::mt19937 rng(static_cast<uint32_t>(seed_) ^ (pub.id * 2654435761u));
      std::uniform_int_distribution<int64_t> size(min_size_, max_size_);
      for (uint64_t seq = 1; !stop_ && (messages_ == 0 || seq <= static_cast<uint64_t>(messages_));
        ++seq)
      {
        auto m = std::make_unique<msg::Payload>();
        m->publisher_id = pub.id;
        m->seq = seq;
        m->data.resize(static_cast<size_t>(size(rng)));
        for (auto & b : m->data) {
          b = static_cast<uint8_t>(rng());
        }
        m->text = pub.topic;
        m->checksum = checksum(*m);
        const auto bytes = static_cast<int64_t>(m->data.size());
        if (pub.generic) {
          rclcpp::SerializedMessage serialized;
          serialization_.serialize_message(m.get(), &serialized);
          pub.generic->publish(serialized);
        } else {
          pub.typed->publish(std::move(m));
        }
        add("published." + pub.topic);
        add("bytes_published." + pub.topic, bytes);
      }
    } catch (const std::exception & e) {
      // rclcpp shutdown while publishing ends the thread.
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("publisher ") + pub.topic + ": " + e.what());
      }
    }
  }

  void check(const std::string & topic, const msg::Payload & m)
  {
    add("received." + topic);
    if (checksum(m) != m.checksum || m.text != topic) {
      add("corrupt");
      error("corrupt message on " + topic + " from " + std::to_string(m.publisher_id));
      return;
    }
    std::lock_guard<std::mutex> lock(seq_mutex_);
    uint64_t & last = last_seq_[topic][m.publisher_id];
    if (m.seq == last) {
      add("duplicate");
      error("duplicate seq " + std::to_string(m.seq) + " on " + topic);
    } else if (m.seq < last) {
      add("out_of_order");
      error("out of order seq " + std::to_string(m.seq) + " < " + std::to_string(last));
    } else {
      if (m.seq > last + 1) {
        add("gaps", static_cast<int64_t>(m.seq - last - 1));
        if (reliable_) {
          error("gap before seq " + std::to_string(m.seq) + " on reliable " + topic);
        }
      }
      last = m.seq;
    }
  }

  int64_t messages_ = 0;
  int64_t min_size_ = 0;
  int64_t max_size_ = 0;
  int64_t seed_ = 0;
  bool generic_ = false;
  bool reliable_ = true;
  int64_t wait_for_subscriptions_ = 1;
  rclcpp::QoS qos_{10};
  rclcpp::Serialization<msg::Payload> serialization_;
  std::vector<Publisher> publishers_;
  std::vector<rclcpp::Subscription<msg::Payload>::SharedPtr> subs_;
  std::vector<rclcpp::GenericSubscription::SharedPtr> generic_subs_;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
  std::mutex seq_mutex_;
  std::map<std::string, std::map<uint32_t, uint64_t>> last_seq_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::TopicFlood)
