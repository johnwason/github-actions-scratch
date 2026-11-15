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

// Deny-by-default exposure rules and their configuration.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "config.hpp"
#include "exposure_policy.hpp"

using robotraconteur_ros2_debug_bridge::BridgeConfig;
using robotraconteur_ros2_debug_bridge::ConfigError;
using robotraconteur_ros2_debug_bridge::ExposurePolicy;
using robotraconteur_ros2_debug_bridge::load_config_file;
using robotraconteur_ros2_debug_bridge::NamePattern;
using robotraconteur_ros2_debug_bridge::parse_config;

TEST(NamePattern, Matching)
{
  EXPECT_TRUE(NamePattern("/chatter").matches("/chatter"));
  EXPECT_FALSE(NamePattern("/chatter").matches("/chatter2"));
  EXPECT_FALSE(NamePattern("/chatter").matches("/ns/chatter"));
  EXPECT_TRUE(NamePattern("/turtle1/*").matches("/turtle1/cmd_vel"));
  EXPECT_FALSE(NamePattern("/turtle1/*").matches("/turtle1/a/b"));
  EXPECT_FALSE(NamePattern("/turtle1/*").matches("/turtle2/cmd_vel"));
  EXPECT_TRUE(NamePattern("/turtle1/**").matches("/turtle1/a/b"));
  EXPECT_TRUE(NamePattern("/**").matches("/x"));
  EXPECT_TRUE(NamePattern("**").matches("/a/b/c"));
  EXPECT_TRUE(NamePattern("**/cmd_vel").matches("/robot/base/cmd_vel"));
  EXPECT_FALSE(NamePattern("**/cmd_vel").matches("/robot/base/cmd_vel2"));
  EXPECT_TRUE(NamePattern("/robot?/odom").matches("/robot1/odom"));
  EXPECT_FALSE(NamePattern("/robot?/odom").matches("/robot/odom"));
  EXPECT_TRUE(NamePattern("/*_vel").matches("/cmd_vel"));
  EXPECT_FALSE(NamePattern("/*").matches("/a/b"));
  EXPECT_FALSE(NamePattern("/a*").matches(""));
}

TEST(NamePattern, Invalid)
{
  for (const char * bad : {"", "chatter", "*", "/a b", "/a***", "/a.b", "/a\\b"}) {
    EXPECT_THROW(NamePattern{bad}, std::invalid_argument) << bad;
  }
}

TEST(ExposurePolicy, DenyByDefault)
{
  const BridgeConfig c = load_config_file(DEFAULT_CONFIG);
  const ExposurePolicy p(c.exposure, c.capabilities);
  for (const char * t : {"/chatter", "/rosout", "/parameter_events", "/a/b"}) {
    EXPECT_FALSE(p.topic_visible(t)) << t;
    EXPECT_FALSE(p.topic_subscribable(t)) << t;
    EXPECT_FALSE(p.topic_publishable(t)) << t;
  }
}

TEST(ExposurePolicy, RulesAndCapabilities)
{
  const BridgeConfig c = parse_config(
    "capabilities: {publish: true}\n"
    "exposure:\n"
    "  topics:\n"
    "    allow: ['/turtle1/**', '/chatter', '/secret']\n"
    "    publish: ['/turtle1/cmd_vel', '/out']\n"
    "    deny: ['/secret', '/turtle1/hidden']\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.topic_subscribable("/chatter"));
  EXPECT_TRUE(p.topic_subscribable("/turtle1/pose"));
  EXPECT_FALSE(p.topic_publishable("/turtle1/pose"));
  EXPECT_TRUE(p.topic_publishable("/turtle1/cmd_vel"));
  // Publish-only: visible, not subscribable.
  EXPECT_TRUE(p.topic_visible("/out"));
  EXPECT_FALSE(p.topic_subscribable("/out"));
  EXPECT_TRUE(p.topic_publishable("/out"));
  // Deny wins over allow and publish.
  for (const char * t : {"/secret", "/turtle1/hidden"}) {
    EXPECT_FALSE(p.topic_visible(t)) << t;
    EXPECT_FALSE(p.topic_subscribable(t)) << t;
    EXPECT_FALSE(p.topic_publishable(t)) << t;
  }
  EXPECT_FALSE(p.topic_visible("/other"));

  // The same rules without the capabilities.
  const BridgeConfig no_pub = parse_config(
    "exposure: {topics: {allow: ['/a'], publish: ['/a', '/b']}}\n", "test");
  const ExposurePolicy q(no_pub.exposure, no_pub.capabilities);
  EXPECT_TRUE(q.topic_subscribable("/a"));
  EXPECT_FALSE(q.topic_publishable("/a"));  // publish capability off by default
  EXPECT_FALSE(q.topic_visible("/b"));
  const BridgeConfig no_read = parse_config(
    "capabilities: {read: false}\nexposure: {topics: {allow: ['/a']}}\n", "test");
  const ExposurePolicy r(no_read.exposure, no_read.capabilities);
  EXPECT_FALSE(r.topic_visible("/a"));
  EXPECT_FALSE(r.topic_subscribable("/a"));
}

TEST(ExposurePolicy, ConfigErrors)
{
  auto error = [](const std::string & yaml) {
      try {
        parse_config(yaml, "f.yaml");
      } catch (const ConfigError & e) {
        return std::string(e.what());
      }
      return std::string("(accepted)");
    };
  EXPECT_NE(error("exposure: {topics: {allow: ['chatter']}}\n").find(
      "f.yaml:1: exposure.topics.allow: pattern 'chatter' must start with"), std::string::npos)
    << error("exposure: {topics: {allow: ['chatter']}}\n");
  EXPECT_NE(error("exposure: {topics: {allow: '/chatter'}}\n").find("expected a list"),
    std::string::npos);
  EXPECT_NE(error("exposure: {topics: {subscribe: []}}\n").find("unknown key"), std::string::npos);
  EXPECT_NE(error("exposure: {graph: {}}\n").find("unknown key"), std::string::npos);
  EXPECT_NE(error("exposure: {topics: {allow: [[]]}}\n").find("expected a single value"),
    std::string::npos);
}

TEST(ExposurePolicy, NodesServicesActions)
{
  const BridgeConfig c = parse_config(
    "exposure:\n"
    "  nodes: {allow: ['/robot/**'], deny: ['/robot/private']}\n"
    "  services: {allow: ['/robot/*/get_parameters']}\n"
    "  actions: {allow: ['/navigate*'], deny: ['/navigate_secret']}\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.node_visible("/robot/driver"));
  EXPECT_FALSE(p.node_visible("/robot/private"));
  EXPECT_FALSE(p.node_visible("/other"));
  EXPECT_TRUE(p.service_visible("/robot/driver/get_parameters"));
  EXPECT_FALSE(p.service_visible("/robot/driver/set_parameters"));
  EXPECT_TRUE(p.action_visible("/navigate_to_pose"));
  EXPECT_FALSE(p.action_visible("/navigate_secret"));
  // Topic rules are separate.
  EXPECT_FALSE(p.topic_visible("/robot/driver"));
  // Nothing without the read capability.
  const BridgeConfig nr = parse_config(
    "capabilities: {read: false}\nexposure: {nodes: {allow: ['/**']}, services: {allow: ['/**']},"
    " actions: {allow: ['/**']}}\n", "test");
  const ExposurePolicy q(nr.exposure, nr.capabilities);
  EXPECT_FALSE(q.node_visible("/a"));
  EXPECT_FALSE(q.service_visible("/a"));
  EXPECT_FALSE(q.action_visible("/a"));
  // The default config shows nothing.
  const BridgeConfig d = load_config_file(DEFAULT_CONFIG);
  const ExposurePolicy r(d.exposure, d.capabilities);
  EXPECT_FALSE(r.node_visible("/robotraconteur_ros2_debug_bridge"));
  EXPECT_FALSE(r.service_visible("/robotraconteur_ros2_debug_bridge/get_parameters"));
  EXPECT_FALSE(r.action_visible("/navigate_to_pose"));
  EXPECT_THROW(
    parse_config("exposure: {nodes: {publish: []}}\n", "test"), ConfigError);
}

TEST(ExposurePolicy, ServiceCallAndServe)
{
  const BridgeConfig c = parse_config(
    "capabilities: {call_service: true, serve_service: true}\n"
    "exposure:\n"
    "  services:\n"
    "    allow: ['/robot/**']\n"
    "    call: ['/robot/reset', '/tools/add']\n"
    "    serve: ['/external/*']\n"
    "    deny: ['/robot/secret*']\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.service_visible("/robot/status"));
  EXPECT_FALSE(p.service_callable("/robot/status"));  // visible, not callable
  EXPECT_TRUE(p.service_callable("/robot/reset"));
  EXPECT_TRUE(p.service_visible("/tools/add"));      // callable implies visible
  EXPECT_TRUE(p.service_servable("/external/plan"));
  EXPECT_TRUE(p.service_visible("/external/plan"));
  EXPECT_FALSE(p.service_servable("/robot/reset"));
  EXPECT_FALSE(p.service_visible("/robot/secret_key"));
  EXPECT_FALSE(p.service_callable("/robot/secret_key"));
  // The rules alone are not enough: both capabilities are off by default.
  const BridgeConfig d = parse_config(
    "exposure: {services: {call: ['/a'], serve: ['/b']}}\n", "test");
  const ExposurePolicy q(d.exposure, d.capabilities);
  EXPECT_FALSE(q.service_callable("/a"));
  EXPECT_FALSE(q.service_servable("/b"));
  EXPECT_FALSE(q.service_visible("/a"));
  EXPECT_THROW(parse_config("exposure: {services: {publish: []}}\n", "test"), ConfigError);
}

TEST(ExposurePolicy, Parameters)
{
  const BridgeConfig c = parse_config(
    "capabilities: {set_parameter: true}\n"
    "exposure: {parameters: {allow: ['/robot/**'], set: ['/robot/tuning', '/tools'],"
    " deny: ['/robot/safety']}}\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.parameters_readable("/robot/driver"));
  EXPECT_FALSE(p.parameters_settable("/robot/driver"));
  EXPECT_TRUE(p.parameters_settable("/robot/tuning"));
  EXPECT_TRUE(p.parameters_readable("/tools"));  // settable implies readable
  EXPECT_FALSE(p.parameters_readable("/robot/safety"));
  EXPECT_FALSE(p.parameters_settable("/robot/safety"));
  EXPECT_FALSE(p.parameters_readable("/other"));
  // set_parameter is off by default.
  const BridgeConfig d = parse_config("exposure: {parameters: {set: ['/a']}}\n", "test");
  const ExposurePolicy q(d.exposure, d.capabilities);
  EXPECT_FALSE(q.parameters_settable("/a"));
  EXPECT_FALSE(q.parameters_readable("/a"));
  const BridgeConfig def = load_config_file(DEFAULT_CONFIG);
  const ExposurePolicy r(def.exposure, def.capabilities);
  EXPECT_FALSE(r.parameters_readable("/robotraconteur_ros2_debug_bridge"));
}

TEST(ExposurePolicy, ActionSend)
{
  const BridgeConfig c = parse_config(
    "capabilities: {action: true}\n"
    "exposure: {actions: {allow: ['/nav/**'], send: ['/nav/navigate_to_pose', '/arm/move'],"
    " deny: ['/nav/dock']}}\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.action_visible("/nav/spin"));
  EXPECT_FALSE(p.action_sendable("/nav/spin"));
  EXPECT_TRUE(p.action_sendable("/nav/navigate_to_pose"));
  EXPECT_TRUE(p.action_visible("/arm/move"));  // sendable implies visible
  EXPECT_FALSE(p.action_visible("/nav/dock"));
  EXPECT_FALSE(p.action_sendable("/nav/dock"));
  const BridgeConfig d = parse_config("exposure: {actions: {send: ['/a']}}\n", "test");
  const ExposurePolicy q(d.exposure, d.capabilities);
  EXPECT_FALSE(q.action_sendable("/a"));  // the action capability is off by default
  EXPECT_THROW(parse_config("exposure: {actions: {call: []}}\n", "test"), ConfigError);
}

TEST(ExposurePolicy, Components)
{
  const BridgeConfig c = parse_config(
    "capabilities: {load_component: true}\n"
    "exposure: {components: {allow: ['/containers/*'], deny: ['/containers/core']}}\n",
    "test");
  const ExposurePolicy p(c.exposure, c.capabilities);
  EXPECT_TRUE(p.container_usable("/containers/tools"));
  EXPECT_FALSE(p.container_usable("/containers/core"));
  EXPECT_FALSE(p.container_usable("/other"));
  const BridgeConfig d = parse_config("exposure: {components: {allow: ['/**']}}\n", "test");
  const ExposurePolicy q(d.exposure, d.capabilities);
  EXPECT_FALSE(q.container_usable("/c"));  // load_component is off by default
}
