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

// B4 test process: the bridge plus ROS nodes to talk to, in one process
// (so it works under rmw_intraprocess_cpp too), until SIGINT.
//
//   /test/chatter       std_msgs/String       "hello <n>", 20 Hz
//   /test/joint_states  sensor_msgs/JointState  j1, j2, 20 Hz
//   /test/latched       std_msgs/String       transient local, published once
//   /test/secret        std_msgs/String       (the tests' config denies it)
//   /test/in_chatter -> /test/echo_chatter, /test/in_joint_states ->
//   /test/echo_joint_states: echoed back
//   /turtle1/pose, /turtle1/cmd_vel: a stand-in for turtlesim (the pose's
//   velocities are the last command's)
//
//   topic_test_host --ros-args -p config_file:=<yaml>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "action_tutorials_interfaces/action/fibonacci.hpp"
#include "example_interfaces/srv/add_two_ints.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "rclcpp_components/component_manager.hpp"
#include "robotraconteur_ros2_debug_bridge/bridge.hpp"
#include "stdin_stop.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/string.hpp"
#include "turtlesim/msg/pose.hpp"

#if defined(__linux__)
#include <sys/prctl.h>  // portability-ok(POSIX-HEADER): test-only backtrace on terminate
#include <unistd.h>  // portability-ok(POSIX-HEADER): test-only backtrace on terminate
#endif

using namespace std::chrono_literals;

namespace
{

class Talker : public rclcpp::Node
{
public:
  Talker()
  : rclcpp::Node("test_talker")
  {
    chatter_ = create_publisher<std_msgs::msg::String>("/test/chatter", 10);
    joints_ = create_publisher<sensor_msgs::msg::JointState>("/test/joint_states", 10);
    secret_ = create_publisher<std_msgs::msg::String>("/test/secret", 10);
    latched_ = create_publisher<std_msgs::msg::String>(
      "/test/latched", rclcpp::QoS(1).transient_local());
    std_msgs::msg::String l;
    l.data = "latched value";
    latched_->publish(l);
    timer_ = create_wall_timer(
      50ms, [this]() {
        // The multithreaded executor runs this on any of its threads; it
        // orders the calls with atomics TSan does not see (uninstrumented).
        std::lock_guard<std::mutex> lock(mutex_);
        std_msgs::msg::String s;
        s.data = "hello " + std::to_string(count_);
        chatter_->publish(s);
        secret_->publish(s);
        sensor_msgs::msg::JointState j;
        j.header.stamp = now();
        j.name = {"j1", "j2"};
        j.position = {0.1 * count_, -0.1 * count_};
        joints_->publish(j);
        ++count_;
      });
  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr chatter_, secret_, latched_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joints_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::mutex mutex_;
  int count_ = 0;
};

class Echo : public rclcpp::Node
{
public:
  Echo()
  : rclcpp::Node("test_echo")
  {
    chatter_out_ = create_publisher<std_msgs::msg::String>("/test/echo_chatter", 10);
    joints_out_ = create_publisher<sensor_msgs::msg::JointState>("/test/echo_joint_states", 10);
    chatter_in_ = create_subscription<std_msgs::msg::String>(
      "/test/in_chatter", 10,
      [this](const std_msgs::msg::String & m) {chatter_out_->publish(m);});
    joints_in_ = create_subscription<sensor_msgs::msg::JointState>(
      "/test/in_joint_states", 10,
      [this](const sensor_msgs::msg::JointState & m) {joints_out_->publish(m);});
  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr chatter_out_;
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr joints_out_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr chatter_in_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joints_in_;
};

class Turtle : public rclcpp::Node
{
public:
  Turtle()
  : rclcpp::Node("turtlesim_standin")
  {
    pose_pub_ = create_publisher<turtlesim::msg::Pose>("/turtle1/pose", 10);
    cmd_ = create_subscription<geometry_msgs::msg::Twist>(
      "/turtle1/cmd_vel", 10, [this](const geometry_msgs::msg::Twist & t) {
        std::lock_guard<std::mutex> lock(mutex_);
        pose_.linear_velocity = static_cast<float>(t.linear.x);
        pose_.angular_velocity = static_cast<float>(t.angular.z);
      });
    pose_.x = 5.5f;
    pose_.y = 5.5f;
    timer_ = create_wall_timer(
      50ms, [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        pose_.x += 0.05f * pose_.linear_velocity;
        pose_.theta += 0.05f * pose_.angular_velocity;
        pose_pub_->publish(pose_);
      });
  }

private:
  std::mutex mutex_;
  turtlesim::msg::Pose pose_;
  rclcpp::Publisher<turtlesim::msg::Pose>::SharedPtr pose_pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_;
  rclcpp::TimerBase::SharedPtr timer_;
};

// /test/control "add" creates the node "dynamic_node" (publishing
// /test/dynamic_topic), "remove" destroys it: entities that appear and
// disappear in the graph (B5). Also stands in an action's feedback topic
// (/fib/_action/feedback) so the graph lists an action.
class Controller : public rclcpp::Node
{
public:
  Controller()
  : rclcpp::Node("test_controller")
  {
    control_ = create_subscription<std_msgs::msg::String>(
      "/test/control", 10, [this](const std_msgs::msg::String & m) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (m.data == "add" && !dynamic_) {
          dynamic_ = std::make_shared<rclcpp::Node>("dynamic_node");
          dynamic_pub_ = dynamic_->create_publisher<std_msgs::msg::String>(
            "/test/dynamic_topic", 10);
        } else if (m.data == "remove") {
          dynamic_pub_.reset();
          dynamic_.reset();
        }
      });
    // Typed, so the process links the type support: a generic publisher
    // loads it alone, and unloading it at exit leaks rosidl's dispatch
    // cache (LeakSanitizer; as in ../rmw_intraprocess M11).
    feedback_ = create_publisher<Feedback>("/fib/_action/feedback", 10);
  }

private:
  using Feedback = action_tutorials_interfaces::action::Fibonacci::Impl::FeedbackMessage;
  std::mutex mutex_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr control_;
  rclcpp::Node::SharedPtr dynamic_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr dynamic_pub_;
  rclcpp::Publisher<Feedback>::SharedPtr feedback_;
};

// B6: /test/add_two_ints and /test/slow_add (0.1 s per call, reentrant)
// AddTwoInts servers; /test/call_served "go" calls /test/served (which a
// Robot Raconteur client serves) with 2 + 3 and publishes the sum, or the
// failure, on /test/served_result.
class Math : public rclcpp::Node
{
public:
  using AddTwoInts = example_interfaces::srv::AddTwoInts;

  Math()
  : rclcpp::Node("test_math")
  {
    add_ = create_service<AddTwoInts>(
      "/test/add_two_ints",
      [](const std::shared_ptr<AddTwoInts::Request> req,
      std::shared_ptr<AddTwoInts::Response> res) {
        res->sum = req->a + req->b;
      });
    slow_group_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    slow_ = create_service<AddTwoInts>(
      "/test/slow_add",
      [](const std::shared_ptr<AddTwoInts::Request> req,
      std::shared_ptr<AddTwoInts::Response> res) {
        std::this_thread::sleep_for(100ms);
        res->sum = req->a + req->b;
      }, rclcpp::ServicesQoS(), slow_group_);
    caller_group_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    served_ = create_client<AddTwoInts>("/test/served", rclcpp::ServicesQoS(), caller_group_);
    result_ = create_publisher<std_msgs::msg::String>("/test/served_result", 10);
    rclcpp::SubscriptionOptions options;
    options.callback_group = caller_group_;
    go_ = create_subscription<std_msgs::msg::String>(
      "/test/call_served", 10, [this](const std_msgs::msg::String &) {
        auto req = std::make_shared<AddTwoInts::Request>();
        req->a = 2;
        req->b = 3;
        served_->async_send_request(
          req, [this](rclcpp::Client<AddTwoInts>::SharedFuture f) {
            std_msgs::msg::String m;
            m.data = "sum " + std::to_string(f.get()->sum);
            result_->publish(m);
          });
      }, options);
  }

private:
  rclcpp::CallbackGroup::SharedPtr slow_group_, caller_group_;
  rclcpp::Service<AddTwoInts>::SharedPtr add_, slow_;
  rclcpp::Client<AddTwoInts>::SharedPtr served_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr go_;
};

// B7: a node with parameters to read and set.
class Params : public rclcpp::Node
{
public:
  Params()
  : rclcpp::Node("test_params")
  {
    declare_parameter("speed", 1.5);
    declare_parameter("name", std::string("robot"));
    declare_parameter("enabled", true);
    declare_parameter("gains", std::vector<double>{1.0, 2.0, 3.0});
    rcl_interfaces::msg::ParameterDescriptor ro;
    ro.read_only = true;
    ro.description = "cannot change";
    declare_parameter("fixed", 7, ro);
  }
};

// B8: /fibonacci action server: feedback every 50 ms, rejects orders < 0 or
// > 46, cancelable.
class FibonacciServer : public rclcpp::Node
{
public:
  using Fibonacci = action_tutorials_interfaces::action::Fibonacci;
  using Handle = rclcpp_action::ServerGoalHandle<Fibonacci>;

  FibonacciServer()
  : rclcpp::Node("test_fibonacci")
  {
    events_ = create_publisher<std_msgs::msg::String>("/test/fibonacci_events", 10);
    server_ = rclcpp_action::create_server<Fibonacci>(
      this, "/fibonacci",
      [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Fibonacci::Goal> goal) {
        return goal->order < 0 || goal->order > 46 ? rclcpp_action::GoalResponse::REJECT :
               rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](const std::shared_ptr<Handle>) {return rclcpp_action::CancelResponse::ACCEPT;},
      [this](const std::shared_ptr<Handle> h) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.emplace_back([this, h]() {execute(h);});
      });
  }

  ~FibonacciServer() override
  {
    stop_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto & w : workers_) {
      w.join();
    }
  }

private:
  void execute(const std::shared_ptr<Handle> h)
  {
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    auto result = std::make_shared<Fibonacci::Result>();
    auto & seq = feedback->partial_sequence;
    seq = {0, 1};
    for (int i = 1; i < h->get_goal()->order && !stop_; ++i) {
      if (h->is_canceling()) {
        result->sequence = seq;
        h->canceled(result);
        report("canceled", h);
        return;
      }
      seq.push_back(seq[static_cast<size_t>(i)] + seq[static_cast<size_t>(i) - 1]);
      h->publish_feedback(feedback);
      std::this_thread::sleep_for(50ms);
    }
    result->sequence = seq;
    if (!stop_) {
      h->succeed(result);
      report("succeeded", h);
    }
  }

  void report(const char * outcome, const std::shared_ptr<Handle> & h)
  {
    std_msgs::msg::String m;
    m.data = std::string(outcome) + " " + std::to_string(h->get_goal()->order);
    events_->publish(m);
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr events_;
  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
  std::mutex mutex_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
};

}  // namespace

#if defined(__linux__)
// On std::terminate, every thread's stack (gdb attached to this process), so
// a rare abort in a test says where it came from. Test only.
[[noreturn]] void terminate_with_backtrace()
{
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
  const std::string cmd = "gdb -q -batch -p " + std::to_string(getpid()) +
    " -ex 'thread apply all bt 30' 1>&2";
  if (std::system(cmd.c_str()) != 0) {  // portability-ok(SHELL): test-only diagnostics
    std::fprintf(stderr, "terminate: no backtrace (gdb failed)\n");
  }
  std::abort();
}
#endif

int main(int argc, char ** argv)
{
#if defined(__linux__)
  std::set_terminate(terminate_with_backtrace);
#endif
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
    auto talker = std::make_shared<Talker>();
    auto echo = std::make_shared<Echo>();
    auto turtle = std::make_shared<Turtle>();
    auto controller = std::make_shared<Controller>();
    auto math = std::make_shared<Math>();
    auto params = std::make_shared<Params>();
    auto fibonacci = std::make_shared<FibonacciServer>();
    auto exec = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();
    // B9: this process is also a component container, /test_container.
    auto container = std::make_shared<rclcpp_components::ComponentManager>(
      exec, "test_container");
    exec->add_node(bridge);
    exec->add_node(talker);
    exec->add_node(echo);
    exec->add_node(turtle);
    exec->add_node(controller);
    exec->add_node(math);
    exec->add_node(params);
    exec->add_node(fibonacci);
    exec->add_node(container);
    RCLCPP_INFO(rclcpp::get_logger("topic_test_host"), "READY");
    exec->spin();
    exec->remove_node(container);
    container.reset();  // unloads what it loaded
    exec->remove_node(fibonacci);
    exec->remove_node(params);
    exec->remove_node(math);
    exec->remove_node(controller);
    exec->remove_node(turtle);
    exec->remove_node(echo);
    exec->remove_node(talker);
    exec->remove_node(bridge);
    fibonacci.reset();
    params.reset();
    math.reset();
    controller.reset();
    turtle.reset();
    echo.reset();
    talker.reset();
    bridge.reset();
    RCLCPP_INFO(rclcpp::get_logger("topic_test_host"), "Bridge destroyed");
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("topic_test_host"), "%s", e.what());
    code = 1;
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return code;
}
