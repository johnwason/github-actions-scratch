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

#include "bridge_component.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rmw/rmw.h"

#include "root_object.hpp"
#include "graph_object.hpp"
#include "topic_proxy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

namespace
{

constexpr const char * kVersion = "0.0.0";
constexpr const char * kRootType = "experimental.ros2_debug_bridge";
constexpr std::chrono::milliseconds kGraphPollPeriod{250};
constexpr int kGraphResendPolls = 4;  // every second

std::string default_config_file()
{
  try {
    return ament_index_cpp::get_package_share_directory("robotraconteur_ros2_debug_bridge") +
           "/config/default.yaml";
  } catch (const std::exception &) {
    return "";  // not installed (some unit tests): the built-in defaults apply
  }
}

}  // namespace

DebugBridge::DebugBridge(const rclcpp::NodeOptions & options)
: rclcpp::Node("robotraconteur_ros2_debug_bridge", options)
{
  const std::string config_file = declare_parameter("config_file", default_config_file());
  if (!config_file.empty()) {
    config_ = load_config_file(config_file);  // throws ConfigError
  }
  start_robotraconteur();
  RCLCPP_INFO(
    get_logger(),
    "Robot Raconteur %s node '%s' serving '%s' (intra %s, local %s, tcp %s); config %s",
    rr_node_->GetRobotRaconteurVersion().c_str(), config_.node_name.c_str(),
    config_.service_name.c_str(), config_.intra_transport ? "on" : "off",
    config_.local_transport ? "on" : "off",
    config_.tcp.enabled ?
    (std::to_string(config_.tcp.port) + (config_.tcp.localhost_only ? " localhost" : " all"))
    .c_str() : "off",
    config_file.empty() ? "(built-in defaults)" : config_file.c_str());
}

DebugBridge::~DebugBridge()
{
  stop_robotraconteur();
}

void DebugBridge::start_robotraconteur()
{
  BridgeIdentity identity;
  identity.ros_node_name = get_name();
  identity.ros_namespace = get_namespace();
  const char * rmw = rmw_get_implementation_identifier();
  identity.rmw_implementation = rmw != nullptr ? rmw : "";
  identity.bridge_version = kVersion;
  for (const auto & name : capability_names()) {
    if (config_.capabilities.count(name) != 0) {
      identity.capabilities.push_back(name);
    }
  }

  rr_node_ = RR_MAKE_SHARED<RR::RobotRaconteurNode>();
  rr_node_->Init();
  try {
    // No command-line or environment overrides of the transports: the
    // configuration file is the only source (CLAUDE.md S2, S5).
    rr_setup_ = std::make_unique<RR::RobotRaconteurNodeSetup>(
      rr_node_, ROBOTRACONTEUR_SERVICE_TYPES, config_.node_name, config_.tcp.port,
      node_setup_flags(config_));
    auto policy =
      std::make_shared<const ExposurePolicy>(config_.exposure, config_.capabilities);
    proxies_ = std::make_shared<ProxyManager>(*this, rr_node_, policy, config_.fast_path);
    graph_ = std::make_shared<GraphMonitor>(*this, policy);
    root_ = RR_MAKE_SHARED<RootObject>(identity, proxies_, graph_);
    if (config_.tcp.enabled && config_.tcp.tls) {
      // The node's TLS certificate (Robot Raconteur's node certificate store).
      auto tcp = rr_setup_->GetTcpTransport();
      tcp->LoadTlsNodeCertificate();
      tcp->SetRequireTls(config_.tcp.require_tls);
    }
    RR_SHARED_PTR<RR::ServiceSecurityPolicy> security;
    if (config_.authentication.enabled) {
      std::ifstream in(config_.authentication.password_file, std::ios::binary);
      if (!in) {
        throw ConfigError(
                config_.authentication.password_file + ": cannot open the password file");
      }
      std::ostringstream users;
      users << in.rdbuf();
      auto authenticator = RR_MAKE_SHARED<RR::PasswordFileUserAuthenticator>(users.str());
      std::map<std::string, std::string> policies;
      policies["requirevaliduser"] = "true";
      policies["allowobjectlock"] = "true";
      security = RR_MAKE_SHARED<RR::ServiceSecurityPolicy>(authenticator, policies);
    }
    rr_node_->RegisterService(config_.service_name, kRootType, root_, security);
    // Graph changes for graph_changes: polled, weakly (stop cancels it).
    // The current value is also sent again every second: Robot Raconteur's
    // WireBroadcaster gives a late wire connection nothing until the next
    // change (docs/upstream_issues.md).
    std::weak_ptr<GraphMonitor> weak_graph = graph_;
    RR_WEAK_PTR<RootObject> weak_root = root_;
    auto polls = std::make_shared<int>(0);
    graph_timer_ = create_wall_timer(
      kGraphPollPeriod, [weak_graph, weak_root, polls]() {
        auto g = weak_graph.lock();
        auto r = weak_root.lock();
        if (!g || !r) {
          return;
        }
        // An exception must not leave a callback of the host's executor:
        // graph queries throw once the ROS context is shut down (SIGINT).
        try {
          const bool changed = g->poll();
          if (changed || ++*polls % kGraphResendPolls == 0) {
            r->graph_changed();
          }
        } catch (const std::exception &) {
        }
      });
  } catch (...) {
    stop_robotraconteur();
    throw;
  }
}

void DebugBridge::stop_robotraconteur() noexcept
{
  // Robot Raconteur first: after Shutdown() no client call is running or
  // can start, and its threads are joined. Then the objects it served.
  if (graph_timer_) {
    graph_timer_->cancel();
  }
  try {
    if (rr_node_) {
      RCLCPP_INFO(get_logger(), "Stopping Robot Raconteur");
      rr_node_->Shutdown();
      RCLCPP_INFO(get_logger(), "Robot Raconteur stopped");
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "Robot Raconteur shutdown: %s", e.what());
  }
  // Then the proxies (their ROS subscriptions and publishers), before the
  // ROS node goes.
  if (proxies_) {
    proxies_->shutdown();
  }
  rr_setup_.reset();
  graph_timer_.reset();
  root_.reset();
  graph_.reset();
  proxies_.reset();
  rr_node_.reset();
}

}  // namespace robotraconteur_ros2_debug_bridge
