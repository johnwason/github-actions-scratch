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

// B12 benchmark host: the bridge plus a node publishing large messages
// (docs/benchmark.md). Parameters of /bench_publisher:
//   kind: "image" (sensor_msgs/Image, rgb8) or "cloud" (sensor_msgs/PointCloud2,
//         x y z intensity float32)
//   width, height: image size, or the cloud's points (width x height)
//   rate: messages per second (at most 10000; the timer falls behind when
//         publishing is slower)
// header.stamp is the system time just before publish(). Every second the
// host prints "BENCH published <n> publish_us <mean time in publish()>".

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "robotraconteur_ros2_debug_bridge/bridge.hpp"
#include "stdin_stop.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

using namespace std::chrono_literals;

namespace
{

class BenchPublisher : public rclcpp::Node
{
public:
  BenchPublisher()
  : rclcpp::Node("bench_publisher"), system_clock_(RCL_SYSTEM_TIME)
  {
    kind_ = declare_parameter("kind", std::string("image"));
    const auto width = static_cast<uint32_t>(declare_parameter("width", 1280));
    const auto height = static_cast<uint32_t>(declare_parameter("height", 720));
    const double rate = declare_parameter("rate", 30.0);
    if (kind_ == "image") {
      image_.width = width;
      image_.height = height;
      image_.encoding = "rgb8";
      image_.step = width * 3;
      image_.data.resize(static_cast<size_t>(image_.step) * height);
      for (size_t i = 0; i < image_.data.size(); ++i) {
        image_.data[i] = static_cast<uint8_t>(i);
      }
      image_pub_ = create_publisher<sensor_msgs::msg::Image>("/bench/image", 10);
    } else if (kind_ == "cloud") {
      cloud_.width = width;
      cloud_.height = height;
      cloud_.point_step = 16;
      cloud_.row_step = cloud_.point_step * width;
      cloud_.is_dense = true;
      const char * names[] = {"x", "y", "z", "intensity"};
      for (uint32_t i = 0; i < 4; ++i) {
        sensor_msgs::msg::PointField f;
        f.name = names[i];
        f.offset = 4 * i;
        f.datatype = sensor_msgs::msg::PointField::FLOAT32;
        f.count = 1;
        cloud_.fields.push_back(f);
      }
      cloud_.data.resize(static_cast<size_t>(cloud_.row_step) * height);
      for (size_t i = 0; i < cloud_.data.size(); ++i) {
        cloud_.data[i] = static_cast<uint8_t>(i * 7);
      }
      cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>("/bench/cloud", 10);
    } else {
      throw std::invalid_argument("kind must be image or cloud");
    }
    if (!(rate > 0 && rate <= 10000)) {
      throw std::invalid_argument("rate must be in (0, 10000]");
    }
    const auto period = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / rate));
    timer_ = create_wall_timer(period, [this]() {publish();});
    report_ = create_wall_timer(1s, [this]() {report();});
  }

private:
  void publish()
  {
    const auto start = std::chrono::steady_clock::now();
    if (image_pub_) {
      image_.header.stamp = system_clock_.now();
      image_pub_->publish(image_);
    } else {
      cloud_.header.stamp = system_clock_.now();
      cloud_pub_->publish(cloud_);
    }
    publish_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
    ++published_;
  }

  void report()
  {
    std::printf(
      "BENCH published %" PRIu64 " publish_us %.1f\n", published_,
      published_ > 0 ? 1e-3 * static_cast<double>(publish_ns_) / static_cast<double>(published_) :
      0.0);
    std::fflush(stdout);
    published_ = 0;
    publish_ns_ = 0;
  }

  // Both timers are in this node's default (mutually exclusive) group and
  // the host runs a single-threaded executor: no locking.
  std::string kind_;
  rclcpp::Clock system_clock_;
  sensor_msgs::msg::Image image_;
  sensor_msgs::msg::PointCloud2 cloud_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
  rclcpp::TimerBase::SharedPtr timer_, report_;
  uint64_t published_ = 0;
  int64_t publish_ns_ = 0;
};

}  // namespace

int main(int argc, char ** argv)
{
  namespace bridge_ns = robotraconteur_ros2_debug_bridge;
  const bool stop_on_stdin_close =
    bridge_ns::take_flag(argc, argv, bridge_ns::kStopOnStdinCloseFlag);
  rclcpp::init(argc, argv);
  if (stop_on_stdin_close) {
    bridge_ns::shutdown_when_stdin_closes();
  }
  int code = 0;
  try {
    auto bridge = robotraconteur_ros2_debug_bridge::make_bridge_node();
    auto bench = std::make_shared<BenchPublisher>();
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(bridge);
    exec.add_node(bench);
    RCLCPP_INFO(rclcpp::get_logger("benchmark_host"), "READY");
    exec.spin();
    exec.remove_node(bench);
    exec.remove_node(bridge);
    bench.reset();
    bridge.reset();
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("benchmark_host"), "%s", e.what());
    code = 1;
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return code;
}
