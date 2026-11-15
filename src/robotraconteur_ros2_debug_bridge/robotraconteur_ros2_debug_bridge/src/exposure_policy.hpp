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

// Deny-by-default exposure rules (CLAUDE.md S5): nothing is reachable over
// Robot Raconteur unless a rule allows it, and a deny rule always wins.
//
// Patterns match fully qualified ROS names: "*" matches any characters
// within one name segment (not "/"), "**" any characters including "/", "?"
// one character other than "/". A pattern starts with "/" or "**".

#ifndef EXPOSURE_POLICY_HPP_
#define EXPOSURE_POLICY_HPP_

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace robotraconteur_ros2_debug_bridge
{

class NamePattern
{
public:
  /// Throws std::invalid_argument for an invalid pattern.
  explicit NamePattern(const std::string & pattern);
  bool matches(const std::string & name) const;
  const std::string & text() const {return text_;}

private:
  std::string text_;
};

struct TopicRules
{
  std::vector<NamePattern> allow;    // visible, subscribable
  std::vector<NamePattern> publish;  // publishable (with the publish capability)
  std::vector<NamePattern> deny;     // overrides both
};

/// Visibility of nodes, services and actions (calling services and sending
/// goals also need their capabilities; B6, B8).
struct Rules
{
  std::vector<NamePattern> allow;
  std::vector<NamePattern> deny;
};

struct ServiceRules
{
  std::vector<NamePattern> allow;  // visible
  std::vector<NamePattern> call;   // callable (with the call_service capability)
  std::vector<NamePattern> serve;  // servable by a client (with serve_service)
  std::vector<NamePattern> deny;   // overrides all
};

struct ActionRules
{
  std::vector<NamePattern> allow;  // visible
  std::vector<NamePattern> send;   // goals (with the action capability)
  std::vector<NamePattern> deny;
};

/// Parameters, by fully qualified node name.
struct ParameterRules
{
  std::vector<NamePattern> allow;  // list, describe, get, events
  std::vector<NamePattern> set;    // set (with the set_parameter capability)
  std::vector<NamePattern> deny;
};

struct ExposureConfig
{
  Rules nodes;
  TopicRules topics;
  ServiceRules services;
  ActionRules actions;
  ParameterRules parameters;
  /// Component containers (fully qualified node names) whose components can
  /// be listed, loaded and unloaded (with the load_component capability).
  Rules components;
};

class ExposurePolicy
{
public:
  ExposurePolicy(ExposureConfig config, std::set<std::string> capabilities);

  /// Shown in the graph and subscribable (also needs the read capability).
  bool topic_visible(const std::string & topic) const;
  bool topic_subscribable(const std::string & topic) const;
  /// Needs a publish rule and the publish capability.
  bool topic_publishable(const std::string & topic) const;

  /// Graph visibility (all need the read capability).
  bool node_visible(const std::string & node) const;
  bool service_visible(const std::string & service) const;
  /// A call rule and the call_service capability.
  bool service_callable(const std::string & service) const;
  /// A serve rule and the serve_service capability.
  bool service_servable(const std::string & service) const;
  bool action_visible(const std::string & action) const;
  /// A send rule and the action capability.
  bool action_sendable(const std::string & action) const;
  /// A node's parameters: readable (read capability, or settable), settable
  /// (a set rule and the set_parameter capability).
  bool parameters_readable(const std::string & node) const;
  bool parameters_settable(const std::string & node) const;
  /// A component rule and the load_component capability.
  bool container_usable(const std::string & container) const;

  bool has_capability(const std::string & name) const {return capabilities_.count(name) != 0;}

private:
  ExposureConfig config_;
  std::set<std::string> capabilities_;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // EXPOSURE_POLICY_HPP_
