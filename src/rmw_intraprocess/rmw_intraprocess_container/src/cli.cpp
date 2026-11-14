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

#include "cli.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rcutils/env.h"
#include "rmw/rmw.h"

#include "config.hpp"
#include "host.hpp"

namespace rmw_intraprocess_container
{

namespace
{

constexpr const char * kDefaultRmw = "rmw_intraprocess_cpp";

void usage(std::ostream & out)
{
  out <<
    "usage: container_host [options] CONFIG.yaml [--ros-args ...]\n"
    "\n"
    "Loads the ROS 2 components listed in CONFIG.yaml into this process and spins\n"
    "them until Ctrl+C, until a component calls rclcpp::shutdown(), or until\n"
    "--shutdown-after expires. See docs/container.md for the file format.\n"
    "\n"
    "options:\n"
    "  -c, --config FILE         the configuration file (or give it as the argument)\n"
    "  --rmw NAME                the RMW implementation; overrides the file and\n"
    "                            RMW_IMPLEMENTATION (default: rmw_intraprocess_cpp)\n"
    "  --check                   validate the file and find every component's library\n"
    "                            without loading anything, then exit\n"
    "  --list-components PKG     list the components package PKG registers, then exit\n"
    "  --shutdown-after SECONDS  stop after this many seconds (smoke tests)\n"
    "  -h, --help                show this help\n"
    "\n"
    "Arguments after --ros-args go to rclcpp::init (process-wide, for example\n"
    "--log-level); components do not see global remappings or parameters.\n"
    "\n"
    "exit status: 0 success, 1 a component failed to load, 2 bad usage or file\n";
}

struct Options
{
  std::string config;
  std::string rmw;
  std::string list_package;
  bool check = false;
  bool help = false;
  double shutdown_after = -1.0;
  std::vector<std::string> ros_arguments;
};

/// Throws ConfigError for a bad command line.
Options parse_options(int argc, char ** argv)
{
  Options o;
  auto value = [&](int & i, const std::string & name) -> std::string {
      if (i + 1 >= argc) {
        throw ConfigError(name + " needs a value");
      }
      return argv[++i];
    };
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ros-args") {
      o.ros_arguments.assign(argv + i, argv + argc);
      break;
    } else if (a == "-h" || a == "--help") {
      o.help = true;
    } else if (a == "-c" || a == "--config") {
      o.config = value(i, a);
    } else if (a == "--rmw") {
      o.rmw = value(i, a);
    } else if (a == "--check") {
      o.check = true;
    } else if (a == "--list-components") {
      o.list_package = value(i, a);
    } else if (a == "--shutdown-after") {
      const std::string v = value(i, a);
      char * end = nullptr;
      o.shutdown_after = std::strtod(v.c_str(), &end);
      if (end == v.c_str() || *end != '\0' || !(o.shutdown_after >= 0.0)) {
        throw ConfigError("--shutdown-after: '" + v + "' is not a number of seconds >= 0");
      }
    } else if (!a.empty() && a[0] == '-') {
      throw ConfigError("unknown option '" + a + "'");
    } else if (o.config.empty()) {
      o.config = a;
    } else {
      throw ConfigError("more than one configuration file given ('" + a + "')");
    }
  }
  return o;
}

/// Checks without loading: each enabled component's package registers the
/// plugin and its library exists; parameter files exist.
bool check(const HostConfig & config, std::ostream & out, std::ostream & err)
{
  bool ok = true;
  auto check_file = [&](const std::string & file, const std::string & what) {
      std::error_code ec;
      if (!std::filesystem::is_regular_file(file, ec)) {
        err << what << ": parameter file '" << file << "' not found\n";
        ok = false;
      }
    };
  for (const auto & f : config.parameter_files) {
    check_file(f, "parameter_files");
  }
  for (const auto & c : config.components) {
    const std::string what = c.describe() + " (line " + std::to_string(c.line) + ")";
    if (!c.enabled) {
      out << "skip  " << what << ": disabled\n";
      continue;
    }
    for (const auto & f : c.parameter_files) {
      check_file(f, what);
    }
    try {
      std::string library;
      for (const auto & r : component_resources(c.package)) {
        if (r.first == c.plugin) {
          library = r.second;
        }
      }
      if (library.empty()) {
        err << what << ": package '" << c.package << "' has no component '" << c.plugin <<
          "' (see --list-components " << c.package << ")\n";
        ok = false;
        continue;
      }
      std::error_code ec;
      if (!std::filesystem::is_regular_file(library, ec)) {
        err << what << ": library '" << library << "' not found\n";
        ok = false;
        continue;
      }
      out << "ok    " << what << ": " << library << "\n";
    } catch (const std::exception & e) {
      err << what << ": " << e.what() << "\n";
      ok = false;
    }
  }
  return ok;
}

}  // namespace

int run(int argc, char ** argv, std::ostream & out, std::ostream & err)
{
  Options options;
  HostConfig config;
  try {
    options = parse_options(argc, argv);
    if (options.help) {
      usage(out);
      return kExitOk;
    }
    if (!options.list_package.empty()) {
      for (const auto & r : component_resources(options.list_package)) {
        out << r.first << "\n";
      }
      return kExitOk;
    }
    if (options.config.empty()) {
      usage(err);
      return kExitUsage;
    }
    config = load_config_file(options.config);
  } catch (const ConfigError & e) {
    err << "container_host: " << e.what() << "\n";
    return kExitUsage;
  } catch (const LoadError & e) {
    err << "container_host: " << e.what() << "\n";
    return kExitUsage;
  }

  if (options.check) {
    const bool ok = check(config, out, err);
    out << (ok ? "configuration OK\n" : "configuration has errors\n");
    return ok ? kExitOk : kExitLoadError;
  }

  // The RMW library is loaded by the first rmw call (in rclcpp::init), so
  // setting RMW_IMPLEMENTATION here selects it.
  std::string rmw = options.rmw;
  std::string rmw_source = "--rmw";
  if (rmw.empty() && !config.rmw_implementation.empty()) {
    rmw = config.rmw_implementation;
    rmw_source = options.config;
  }
  if (rmw.empty()) {
    const char * env = nullptr;
    if (rcutils_get_env("RMW_IMPLEMENTATION", &env) == nullptr && env != nullptr && *env != '\0') {
      rmw = env;
      rmw_source = "RMW_IMPLEMENTATION";
    } else {
      rmw = kDefaultRmw;
      rmw_source = "default";
    }
  }
  if (!rcutils_set_env("RMW_IMPLEMENTATION", rmw.c_str())) {
    err << "container_host: cannot set RMW_IMPLEMENTATION\n";
    return kExitLoadError;
  }

  std::vector<std::string> init_args{argc > 0 ? argv[0] : "container_host"};
  if (!config.ros_arguments.empty()) {
    init_args.push_back("--ros-args");
    init_args.insert(init_args.end(), config.ros_arguments.begin(), config.ros_arguments.end());
    init_args.push_back("--");
  }
  init_args.insert(init_args.end(), options.ros_arguments.begin(), options.ros_arguments.end());
  std::vector<const char *> init_argv;
  for (const auto & a : init_args) {
    init_argv.push_back(a.c_str());
  }

  try {
    rclcpp::init(static_cast<int>(init_argv.size()), init_argv.data());
  } catch (const std::exception & e) {
    err << "container_host: rclcpp::init failed with RMW '" << rmw << "': " << e.what() << "\n";
    return kExitLoadError;
  }

  int code = kExitOk;
  {
    const auto logger = rclcpp::get_logger("container_host");
    const char * identifier = rmw_get_implementation_identifier();
    RCLCPP_INFO(
      logger, "RMW %s (from %s), executor %s%s, %zu component(s)",
      identifier != nullptr ? identifier : "?", rmw_source.c_str(),
      executor_type_name(config.executor.type), config.executor.isolated ? " (isolated)" : "",
      config.components.size());
    try {
      ComponentHost host(config);
      if (!host.load_all()) {
        code = kExitLoadError;
      }
      rclcpp::TimerBase::SharedPtr timer;
      if (code == kExitOk) {
        if (options.shutdown_after >= 0.0) {
          timer = host.container_node()->create_wall_timer(
            std::chrono::duration<double>(options.shutdown_after),
            [&host, logger]() {
              RCLCPP_INFO(logger, "--shutdown-after expired");
              host.request_stop();
            });
        }
        host.spin();
      }
      timer.reset();
      RCLCPP_INFO(logger, "Shutting down");
      host.shutdown();
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger, "%s", e.what());
      code = kExitLoadError;
    }
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return code;
}

}  // namespace rmw_intraprocess_container
