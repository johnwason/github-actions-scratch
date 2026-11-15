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

// Standalone bridge: the bridge node composed with a small demo node
// (publishes std_msgs/String on /bridge_demo/chatter once a second), on one
// MultiThreadedExecutor, until Ctrl+C. Applications normally load the
// component into their own process instead (for example with container_host
// from ../rmw_intraprocess).
//
//   debug_bridge [--stop-on-stdin-close] [--ros-args -p config_file:=my_config.yaml]
//
// --stop-on-stdin-close also stops it when stdin closes (src/stdin_stop.hpp).

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "robotraconteur_ros2_debug_bridge/bridge.hpp"
#include "stdin_stop.hpp"
#include "std_msgs/msg/string.hpp"

namespace
{

class DemoNode : public rclcpp::Node
{
public:
  DemoNode()
  : rclcpp::Node("bridge_demo")
  {
    pub_ = create_publisher<std_msgs::msg::String>("~/chatter", 10);
    timer_ = create_wall_timer(
      std::chrono::seconds(1), [this]() {
        std_msgs::msg::String m;
        m.data = "hello " + std::to_string(count_++);
        pub_->publish(m);
      });
  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  int count_ = 0;
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
    auto demo = std::make_shared<DemoNode>();
    rclcpp::executors::MultiThreadedExecutor exec;
    exec.add_node(bridge);
    exec.add_node(demo);
    exec.spin();
    const auto logger = rclcpp::get_logger("debug_bridge");
    RCLCPP_INFO(logger, "Executor stopped; shutting down");
    exec.remove_node(demo);
    exec.remove_node(bridge);
    demo.reset();
    bridge.reset();  // stops Robot Raconteur
    RCLCPP_INFO(logger, "Bridge destroyed");
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("debug_bridge"), "%s", e.what());
    code = 1;
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return code;
}
