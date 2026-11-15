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

// B1 acceptance: the component, created through its rclcpp_components
// factory (class_loader, as a component container does), loaded and unloaded
// 100 times in one process; every time a Robot Raconteur client in this process
// connects and reads the root properties. Registered once per RMW
// (RMW_IMPLEMENTATION).
//
// The client uses LocalTransport: the component links its own private copy of
// Robot Raconteur (its symbols are not exported; test_symbols.py), so
// IntraTransport only reaches clients inside the component library, as on
// Windows.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "class_loader/class_loader.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/node_factory.hpp"
#include "RobotRaconteur.h"
#include "robotraconteur_generated.h"  // NOLINT(build/include_subdir): generated
#include "rr_test_safety.hpp"

namespace RR = RobotRaconteur;
namespace rrdef = experimental::ros2_debug_bridge;

namespace
{

constexpr const char * kFactoryClass =
  "rclcpp_components::NodeFactoryTemplate<robotraconteur_ros2_debug_bridge::DebugBridge>";

std::string unique(const std::string & base)
{
  static int counter = 0;
  return base + "_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000) +
         "_" + std::to_string(counter++);
}

/// Writes a bridge configuration (unique RR node name) to a temporary file.
std::string write_config(const std::string & node_name, const std::string & extra = "")
{
  const auto path = std::filesystem::temp_directory_path() / (node_name + ".yaml");
  std::ofstream out(path);
  out << "robotraconteur:\n  node_name: " << node_name << "\n" << extra;
  return path.string();
}

/// The component library stays loaded until the process exits, as in a
/// component container (container_host keeps component libraries mapped):
/// unloading it before exit would also leave LeakSanitizer unable to
/// symbolize anything allocated from it.
class_loader::ClassLoader * component_loader()
{
  static auto * loader = new class_loader::ClassLoader(COMPONENT_LIBRARY);
  return loader;
}

class ComponentLifecycle : public ::testing::Test
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    loader_ = component_loader();
    factory_ = loader_->createInstance<rclcpp_components::NodeFactory>(kFactoryClass);
    client_ = RR_MAKE_SHARED<RR::RobotRaconteurNode>();
    client_->Init();
    client_setup_ = rr_test_safety::make_node_setup(
      client_, ROBOTRACONTEUR_SERVICE_TYPES, "", rr_test_safety::client_flags());
  }

  void TearDown() override
  {
    client_->Shutdown();
    client_setup_.reset();
    client_.reset();
    factory_.reset();
    rclcpp::shutdown();
  }

  rclcpp_components::NodeInstanceWrapper load(
    const std::string & config_file, const std::string & ros_namespace = "")
  {
    rclcpp::NodeOptions options;
    if (!ros_namespace.empty()) {
      options.arguments({"--ros-args", "-r", "__ns:=" + ros_namespace});
    }
    options.parameter_overrides({rclcpp::Parameter("config_file", config_file)});
    return factory_->create_node_instance(options);
  }

  rrdef::ROS2DebugBridgePtr connect_bridge(
    const std::string & node_name,
    const std::string & transport)
  {
    const std::string url = transport + ":///?nodename=" + node_name +
      "&service=ros2_debug_bridge";
    return RR::rr_cast<rrdef::ROS2DebugBridge>(client_->ConnectService(url));
  }

  class_loader::ClassLoader * loader_ = nullptr;
  std::shared_ptr<rclcpp_components::NodeFactory> factory_;
  RR_SHARED_PTR<RR::RobotRaconteurNode> client_;
  std::unique_ptr<RR::RobotRaconteurNodeSetup> client_setup_;
};

}  // namespace

TEST_F(ComponentLifecycle, LoadUnloadHundredTimesWithClient)
{
  const std::string name = unique("bridge_lifecycle");
  const std::string config = write_config(name);
  const char * rmw = std::getenv("RMW_IMPLEMENTATION");
  ASSERT_NE(nullptr, rmw);
  for (int i = 0; i < 100; ++i) {
    SCOPED_TRACE("iteration " + std::to_string(i));
    auto wrapper = load(config);
    ASSERT_EQ(
      "robotraconteur_ros2_debug_bridge",
      std::string(wrapper.get_node_base_interface()->get_name()));
    auto bridge = connect_bridge(name, "rr+local");
    EXPECT_EQ("robotraconteur_ros2_debug_bridge", bridge->get_ros_node_name());
    EXPECT_EQ("/", bridge->get_ros_namespace());
    EXPECT_EQ(std::string(rmw), bridge->get_rmw_implementation());
    EXPECT_EQ("0.0.0", bridge->get_bridge_version());
    auto caps = bridge->get_capabilities();
    ASSERT_TRUE(caps);
    ASSERT_EQ(1u, caps->size());
    EXPECT_EQ("read", RR::RRArrayToString(caps->front()));
    client_->DisconnectService(bridge);
    // Unload while the client is still connected on alternate iterations:
    // the bridge must stop Robot Raconteur cleanly either way.
    if (i % 2 == 1) {
      auto again = connect_bridge(name, "rr+local");
      EXPECT_EQ("/", again->get_ros_namespace());
      wrapper = {};  // unload with the connection open
      EXPECT_ANY_THROW(again->get_ros_namespace());
    }
  }
  std::filesystem::remove(config);
}

TEST_F(ComponentLifecycle, TwoBridgesAndBadConfiguration)
{
  const std::string a = unique("bridge_a");
  const std::string b = unique("bridge_b");
  const std::string config_a = write_config(a);
  const std::string config_b = write_config(b, "capabilities: {publish: true}\n");
  const std::string bad = write_config(unique("bridge_bad"), "frobnicate: 1\n");
  {
    auto wa = load(config_a);
    rclcpp::NodeOptions ns_options;
    ns_options.arguments({"--ros-args", "-r", "__ns:=/second"});
    ns_options.parameter_overrides({rclcpp::Parameter("config_file", config_b)});
    auto wb = factory_->create_node_instance(ns_options);
    // (Distinct ROS namespaces: these test the configuration and the Robot
    // Raconteur node name, and rcl keeps one rosout publisher per ROS node
    // name, so a duplicate ROS name would orphan the failed node's.)
    EXPECT_ANY_THROW(load(bad, "/bad"));  // an invalid file refuses to load
    EXPECT_ANY_THROW(load(config_a, "/dup"));  // the same RR node name twice in one process
    auto ba = connect_bridge(a, "rr+local");
    auto bb = connect_bridge(b, "rr+local");
    EXPECT_EQ("/", ba->get_ros_namespace());
    EXPECT_EQ("/second", bb->get_ros_namespace());
    EXPECT_EQ(1u, ba->get_capabilities()->size());
    EXPECT_EQ(2u, bb->get_capabilities()->size());  // read, publish
    client_->DisconnectService(ba);
    client_->DisconnectService(bb);
  }
  // Both stopped: neither is reachable, and the names are free again.
  EXPECT_ANY_THROW(connect_bridge(a, "rr+local"));
  auto wa = load(config_a);
  auto ba = connect_bridge(a, "rr+local");
  EXPECT_EQ("/", ba->get_ros_namespace());
  client_->DisconnectService(ba);
  for (const auto & f : {config_a, config_b, bad}) {
    std::filesystem::remove(f);
  }
}
