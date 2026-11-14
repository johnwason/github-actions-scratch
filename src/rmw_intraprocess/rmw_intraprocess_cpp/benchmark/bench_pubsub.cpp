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

// M10 benchmark (not a test): rclcpp publish/subscribe latency and throughput
// in one process, for the RMW selected by RMW_IMPLEMENTATION. rclcpp's
// intra-process comms are off, so the RMW carries every message.
//   latency:    ping-pong between two nodes on one SingleThreadedExecutor;
//               reports the mean and median round trip.
//   throughput: one publisher bursts N messages (RELIABLE, KEEP_LAST deep
//               enough to lose none) to a subscription spun on another
//               thread; reports messages/s and MB/s until the last arrives.
// Payload: std_msgs/UInt8MultiArray.data of the given size.
// Output: one tab-separated line per measurement.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

using Msg = std_msgs::msg::UInt8MultiArray;
using Clock = std::chrono::steady_clock;

namespace
{

rclcpp::NodeOptions options()
{
  return rclcpp::NodeOptions().use_intra_process_comms(false)
         .start_parameter_services(false).start_parameter_event_publisher(false);
}

void latency(size_t size, int rounds)
{
  auto a = std::make_shared<rclcpp::Node>("bench_ping", options());
  auto b = std::make_shared<rclcpp::Node>("bench_pong", options());
  const auto qos = rclcpp::QoS(10).reliable();
  auto ping = a->create_publisher<Msg>("bench_ping", qos);
  auto pong = b->create_publisher<Msg>("bench_pong", qos);
  auto echo = b->create_subscription<Msg>(
    "bench_ping", qos, [&pong](Msg::UniquePtr m) {pong->publish(std::move(m));});
  std::vector<double> rtt_us;
  rtt_us.reserve(static_cast<size_t>(rounds));
  Clock::time_point sent;
  int done = 0;
  Msg payload;
  payload.data.resize(size, 0xAB);
  auto back = a->create_subscription<Msg>(
    "bench_pong", qos, [&](const Msg &) {
      rtt_us.push_back(
        std::chrono::duration<double, std::micro>(Clock::now() - sent).count());
      if (++done < rounds) {
        sent = Clock::now();
        ping->publish(payload);
      }
    });
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(a);
  exec.add_node(b);
  while (ping->get_subscription_count() == 0 || pong->get_subscription_count() == 0) {
    exec.spin_some(std::chrono::milliseconds(1));
  }
  sent = Clock::now();
  ping->publish(payload);
  while (done < rounds) {
    exec.spin_once(std::chrono::milliseconds(100));
  }
  std::sort(rtt_us.begin(), rtt_us.end());
  double sum = 0;
  for (double v : rtt_us) {
    sum += v;
  }
  std::printf(
    "latency\t%zu\tmean_rtt_us\t%.2f\tmedian_rtt_us\t%.2f\tp99_rtt_us\t%.2f\n", size,
    sum / static_cast<double>(rtt_us.size()), rtt_us[rtt_us.size() / 2],
    rtt_us[rtt_us.size() * 99 / 100]);
}

void throughput(size_t size, int count)
{
  auto pub_node = std::make_shared<rclcpp::Node>("bench_tx", options());
  auto sub_node = std::make_shared<rclcpp::Node>("bench_rx", options());
  const auto qos = rclcpp::QoS(static_cast<size_t>(count)).reliable();
  auto pub = pub_node->create_publisher<Msg>("bench_burst", qos);
  std::atomic<int> received{0};
  auto sub = sub_node->create_subscription<Msg>(
    "bench_burst", qos, [&received](const Msg &) {++received;});
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(sub_node);
  std::thread spin([&exec]() {exec.spin();});
  while (pub->get_subscription_count() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Msg payload;
  payload.data.resize(size, 0xCD);
  const auto start = Clock::now();
  for (int i = 0; i < count; ++i) {
    pub->publish(payload);
  }
  while (received.load() < count) {
    std::this_thread::yield();
  }
  const double s = std::chrono::duration<double>(Clock::now() - start).count();
  exec.cancel();
  spin.join();
  std::printf(
    "throughput\t%zu\tmsgs_per_s\t%.0f\tMB_per_s\t%.1f\n", size, count / s,
    static_cast<double>(size) * count / s / 1e6);
}

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  const char * rmw = std::getenv("RMW_IMPLEMENTATION");
  std::printf("rmw\t%s\n", rmw != nullptr ? rmw : "(default)");
  for (size_t size : {size_t{64}, size_t{4096}, size_t{262144}, size_t{1048576}}) {
    const int rounds = size >= 262144 ? 500 : 5000;
    latency(size, rounds);
    throughput(size, rounds);
  }
  rclcpp::shutdown();
  return 0;
}
