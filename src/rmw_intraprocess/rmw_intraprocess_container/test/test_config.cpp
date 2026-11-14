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

// The configuration file parser of container_host (no rclcpp needed).

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "rcutils/env.h"

#include "config.hpp"

using rmw_intraprocess_container::ComponentConfig;
using rmw_intraprocess_container::ConfigError;
using rmw_intraprocess_container::ExecutorType;
using rmw_intraprocess_container::HostConfig;
using rmw_intraprocess_container::load_config_file;
using rmw_intraprocess_container::parse_config;
using rmw_intraprocess_container::substitute;

namespace
{

HostConfig parse(const std::string & text, const std::string & base = "/base")
{
  return parse_config(text, base, "test.yaml");
}

/// The ConfigError message for `text` (fails the test if it parses).
std::string error_of(const std::string & text)
{
  try {
    parse(text);
  } catch (const ConfigError & e) {
    return e.what();
  }
  ADD_FAILURE() << "parsed without error:\n" << text;
  return "";
}

std::string path(const std::string & p)
{
  return std::filesystem::path(p).lexically_normal().string();
}

const rclcpp::Parameter & find(const ComponentConfig & c, const std::string & name)
{
  for (const auto & p : c.parameters) {
    if (p.get_name() == name) {
      return p;
    }
  }
  throw std::runtime_error("no parameter " + name);
}

}  // namespace

TEST(Config, MinimalUsesDefaults)
{
  const HostConfig c =
    parse("components:\n  - {package: composition, plugin: composition::Talker}\n");
  EXPECT_TRUE(c.rmw_implementation.empty());
  EXPECT_TRUE(c.ros_arguments.empty());
  EXPECT_EQ(ExecutorType::SingleThreaded, c.executor.type);
  EXPECT_EQ(0u, c.executor.threads);
  EXPECT_FALSE(c.executor.isolated);
  EXPECT_EQ("component_container", c.container_name);
  EXPECT_EQ("/", c.container_namespace);
  EXPECT_FALSE(c.continue_on_error);
  ASSERT_EQ(1u, c.components.size());
  const ComponentConfig & t = c.components[0];
  EXPECT_EQ("composition", t.package);
  EXPECT_EQ("composition::Talker", t.plugin);
  EXPECT_TRUE(t.name.empty());
  EXPECT_TRUE(t.node_namespace.empty());
  EXPECT_FALSE(t.use_intra_process_comms);
  EXPECT_TRUE(t.enabled);
  EXPECT_EQ(2, t.line);
  EXPECT_EQ("composition/composition::Talker", t.describe());
}

TEST(Config, EmptyComponentListIsAllowed)
{
  EXPECT_TRUE(parse("components: []\n").components.empty());
}

TEST(Config, AllKeys)
{
  const HostConfig c = parse(
    "rmw_implementation: rmw_fastrtps_cpp\n"
    "ros_arguments: [--log-level, debug]\n"
    "executor: {type: multi_threaded, threads: 3}\n"
    "container: {name: host, namespace: /robot}\n"
    "parameter_files: [common.yaml, /abs/other.yaml]\n"
    "use_intra_process_comms: true\n"
    "continue_on_error: yes\n"
    "components:\n"
    "  - package: p\n"
    "    plugin: p::A\n"
    "    name: a\n"
    "    namespace: /ns\n"
    "    remappings: {in: /x, out: /y}\n"
    "    parameter_files: sub/a.yaml\n"
    "    ros_arguments: [--log-level, 'a:=warn']\n"
    "    enabled: false\n"
    "  - package: p\n"
    "    plugin: p::B\n"
    "    use_intra_process_comms: false\n");
  EXPECT_EQ("rmw_fastrtps_cpp", c.rmw_implementation);
  EXPECT_EQ((std::vector<std::string>{"--log-level", "debug"}), c.ros_arguments);
  EXPECT_EQ(ExecutorType::MultiThreaded, c.executor.type);
  EXPECT_EQ(3u, c.executor.threads);
  EXPECT_EQ("host", c.container_name);
  EXPECT_EQ("/robot", c.container_namespace);
  EXPECT_EQ(
    (std::vector<std::string>{path("/base/common.yaml"), path("/abs/other.yaml")}),
    c.parameter_files);
  EXPECT_TRUE(c.continue_on_error);
  ASSERT_EQ(2u, c.components.size());
  const ComponentConfig & a = c.components[0];
  EXPECT_EQ("a", a.name);
  EXPECT_EQ("/ns", a.node_namespace);
  EXPECT_EQ((std::vector<std::string>{"in:=/x", "out:=/y"}), a.remappings);
  EXPECT_EQ((std::vector<std::string>{path("/base/sub/a.yaml")}), a.parameter_files);
  EXPECT_EQ((std::vector<std::string>{"--log-level", "a:=warn"}), a.ros_arguments);
  EXPECT_TRUE(a.use_intra_process_comms);  // the file-wide default
  EXPECT_FALSE(a.enabled);
  EXPECT_FALSE(c.components[1].use_intra_process_comms);  // overridden
  EXPECT_EQ("p/p::A (a)", a.describe());
}

TEST(Config, ExecutorForms)
{
  const char * tail = "components: []\n";
  EXPECT_EQ(
    ExecutorType::Events, parse(std::string("executor: events\n") + tail).executor.type);
  EXPECT_EQ(
    ExecutorType::MultiThreaded,
    parse(std::string("executor: multi_threaded\n") + tail).executor.type);
  const HostConfig iso = parse(std::string("executor: {isolated: true}\n") + tail);
  EXPECT_TRUE(iso.executor.isolated);
  EXPECT_EQ(ExecutorType::SingleThreaded, iso.executor.type);
  EXPECT_TRUE(
    parse(std::string("executor: {type: multi_threaded, isolated: true}\n") + tail)
    .executor.isolated);

  EXPECT_NE(
    std::string::npos,
    error_of(std::string("executor: static\n") + tail).find("not one of"));
  EXPECT_NE(
    std::string::npos,
    error_of(std::string("executor: {type: single_threaded, threads: 2}\n") + tail)
    .find("multi_threaded"));
  EXPECT_NE(
    std::string::npos,
    error_of(std::string("executor: {type: multi_threaded, threads: -1}\n") + tail)
    .find("executor.threads"));
  EXPECT_NE(
    std::string::npos,
    error_of(std::string("executor: {type: events, isolated: true}\n") + tail)
    .find("executor.isolated"));
  EXPECT_NE(
    std::string::npos,
    error_of(std::string("executor: {kind: events}\n") + tail).find("executor.kind"));
}

TEST(Config, ParametersAreTypedLikeParameterFiles)
{
  const HostConfig c = parse(
    "components:\n"
    "  - package: p\n"
    "    plugin: p::A\n"
    "    parameters:\n"
    "      flag: true\n"
    "      count: 7\n"
    "      ratio: 0.25\n"
    "      label: hello\n"
    "      quoted_number: \"5\"\n"
    "      single_quoted_bool: 'true'\n"
    "      ints: [1, 2, 3]\n"
    "      doubles: [1.5, 2.0]\n"
    "      bools: [true, false]\n"
    "      strings: [a, \"1\"]\n"
    "      nested:\n"
    "        deeper:\n"
    "          value: 9\n");
  const ComponentConfig & a = c.components.at(0);
  ASSERT_EQ(11u, a.parameters.size());
  EXPECT_EQ(rclcpp::ParameterType::PARAMETER_BOOL, find(a, "flag").get_type());
  EXPECT_TRUE(find(a, "flag").as_bool());
  EXPECT_EQ(7, find(a, "count").as_int());
  EXPECT_DOUBLE_EQ(0.25, find(a, "ratio").as_double());
  EXPECT_EQ("hello", find(a, "label").as_string());
  EXPECT_EQ("5", find(a, "quoted_number").as_string());
  EXPECT_EQ("true", find(a, "single_quoted_bool").as_string());
  EXPECT_EQ((std::vector<int64_t>{1, 2, 3}), find(a, "ints").as_integer_array());
  EXPECT_EQ((std::vector<double>{1.5, 2.0}), find(a, "doubles").as_double_array());
  EXPECT_EQ((std::vector<bool>{true, false}), find(a, "bools").as_bool_array());
  EXPECT_EQ((std::vector<std::string>{"a", "1"}), find(a, "strings").as_string_array());
  EXPECT_EQ(9, find(a, "nested.deeper.value").as_int());
}

TEST(Config, BadParameters)
{
  const std::string head = "components:\n  - package: p\n    plugin: p::A\n    parameters:\n";
  EXPECT_NE(std::string::npos, error_of(head + "      e: []\n").find("empty list"));
  EXPECT_NE(std::string::npos, error_of(head + "      m: [1, a]\n").find("parameters.m"));
  EXPECT_NE(std::string::npos, error_of(head + "      n: [[1]]\n").find("single values"));
  EXPECT_NE(std::string::npos, error_of(head + "      z:\n").find("has no value"));
  EXPECT_NE(
    std::string::npos,
    error_of("components:\n  - package: p\n    plugin: p::A\n    parameters: [1]\n")
    .find("mapping"));
}

TEST(Config, Remappings)
{
  const HostConfig c = parse(
    "components:\n"
    "  - {package: p, plugin: p::A, remappings: [\"a:=b\", \"__node:=x\"]}\n");
  EXPECT_EQ((std::vector<std::string>{"a:=b", "__node:=x"}), c.components[0].remappings);
  EXPECT_NE(
    std::string::npos,
    error_of("components:\n  - {package: p, plugin: p::A, remappings: [ab]}\n")
    .find("not a remapping rule"));
}

TEST(Config, ErrorsNameTheKeyAndLine)
{
  const std::string typo = error_of(
    "components:\n"
    "  - package: p\n"
    "    plugin: p::A\n"
    "    nmae: typo\n");
  EXPECT_NE(std::string::npos, typo.find("test.yaml:4")) << typo;
  EXPECT_NE(std::string::npos, typo.find("components[0].nmae")) << typo;
  EXPECT_NE(std::string::npos, typo.find("unknown key")) << typo;

  EXPECT_NE(std::string::npos, error_of("compnents: []\n").find("unknown key"));
  EXPECT_NE(std::string::npos, error_of("executor: events\n").find("components: missing"));
  EXPECT_NE(std::string::npos, error_of("components: {a: 1}\n").find("list"));
  EXPECT_NE(std::string::npos, error_of("components:\n  - {package: p}\n").find(".plugin"));
  EXPECT_NE(std::string::npos, error_of("components:\n  - {plugin: p::A}\n").find(".package"));
  EXPECT_NE(std::string::npos, error_of("components:\n  - just_a_name\n").find("mapping"));
  EXPECT_NE(std::string::npos, error_of("- a\n- b\n").find("must be a mapping"));
  EXPECT_NE(
    std::string::npos,
    error_of("continue_on_error: maybe\ncomponents: []\n").find("true or false"));
  EXPECT_NE(
    std::string::npos,
    error_of("container: {name: [a]}\ncomponents: []\n").find("container.name"));
  const std::string syntax = error_of("components:\n  - {package: p\n");
  EXPECT_NE(std::string::npos, syntax.find("test.yaml:")) << syntax;
}

TEST(Config, Substitutions)
{
  ASSERT_TRUE(rcutils_set_env("RMW_INTRAPROCESS_CONTAINER_TEST_VAR", "value"));
  EXPECT_EQ("a/value/b", substitute("a/$(env RMW_INTRAPROCESS_CONTAINER_TEST_VAR)/b"));
  EXPECT_EQ("x", substitute("$(env RMW_INTRAPROCESS_CONTAINER_TEST_UNSET x)"));
  EXPECT_EQ("plain", substitute("plain"));
  EXPECT_THROW(substitute("$(env RMW_INTRAPROCESS_CONTAINER_TEST_UNSET)"), ConfigError);
  EXPECT_THROW(substitute("$(find-pkg-share no_such_package_anywhere)"), ConfigError);
  EXPECT_THROW(substitute("$(dirname)"), ConfigError);
  EXPECT_THROW(substitute("$(env X"), ConfigError);
  // A package every ROS installation has.
  const std::string share = substitute("$(find-pkg-share rclcpp)/x");
  EXPECT_TRUE(std::filesystem::is_directory(share.substr(0, share.size() - 2))) << share;

  // Substitutions apply to values, including parameter values and paths.
  const HostConfig c = parse(
    "parameter_files: [\"$(env RMW_INTRAPROCESS_CONTAINER_TEST_VAR)/p.yaml\"]\n"
    "components:\n"
    "  - package: p\n"
    "    plugin: p::A\n"
    "    parameters: {s: \"$(env RMW_INTRAPROCESS_CONTAINER_TEST_VAR)\"}\n");
  EXPECT_EQ(path("/base/value/p.yaml"), c.parameter_files.at(0));
  EXPECT_EQ("value", c.components[0].parameters.at(0).as_string());
  EXPECT_NE(
    std::string::npos,
    error_of("components:\n  - {package: \"$(env RMW_INTRAPROCESS_CONTAINER_TEST_UNSET)\", "
    "plugin: a}\n").find("variable not set"));
}

TEST(Config, LoadFileResolvesPathsNextToTheFile)
{
  const std::string dir = TEST_CONFIG_DIR;
  const HostConfig c = load_config_file(dir + "/talker_listener.yaml");
  ASSERT_EQ(1u, c.parameter_files.size());
  EXPECT_TRUE(std::filesystem::is_regular_file(c.parameter_files[0])) << c.parameter_files[0];
  ASSERT_EQ(3u, c.components.size());
  ASSERT_EQ(1u, c.components[2].parameter_files.size());
  EXPECT_TRUE(std::filesystem::is_regular_file(c.components[2].parameter_files[0]));
  EXPECT_THROW(load_config_file(dir + "/no_such_file.yaml"), ConfigError);
  EXPECT_THROW(load_config_file(dir + "/bad_key.yaml"), ConfigError);
}
