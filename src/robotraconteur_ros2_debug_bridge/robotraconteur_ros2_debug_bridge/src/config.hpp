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

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "exposure_policy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

/// A configuration that cannot be used (the message names the file and key).
class ConfigError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct TcpConfig
{
  bool enabled = false;
  uint16_t port = 34573;
  bool localhost_only = true;
  /// TLS (Robot Raconteur node certificate needed); require: reject plain.
  bool tls = false;
  bool require_tls = false;
};

struct AuthenticationConfig
{
  bool enabled = false;
  /// A Robot Raconteur password file ("user md5(password) privileges" per
  /// line); a relative path is relative to the configuration file.
  std::string password_file;
};

/// See config/default.yaml for the meaning and the safe defaults.
struct BridgeConfig
{
  std::string node_name = "ros2_debug_bridge";
  std::string service_name = "ros2_debug_bridge";
  bool intra_transport = true;
  bool local_transport = true;
  TcpConfig tcp;
  AuthenticationConfig authentication;
  /// Topics: with rmw_intraprocess_cpp, take messages from its tap API
  /// (native messages, no deserialization; docs/benchmark.md). Off by
  /// default: the conversion then runs in the publisher's publish().
  bool fast_path = false;
  bool node_announce = false;
  bool node_discovery_listening = false;
  /// Enabled capabilities; only "read" by default.
  std::set<std::string> capabilities{"read"};
  /// Deny by default: no rules.
  ExposureConfig exposure;
};

/// The capability names the configuration accepts (PLAN.md 2.5).
const std::vector<std::string> & capability_names();

/// Parses a configuration document. Unknown keys are errors.
BridgeConfig parse_config(const std::string & yaml_text, const std::string & source);

/// Reads and parses a configuration file.
BridgeConfig load_config_file(const std::string & path);

/// Robot Raconteur node setup flags for the configuration. Never includes
/// the hardware transport or a public LocalTransport server; TCP, announce
/// and discovery only when enabled.
uint32_t node_setup_flags(const BridgeConfig & config);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // CONFIG_HPP_
