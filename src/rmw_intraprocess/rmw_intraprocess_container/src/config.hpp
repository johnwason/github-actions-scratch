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

#ifndef CONFIG_HPP_
#define CONFIG_HPP_

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/parameter.hpp"

namespace rmw_intraprocess_container
{

/// A configuration file that cannot be used. The message names the file and,
/// where known, the line and the key.
class ConfigError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

enum class ExecutorType
{
  SingleThreaded,
  MultiThreaded,
  Events,
};

struct ExecutorConfig
{
  ExecutorType type = ExecutorType::SingleThreaded;
  /// MultiThreaded only; 0 lets rclcpp pick (number of cores).
  size_t threads = 0;
  /// One executor (of `type`) and one thread per component, as
  /// component_container_isolated does. The container's own node stays on the
  /// main executor.
  bool isolated = false;
};

struct ComponentConfig
{
  std::string package;
  std::string plugin;
  /// Empty: the name and namespace the component's constructor gives it.
  std::string name;
  std::string node_namespace;
  /// "from:=to" rules, in file order.
  std::vector<std::string> remappings;
  /// Inline parameters (override every parameter file).
  std::vector<rclcpp::Parameter> parameters;
  /// Absolute paths, after the global parameter_files.
  std::vector<std::string> parameter_files;
  /// Extra --ros-args for this node only (for example --log-level).
  std::vector<std::string> ros_arguments;
  bool use_intra_process_comms = false;
  bool enabled = true;
  /// 1-based line of the entry in the file (for messages); 0 if unknown.
  int line = 0;

  /// "package/plugin" plus the name when one is set.
  std::string describe() const;
};

struct HostConfig
{
  /// Empty: not set in the file.
  std::string rmw_implementation;
  /// Process-wide --ros-args given to rclcpp::init (logging and the like;
  /// components ignore global remappings and parameters, as in any component
  /// container).
  std::vector<std::string> ros_arguments;
  ExecutorConfig executor;
  std::string container_name = "component_container";
  std::string container_namespace = "/";
  /// Loaded by every component, before its own files.
  std::vector<std::string> parameter_files;
  bool continue_on_error = false;
  std::vector<ComponentConfig> components;
};

/// Parses a configuration document. Relative file paths resolve against
/// `base_dir`; `source` names the document in error messages.
HostConfig parse_config(
  const std::string & yaml_text, const std::string & base_dir, const std::string & source);

/// Reads and parses a configuration file (relative paths resolve against its
/// directory).
HostConfig load_config_file(const std::string & path);

/// Replaces $(find-pkg-share PKG), $(env NAME) and $(env NAME DEFAULT) in
/// `value`. Throws ConfigError for an unknown package, an unset variable
/// without a default, or a malformed substitution.
std::string substitute(const std::string & value);

const char * executor_type_name(ExecutorType type);

}  // namespace rmw_intraprocess_container

#endif  // CONFIG_HPP_
