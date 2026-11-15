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

#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rcl_yaml_param_parser/parser.h"
#include "rclcpp/parameter_map.hpp"
#include "rcutils/allocator.h"
#include "rcutils/error_handling.h"
#include "rcutils/env.h"
#include "yaml-cpp/yaml.h"

namespace rmw_intraprocess_container
{

namespace
{

class Parser
{
public:
  Parser(std::string source, std::string base_dir)
  : source_(std::move(source)), base_dir_(std::move(base_dir)) {}

  HostConfig parse(const std::string & text)
  {
    YAML::Node root;
    try {
      root = YAML::Load(text);
    } catch (const YAML::Exception & e) {
      throw ConfigError(source_ + ":" + std::to_string(e.mark.line + 1) + ": " + e.msg);
    }
    if (!root.IsMap()) {
      fail(root, "", "the file must be a mapping (key: value) with a 'components' list");
    }
    check_keys(
      root, "", {"rmw_implementation", "ros_arguments", "executor", "container",
        "parameter_files", "use_intra_process_comms", "continue_on_error", "components"});

    HostConfig config;
    if (root["rmw_implementation"]) {
      config.rmw_implementation = scalar(root["rmw_implementation"], "rmw_implementation");
    }
    if (root["ros_arguments"]) {
      config.ros_arguments = strings(root["ros_arguments"], "ros_arguments", true);
    }
    if (root["executor"]) {
      config.executor = executor(root["executor"]);
    }
    if (root["container"]) {
      const YAML::Node c = root["container"];
      if (!c.IsMap()) {
        fail(c, "container", "expected a mapping with 'name' and/or 'namespace'");
      }
      check_keys(c, "container", {"name", "namespace"});
      if (c["name"]) {
        config.container_name = scalar(c["name"], "container.name");
      }
      if (c["namespace"]) {
        config.container_namespace = scalar(c["namespace"], "container.namespace");
      }
    }
    if (root["parameter_files"]) {
      config.parameter_files = files(root["parameter_files"], "parameter_files");
    }
    bool default_ipc = false;
    if (root["use_intra_process_comms"]) {
      default_ipc = boolean(root["use_intra_process_comms"], "use_intra_process_comms");
    }
    if (root["continue_on_error"]) {
      config.continue_on_error = boolean(root["continue_on_error"], "continue_on_error");
    }

    const YAML::Node components = root["components"];
    if (!components || components.IsNull()) {
      fail(root, "components", "missing: list the components to load");
    }
    if (!components.IsSequence()) {
      fail(components, "components", "expected a list of components");
    }
    for (size_t i = 0; i < components.size(); ++i) {
      config.components.push_back(
        component(components[i], "components[" + std::to_string(i) + "]", default_ipc));
    }
    return config;
  }

private:
  [[noreturn]] void fail(const YAML::Node & node, const std::string & key, const std::string & msg)
  const
  {
    std::string where = source_;
    if (node.IsDefined() && !node.Mark().is_null()) {
      where += ":" + std::to_string(node.Mark().line + 1);
    }
    throw ConfigError(where + ": " + (key.empty() ? msg : key + ": " + msg));
  }

  void check_keys(
    const YAML::Node & map, const std::string & where, const std::set<std::string> & allowed) const
  {
    for (const auto & kv : map) {
      const std::string key = kv.first.Scalar();
      if (allowed.count(key) == 0) {
        std::string list;
        for (const auto & a : allowed) {
          list += (list.empty() ? "" : ", ") + a;
        }
        fail(
          kv.first, where.empty() ? key : where + "." + key,
          "unknown key (allowed: " + list + ")");
      }
    }
  }

  std::string scalar(const YAML::Node & node, const std::string & key) const
  {
    if (!node.IsScalar()) {
      fail(node, key, "expected a single value");
    }
    return substitute_at(node, key, node.Scalar());
  }

  bool boolean(const YAML::Node & node, const std::string & key) const
  {
    bool value = false;
    if (!node.IsScalar() || !YAML::convert<bool>::decode(node, value)) {
      fail(node, key, "expected true or false");
    }
    return value;
  }

  std::vector<std::string> strings(
    const YAML::Node & node, const std::string & key, bool allow_single) const
  {
    std::vector<std::string> out;
    if (allow_single && node.IsScalar()) {
      out.push_back(scalar(node, key));
      return out;
    }
    if (!node.IsSequence()) {
      fail(node, key, "expected a list");
    }
    for (const auto & item : node) {
      out.push_back(scalar(item, key));
    }
    return out;
  }

  std::vector<std::string> files(const YAML::Node & node, const std::string & key) const
  {
    std::vector<std::string> out = strings(node, key, true);
    for (auto & f : out) {
      std::filesystem::path p(f);
      if (p.is_relative()) {
        p = std::filesystem::path(base_dir_) / p;
      }
      f = p.lexically_normal().string();
    }
    return out;
  }

  std::string substitute_at(
    const YAML::Node & node, const std::string & key, const std::string & value) const
  {
    try {
      return substitute(value);
    } catch (const ConfigError & e) {
      fail(node, key, e.what());
    }
  }

  ExecutorConfig executor(const YAML::Node & node) const
  {
    ExecutorConfig out;
    // Initialized once: assigning to a YAML::Node writes through to the node
    // it refers to.
    const YAML::Node type = node.IsMap() ? node["type"] : node;
    if (node.IsMap()) {
      check_keys(node, "executor", {"type", "threads", "isolated"});
      const YAML::Node threads_node = node["threads"];
      if (threads_node) {
        int64_t threads = -1;
        if (!threads_node.IsScalar() || !YAML::convert<int64_t>::decode(threads_node, threads) ||
          threads < 0)
        {
          fail(threads_node, "executor.threads", "expected a whole number >= 0");
        }
        out.threads = static_cast<size_t>(threads);
      }
      if (node["isolated"]) {
        out.isolated = boolean(node["isolated"], "executor.isolated");
      }
    }
    if (type) {
      const std::string name = scalar(type, "executor.type");
      if (name == "single_threaded") {
        out.type = ExecutorType::SingleThreaded;
      } else if (name == "multi_threaded") {
        out.type = ExecutorType::MultiThreaded;
      } else if (name == "events") {
        out.type = ExecutorType::Events;
      } else {
        fail(type, "executor.type", "'" + name + "' is not one of single_threaded, "
          "multi_threaded, events");
      }
    }
    if (out.threads != 0 && out.type != ExecutorType::MultiThreaded) {
      fail(node, "executor.threads", "only applies to the multi_threaded executor");
    }
    if (out.isolated && out.type == ExecutorType::Events) {
      fail(node, "executor.isolated", "isolated executors are single_threaded or multi_threaded");
    }
    return out;
  }

  ComponentConfig component(const YAML::Node & node, const std::string & where, bool default_ipc)
  const
  {
    if (!node.IsMap()) {
      fail(node, where, "expected a mapping with at least 'package' and 'plugin'");
    }
    check_keys(
      node, where, {"package", "plugin", "name", "namespace", "remappings", "parameters",
        "parameter_files", "ros_arguments", "use_intra_process_comms", "enabled"});
    ComponentConfig c;
    c.line = node.Mark().is_null() ? 0 : node.Mark().line + 1;
    c.use_intra_process_comms = default_ipc;
    if (!node["package"]) {
      fail(node, where + ".package", "missing");
    }
    if (!node["plugin"]) {
      fail(node, where + ".plugin", "missing (the class name, for example composition::Talker)");
    }
    c.package = scalar(node["package"], where + ".package");
    c.plugin = scalar(node["plugin"], where + ".plugin");
    if (node["name"]) {
      c.name = scalar(node["name"], where + ".name");
    }
    if (node["namespace"]) {
      c.node_namespace = scalar(node["namespace"], where + ".namespace");
    }
    if (node["remappings"]) {
      c.remappings = remappings(node["remappings"], where + ".remappings");
    }
    if (node["parameters"]) {
      c.parameters = parameters(node["parameters"], where + ".parameters");
    }
    if (node["parameter_files"]) {
      c.parameter_files = files(node["parameter_files"], where + ".parameter_files");
    }
    if (node["ros_arguments"]) {
      c.ros_arguments = strings(node["ros_arguments"], where + ".ros_arguments", true);
    }
    if (node["use_intra_process_comms"]) {
      c.use_intra_process_comms =
        boolean(node["use_intra_process_comms"], where + ".use_intra_process_comms");
    }
    if (node["enabled"]) {
      c.enabled = boolean(node["enabled"], where + ".enabled");
    }
    return c;
  }

  std::vector<std::string> remappings(const YAML::Node & node, const std::string & key) const
  {
    std::vector<std::string> out;
    if (node.IsMap()) {
      for (const auto & kv : node) {
        out.push_back(
          scalar(kv.first, key) + ":=" + scalar(kv.second, key + "." + kv.first.Scalar()));
      }
      return out;
    }
    if (!node.IsSequence()) {
      fail(node, key, "expected a mapping {from: to} or a list of \"from:=to\"");
    }
    for (const auto & item : node) {
      const std::string rule = scalar(item, key);
      if (rule.find(":=") == std::string::npos) {
        fail(item, key, "'" + rule + "' is not a remapping rule (from:=to)");
      }
      out.push_back(rule);
    }
    return out;
  }

  // Parameters: nested mappings flatten to dotted names (as in a ROS
  // parameter file). Each leaf is typed by rcl's own parameter-file parser, so
  // the rules match parameter files exactly (quoted values are strings, lists
  // must be uniform, and so on).
  std::vector<rclcpp::Parameter> parameters(const YAML::Node & node, const std::string & key) const
  {
    if (!node.IsMap()) {
      fail(node, key, "expected a mapping of parameter names to values");
    }
    std::vector<rclcpp::Parameter> out;
    flatten(node, "", key, out);
    return out;
  }

  void flatten(
    const YAML::Node & map, const std::string & prefix, const std::string & key,
    std::vector<rclcpp::Parameter> & out) const
  {
    for (const auto & kv : map) {
      const std::string name = prefix + kv.first.Scalar();
      const YAML::Node & value = kv.second;
      if (value.IsMap()) {
        flatten(value, name + ".", key, out);
      } else {
        out.push_back(parameter(name, value, key + "." + name));
      }
    }
  }

  rclcpp::Parameter parameter(
    const std::string & name, const YAML::Node & value, const std::string & key) const
  {
    YAML::Emitter emitter;
    emitter << YAML::Flow;
    if (value.IsScalar()) {
      emit_scalar(emitter, value, key);
    } else if (value.IsSequence()) {
      if (value.size() == 0) {
        fail(value, key, "an empty list has no type; omit the parameter or give a value");
      }
      emitter << YAML::BeginSeq;
      for (const auto & item : value) {
        if (!item.IsScalar()) {
          fail(item, key, "list items must be single values");
        }
        emit_scalar(emitter, item, key);
      }
      emitter << YAML::EndSeq;
    } else {
      fail(value, key, "has no value");
    }

    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rcl_params_t * params = rcl_yaml_node_struct_init(allocator);
    if (params == nullptr) {
      throw std::bad_alloc();
    }
    std::string error;
    rclcpp::ParameterMap map;
    if (!rcl_parse_yaml_value("component", name.c_str(), emitter.c_str(), params)) {
      error = rcutils_get_error_string().str;
      rcutils_reset_error();
    } else {
      try {
        map = rclcpp::parameter_map_from(params);
      } catch (const std::exception & e) {
        error = e.what();
      }
    }
    rcl_yaml_node_struct_fini(params);
    if (!error.empty() || map.size() != 1 || map.begin()->second.size() != 1) {
      fail(
        value, key, "not a valid parameter value (" +
        (error.empty() ? std::string("unsupported value") : error) + ")");
    }
    return map.begin()->second.front();
  }

  void emit_scalar(YAML::Emitter & emitter, const YAML::Node & value, const std::string & key)
  const
  {
    // yaml-cpp tags quoted scalars "!": keep them quoted so they stay strings.
    const std::string text = substitute_at(value, key, value.Scalar());
    if (value.Tag() == "!") {
      emitter << YAML::DoubleQuoted << text;
    } else {
      emitter << text;
    }
  }

  std::string source_;
  std::string base_dir_;
};

}  // namespace

std::string ComponentConfig::describe() const
{
  std::string out = package + "/" + plugin;
  if (!name.empty()) {
    out += " (" + name + ")";
  }
  return out;
}

const char * executor_type_name(ExecutorType type)
{
  switch (type) {
    case ExecutorType::SingleThreaded: return "single_threaded";
    case ExecutorType::MultiThreaded: return "multi_threaded";
    case ExecutorType::Events: return "events";
  }
  return "unknown";
}

std::string substitute(const std::string & value)
{
  std::string out;
  size_t pos = 0;
  while (true) {
    const size_t start = value.find("$(", pos);
    if (start == std::string::npos) {
      out += value.substr(pos);
      return out;
    }
    const size_t end = value.find(')', start);
    if (end == std::string::npos) {
      throw ConfigError("unterminated substitution in '" + value + "'");
    }
    out += value.substr(pos, start - pos);
    std::istringstream words(value.substr(start + 2, end - start - 2));
    std::vector<std::string> args;
    for (std::string w; words >> w; ) {
      args.push_back(w);
    }
    if (args.size() == 2 && args[0] == "find-pkg-share") {
      try {
        out += ament_index_cpp::get_package_share_directory(args[1]);
      } catch (const std::exception &) {
        throw ConfigError("$(find-pkg-share " + args[1] + "): package not found");
      }
    } else if ((args.size() == 2 || args.size() == 3) && args[0] == "env") {
      const char * env = nullptr;
      const char * err = rcutils_get_env(args[1].c_str(), &env);
      if (err != nullptr || env == nullptr || *env == '\0') {
        if (args.size() == 2) {
          throw ConfigError("$(env " + args[1] + "): variable not set");
        }
        out += args[2];
      } else {
        out += env;
      }
    } else {
      throw ConfigError(
              "unknown substitution '" + value.substr(start, end - start + 1) +
              "' (supported: $(find-pkg-share PKG), $(env NAME), $(env NAME DEFAULT))");
    }
    pos = end + 1;
  }
}

HostConfig parse_config(
  const std::string & yaml_text, const std::string & base_dir, const std::string & source)
{
  return Parser(source, base_dir).parse(yaml_text);
}

HostConfig load_config_file(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw ConfigError(path + ": cannot open the file");
  }
  std::ostringstream text;
  text << in.rdbuf();
  const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path));
  return parse_config(text.str(), abs.parent_path().string(), path);
}

}  // namespace rmw_intraprocess_container
