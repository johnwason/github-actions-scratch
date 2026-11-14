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

// container_host's command line, run in this process (each full run
// initializes and shuts down rclcpp itself).

#include <gtest/gtest.h>

#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rcutils/logging.h"

#include "cli.hpp"

using rmw_intraprocess_container::kExitLoadError;
using rmw_intraprocess_container::kExitOk;
using rmw_intraprocess_container::kExitUsage;
using rmw_intraprocess_container::run;

namespace
{

struct Result
{
  int code;
  std::string out;
  std::string err;
};

Result run_cli(std::initializer_list<std::string> args)
{
  std::vector<std::string> storage{"container_host"};
  storage.insert(storage.end(), args.begin(), args.end());
  std::vector<char *> argv;
  for (auto & a : storage) {
    argv.push_back(a.data());
  }
  std::ostringstream out;
  std::ostringstream err;
  const int code = run(static_cast<int>(argv.size()), argv.data(), out, err);
  EXPECT_FALSE(rclcpp::ok()) << "run() left rclcpp initialized";
  return Result{code, out.str(), err.str()};
}

std::string config(const std::string & name)
{
  return std::string(TEST_CONFIG_DIR) + "/" + name;
}

bool contains(const std::string & haystack, const std::string & needle)
{
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Cli, Help)
{
  const Result r = run_cli({"--help"});
  EXPECT_EQ(kExitOk, r.code);
  EXPECT_TRUE(contains(r.out, "usage: container_host"));
}

TEST(Cli, UsageErrors)
{
  EXPECT_EQ(kExitUsage, run_cli({}).code);
  EXPECT_EQ(kExitUsage, run_cli({"--frobnicate"}).code);
  EXPECT_EQ(kExitUsage, run_cli({"a.yaml", "b.yaml"}).code);
  EXPECT_EQ(kExitUsage, run_cli({"--rmw"}).code);
  EXPECT_EQ(kExitUsage, run_cli({config("talker_listener.yaml"), "--shutdown-after", "x"}).code);
  EXPECT_EQ(kExitUsage, run_cli({config("talker_listener.yaml"), "--shutdown-after", "-1"}).code);
  const Result missing = run_cli({config("no_such_file.yaml")});
  EXPECT_EQ(kExitUsage, missing.code);
  EXPECT_TRUE(contains(missing.err, "cannot open")) << missing.err;
  const Result bad = run_cli({"--config", config("bad_key.yaml")});
  EXPECT_EQ(kExitUsage, bad.code);
  EXPECT_TRUE(contains(bad.err, "nmae")) << bad.err;
}

TEST(Cli, ListComponents)
{
  const Result r = run_cli({"--list-components", "composition"});
  EXPECT_EQ(kExitOk, r.code);
  EXPECT_TRUE(contains(r.out, "composition::Talker\n")) << r.out;
  EXPECT_EQ(kExitUsage, run_cli({"--list-components", "no_such_package_anywhere"}).code);
}

TEST(Cli, Check)
{
  const Result ok = run_cli({"--check", config("talker_listener.yaml")});
  EXPECT_EQ(kExitOk, ok.code) << ok.err;
  EXPECT_TRUE(contains(ok.out, "configuration OK")) << ok.out;
  const Result bad = run_cli({"--check", config("unknown_plugin.yaml")});
  EXPECT_EQ(kExitLoadError, bad.code);
  EXPECT_TRUE(contains(bad.err, "has no component 'composition::DoesNotExist'")) << bad.err;
}

TEST(Cli, RunsUntilShutdownAfter)
{
  const Result r = run_cli(
    {config("talker_listener.yaml"), "--shutdown-after", "1", "--ros-args", "--log-level",
      "warn"});
  EXPECT_EQ(kExitOk, r.code) << r.err;
}

TEST(Cli, LoadFailureExitsWithOne)
{
  EXPECT_EQ(kExitLoadError, run_cli({config("unknown_plugin.yaml")}).code);
}

TEST(Cli, ComponentShutdownEndsTheRun)
{
  // The Fibonacci action client calls rclcpp::shutdown() when its goal
  // succeeds; no --shutdown-after.
  // INFO again (an earlier run set --log-level warn, which outlives its
  // rclcpp::shutdown()), so a failure leaves the demo's trace in the log.
  rcutils_logging_set_default_logger_level(RCUTILS_LOG_SEVERITY_INFO);
  const Result r = run_cli({config("fibonacci.yaml")});
  EXPECT_EQ(kExitOk, r.code) << r.err;
}
