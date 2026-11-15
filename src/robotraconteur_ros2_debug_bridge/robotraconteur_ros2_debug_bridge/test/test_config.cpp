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

// The bridge configuration: the shipped default is safe (no TCP, announce or
// discovery; only "read"), strict parsing, and the node setup flags.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "RobotRaconteur.h"
#include "config.hpp"
#include "rr_test_safety.hpp"

namespace RR = RobotRaconteur;
using robotraconteur_ros2_debug_bridge::BridgeConfig;
using robotraconteur_ros2_debug_bridge::ConfigError;
using robotraconteur_ros2_debug_bridge::load_config_file;
using robotraconteur_ros2_debug_bridge::node_setup_flags;
using robotraconteur_ros2_debug_bridge::parse_config;

namespace
{

std::string error_of(const std::string & text)
{
  try {
    parse_config(text, "test.yaml");
  } catch (const ConfigError & e) {
    return e.what();
  }
  ADD_FAILURE() << "parsed without error:\n" << text;
  return "";
}

void expect_defaults(const BridgeConfig & c)
{
  EXPECT_EQ("ros2_debug_bridge", c.node_name);
  EXPECT_EQ("ros2_debug_bridge", c.service_name);
  EXPECT_TRUE(c.intra_transport);
  EXPECT_TRUE(c.local_transport);
  EXPECT_FALSE(c.tcp.enabled);
  EXPECT_EQ(34573, c.tcp.port);
  EXPECT_TRUE(c.tcp.localhost_only);
  EXPECT_FALSE(c.node_announce);
  EXPECT_FALSE(c.node_discovery_listening);
  EXPECT_EQ(std::set<std::string>{"read"}, c.capabilities);
}

}  // namespace

TEST(BridgeConfig, ShippedDefaultIsSafe)
{
  const BridgeConfig c = load_config_file(DEFAULT_CONFIG);
  expect_defaults(c);
  const uint32_t flags = node_setup_flags(c);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_DISCOVERY_LISTENING);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_HARDWARE_TRANSPORT);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_SERVER_PUBLIC);
  // The test safety helper (which refuses anything that can reach the
  // network) accepts exactly these flags, and a node set up with them has no
  // TCP transport.
  EXPECT_NO_THROW(rr_test_safety::check_flags(flags));
  auto node = RR_MAKE_SHARED<RR::RobotRaconteurNode>();
  node->Init();
  {
    auto setup = rr_test_safety::make_node_setup(node, {}, "bridge_config_test", flags);
    EXPECT_FALSE(setup->GetTcpTransport());
    EXPECT_TRUE(setup->GetLocalTransport());
    EXPECT_TRUE(setup->GetIntraTransport());
  }
  node->Shutdown();
}

TEST(BridgeConfig, EmptyFileMeansDefaults)
{
  expect_defaults(parse_config("", "empty.yaml"));
  expect_defaults(BridgeConfig{});
}

TEST(BridgeConfig, OptInTcpAndCapabilities)
{
  const BridgeConfig c = parse_config(
    "robotraconteur:\n"
    "  node_name: n\n"
    "  service_name: s\n"
    "  transports: {intra: false, local: true, tcp: {enabled: true, port: 40000, "
    "localhost_only: false}}\n"
    "  node_announce: true\n"
    "capabilities: {read: false, publish: true, load_component: true}\n", "t.yaml");
  EXPECT_EQ("n", c.node_name);
  EXPECT_EQ("s", c.service_name);
  EXPECT_FALSE(c.intra_transport);
  EXPECT_TRUE(c.tcp.enabled);
  EXPECT_EQ(40000, c.tcp.port);
  EXPECT_FALSE(c.tcp.localhost_only);
  EXPECT_TRUE(c.node_announce);
  EXPECT_EQ((std::set<std::string>{"publish", "load_component"}), c.capabilities);
  const uint32_t flags = node_setup_flags(c);
  EXPECT_NE(0u, flags & RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST);
  EXPECT_NE(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT);
  // Never, whatever the configuration says.
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_ENABLE_HARDWARE_TRANSPORT);
  EXPECT_EQ(0u, flags & RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_SERVER_PUBLIC);
  // And the test helper refuses it.
  EXPECT_THROW(rr_test_safety::check_flags(flags), rr_test_safety::UnsafeSetupError);
}

TEST(BridgeConfig, Errors)
{
  EXPECT_NE(std::string::npos, error_of("robotracontuer: {}\n").find("unknown key"));
  EXPECT_NE(
    std::string::npos,
    error_of("robotraconteur:\n  transports:\n    tpc: {}\n").find("transports.tpc"));
  EXPECT_NE(std::string::npos, error_of("capabilities: {fly: true}\n").find("capabilities.fly"));
  EXPECT_NE(std::string::npos, error_of("capabilities: {read: maybe}\n").find("true or false"));
  EXPECT_NE(
    std::string::npos,
    error_of("robotraconteur: {transports: {tcp: {port: 70000}}}\n").find("port"));
  EXPECT_NE(
    std::string::npos,
    error_of("robotraconteur: {transports: {intra: false, local: false}}\n").find("disabled"));
  EXPECT_NE(
    std::string::npos,
    error_of("robotraconteur: {node_discovery_listening: true}\n").find("need the TCP"));
  EXPECT_NE(std::string::npos, error_of("robotraconteur: {node_name: ''}\n").find("empty"));
  EXPECT_NE(std::string::npos, error_of("- a\n").find("mapping"));
  EXPECT_NE(std::string::npos, error_of("capabilities: {read\n").find("test.yaml:"));
  EXPECT_THROW(load_config_file("/no/such/file.yaml"), ConfigError);
}

TEST(BridgeConfig, AuthenticationAndTls)
{
  const BridgeConfig d = load_config_file(DEFAULT_CONFIG);
  EXPECT_FALSE(d.authentication.enabled);
  EXPECT_FALSE(d.tcp.tls);
  EXPECT_FALSE(d.tcp.require_tls);

  const BridgeConfig c = parse_config(
    "robotraconteur:\n"
    "  transports: {tcp: {enabled: true, tls: {enabled: true, require: true}}}\n"
    "  authentication: {enabled: true, password_file: /etc/users.txt}\n",
    "test");
  EXPECT_TRUE(c.tcp.tls);
  EXPECT_TRUE(c.tcp.require_tls);
  EXPECT_TRUE(c.authentication.enabled);
  EXPECT_EQ(c.authentication.password_file, "/etc/users.txt");

  auto error = [](const std::string & yaml) {
      try {
        parse_config(yaml, "f.yaml");
      } catch (const ConfigError & e) {
        return std::string(e.what());
      }
      return std::string("(accepted)");
    };
  EXPECT_NE(error("robotraconteur: {authentication: {enabled: true}}\n").find(
      "password_file: needed"), std::string::npos);
  EXPECT_NE(error("robotraconteur: {transports: {tcp: {tls: {enabled: true}}}}\n").find(
      "TLS needs the TCP transport"), std::string::npos);
  EXPECT_NE(error(
      "robotraconteur: {transports: {tcp: {enabled: true, tls: {require: true}}}}\n").find(
      "needs tls.enabled"), std::string::npos);
  EXPECT_NE(error("robotraconteur: {authentication: {users: x}}\n").find("unknown key"),
    std::string::npos);
}

TEST(BridgeConfig, FastPath)
{
  // Off by default (docs/benchmark.md: it slows the application's publish()).
  EXPECT_FALSE(load_config_file(DEFAULT_CONFIG).fast_path);
  EXPECT_FALSE(BridgeConfig{}.fast_path);
  EXPECT_TRUE(parse_config("fast_path: true\n", "test").fast_path);
  EXPECT_THROW(parse_config("fast_path: maybe\n", "test"), ConfigError);
}
