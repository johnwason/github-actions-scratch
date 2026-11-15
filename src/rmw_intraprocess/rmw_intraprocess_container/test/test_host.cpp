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

// ComponentHost with every executor model: components from
// test/configs/talker_listener.yaml (remappings, parameter files, inline
// parameters), observed from a probe node in the same process; the
// container's load_node/list_nodes services; unload; load errors.
//
// The probe spins on the test thread, so every probe callback runs there and
// results never cross threads through futures (see rmw_intraprocess_cpp's
// test/rclcpp_test_utils.hpp for why that matters under TSan).

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "composition_interfaces/srv/list_nodes.hpp"
#include "composition_interfaces/srv/load_node.hpp"
#include "composition_interfaces/srv/unload_node.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include "host.hpp"

using rmw_intraprocess_container::ComponentConfig;
using rmw_intraprocess_container::ComponentHost;
using rmw_intraprocess_container::ExecutorConfig;
using rmw_intraprocess_container::ExecutorType;
using rmw_intraprocess_container::HostConfig;
using rmw_intraprocess_container::load_config_file;
using rmw_intraprocess_container::LoadError;

namespace
{

using namespace std::chrono_literals;

struct ExecutorCase
{
  const char * name;
  ExecutorConfig config;
};

std::string case_name(const testing::TestParamInfo<ExecutorCase> & info)
{
  return info.param.name;
}

void PrintTo(const ExecutorCase & c, std::ostream * os)
{
  *os << c.name;
}

class HostTest : public testing::TestWithParam<ExecutorCase>
{
protected:
  void SetUp() override
  {
    rclcpp::init(0, nullptr);
    config_ = load_config_file(std::string(TEST_CONFIG_DIR) + "/talker_listener.yaml");
    config_.executor = GetParam().config;
    config_.container_name = "test_container";
    probe_ = std::make_shared<rclcpp::Node>("probe");
    probe_exec_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    probe_exec_->add_node(probe_);
  }

  void TearDown() override
  {
    stop_host();
    probe_exec_->remove_node(probe_);
    probe_exec_.reset();
    probe_.reset();
    rclcpp::shutdown();
  }

  void start_host()
  {
    host_ = std::make_unique<ComponentHost>(config_);
  }

  void spin_host()
  {
    spin_thread_ = std::thread([this]() {host_->spin();});
  }

  void stop_host()
  {
    if (host_) {
      host_->stop();
    }
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
    host_.reset();
  }

  /// Spins the probe on this thread until `done` or the timeout.
  bool wait_until(const std::function<bool()> & done, std::chrono::milliseconds timeout = 20s)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
      if (std::chrono::steady_clock::now() > deadline) {
        return false;
      }
      probe_exec_->spin_once(10ms);
    }
    return true;
  }

  template<class ServiceT>
  std::optional<typename ServiceT::Response> call(
    const std::string & service, typename ServiceT::Request::SharedPtr request)
  {
    auto client = probe_->create_client<ServiceT>(service);
    if (!wait_until([&]() {return client->service_is_ready();})) {
      return std::nullopt;
    }
    std::optional<typename ServiceT::Response> response;
    client->async_send_request(
      request, [&response](typename rclcpp::Client<ServiceT>::SharedFuture f) {
        response = *f.get();
      });
    wait_until([&]() {return response.has_value();});
    return response;
  }

  HostConfig config_;
  std::unique_ptr<ComponentHost> host_;
  std::thread spin_thread_;
  rclcpp::Node::SharedPtr probe_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> probe_exec_;
};

}  // namespace

TEST_P(HostTest, LoadsConfiguredComponents)
{
  start_host();
  ASSERT_TRUE(host_->load_all());
  const auto loaded = host_->loaded();
  ASSERT_EQ(3u, loaded.size());
  EXPECT_EQ("/smoke/talker", loaded[0].full_node_name);
  EXPECT_EQ("/smoke/listener", loaded[1].full_node_name);
  EXPECT_EQ("/blackboard", loaded[2].full_node_name);
  spin_host();

  // Remapped topic, relative to the component's namespace. composition's
  // Talker publishes once a second.
  int received = 0;
  auto sub = probe_->create_subscription<std_msgs::msg::String>(
    "/smoke/smoke_chat", 10, [&received](const std_msgs::msg::String &) {++received;});
  EXPECT_TRUE(wait_until([&]() {return received > 0;}));
  EXPECT_EQ(1u, sub->get_publisher_count());  // the talker

  // Parameters: global file < component file < inline.
  auto params = std::make_shared<rclcpp::AsyncParametersClient>(probe_, "/blackboard");
  ASSERT_TRUE(wait_until([&]() {return params->service_is_ready();}));
  std::optional<std::vector<rclcpp::Parameter>> values;
  params->get_parameters(
    {"from_global_file", "from_component_file", "overridden", "inline_wins"},
    [&values](std::shared_future<std::vector<rclcpp::Parameter>> f) {values = f.get();});
  ASSERT_TRUE(wait_until([&]() {return values.has_value();}));
  ASSERT_EQ(4u, values->size());
  EXPECT_EQ("global", (*values)[0].as_string());
  EXPECT_DOUBLE_EQ(2.5, (*values)[1].as_double());
  EXPECT_EQ(2, (*values)[2].as_int());
  EXPECT_EQ(11, (*values)[3].as_int());

  // The container's standard services work inside the process.
  auto load = std::make_shared<composition_interfaces::srv::LoadNode::Request>();
  load->package_name = "composition";
  load->plugin_name = "composition::Listener";
  load->node_name = "late_listener";
  auto loaded_late = call<composition_interfaces::srv::LoadNode>(
    "/test_container/_container/load_node", load);
  ASSERT_TRUE(loaded_late.has_value());
  EXPECT_TRUE(loaded_late->success) << loaded_late->error_message;
  EXPECT_EQ("/late_listener", loaded_late->full_node_name);

  auto list = call<composition_interfaces::srv::ListNodes>(
    "/test_container/_container/list_nodes",
    std::make_shared<composition_interfaces::srv::ListNodes::Request>());
  ASSERT_TRUE(list.has_value());
  EXPECT_EQ(
    (std::vector<std::string>{"/smoke/talker", "/smoke/listener", "/blackboard",
      "/late_listener"}), list->full_node_names);

  // Unload while spinning, the supported way (unload_node runs on the
  // executor): the talker's publisher disappears.
  auto unload = std::make_shared<composition_interfaces::srv::UnloadNode::Request>();
  unload->unique_id = loaded[0].unique_id;
  auto unloaded = call<composition_interfaces::srv::UnloadNode>(
    "/test_container/_container/unload_node", unload);
  ASSERT_TRUE(unloaded.has_value());
  EXPECT_TRUE(unloaded->success) << unloaded->error_message;
  EXPECT_TRUE(wait_until([&]() {return sub->get_publisher_count() == 0;}));

  // Direct unload from the owning thread, with the host stopped.
  host_->stop();
  spin_thread_.join();
  EXPECT_THROW(host_->unload(loaded[0].unique_id), LoadError);  // already gone
  host_->unload(loaded[1].unique_id);
  ASSERT_EQ(1u, host_->loaded().size());
  EXPECT_EQ("/blackboard", host_->loaded()[0].full_node_name);

  stop_host();  // unloads the rest, including the service-loaded listener
}

TEST_P(HostTest, LoadErrors)
{
  start_host();
  ComponentConfig c;
  c.package = "composition";
  c.plugin = "composition::DoesNotExist";
  EXPECT_THROW(host_->load(c), LoadError);
  c.package = "no_such_package_anywhere";
  EXPECT_THROW(host_->load(c), LoadError);
  c.package = "composition";
  c.plugin = "composition::Talker";
  c.name = "bad name with spaces";
  EXPECT_THROW(host_->load(c), LoadError);
  c.name = "talker";
  c.parameter_files = {std::string(TEST_CONFIG_DIR) + "/no_such_params.yaml"};
  EXPECT_THROW(host_->load(c), LoadError);
  EXPECT_TRUE(host_->loaded().empty());
  c.parameter_files.clear();
  EXPECT_EQ("/talker", host_->load(c).full_node_name);
}

const ExecutorCase kCases[] = {
  {"SingleThreaded", {ExecutorType::SingleThreaded, 0, false}},
  {"MultiThreaded", {ExecutorType::MultiThreaded, 3, false}},
  {"Events", {ExecutorType::Events, 0, false}},
  {"IsolatedSingleThreaded", {ExecutorType::SingleThreaded, 0, true}},
  {"IsolatedMultiThreaded", {ExecutorType::MultiThreaded, 0, true}},
};

INSTANTIATE_TEST_SUITE_P(Executors, HostTest, testing::ValuesIn(kCases), case_name);

class LoadAllTest : public testing::Test
{
protected:
  void SetUp() override {rclcpp::init(0, nullptr);}
  void TearDown() override {rclcpp::shutdown();}

  static HostConfig config(bool continue_on_error)
  {
    HostConfig c;
    c.continue_on_error = continue_on_error;
    ComponentConfig bad;
    bad.package = "composition";
    bad.plugin = "composition::DoesNotExist";
    ComponentConfig disabled;
    disabled.package = "composition";
    disabled.plugin = "composition::Listener";
    disabled.enabled = false;
    ComponentConfig good;
    good.package = "composition";
    good.plugin = "composition::Talker";
    c.components = {bad, disabled, good};
    return c;
  }
};

TEST_F(LoadAllTest, StopsAtFirstFailure)
{
  ComponentHost host(config(false));
  EXPECT_FALSE(host.load_all());
  EXPECT_TRUE(host.loaded().empty());
}

TEST_F(LoadAllTest, ContinueOnErrorSkipsFailuresAndDisabled)
{
  ComponentHost host(config(true));
  EXPECT_FALSE(host.load_all());
  ASSERT_EQ(1u, host.loaded().size());
  EXPECT_EQ("/talker", host.loaded()[0].full_node_name);
}

TEST_F(LoadAllTest, RequestStopBeforeSpinIsNotLost)
{
  ComponentHost host(config(true));
  host.request_stop();
  host.spin();  // returns at once
  std::thread t([&host]() {host.spin();});
  host.stop();
  t.join();
}
