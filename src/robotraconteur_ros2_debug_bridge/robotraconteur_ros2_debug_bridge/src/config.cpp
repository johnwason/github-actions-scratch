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

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "RobotRaconteur/NodeSetup.h"
#include "yaml-cpp/yaml.h"

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

class Parser
{
public:
  explicit Parser(std::string source)
  : source_(std::move(source)) {}

  BridgeConfig parse(const std::string & text) const
  {
    YAML::Node root;
    try {
      root = YAML::Load(text);
    } catch (const YAML::Exception & e) {
      throw ConfigError(source_ + ":" + std::to_string(e.mark.line + 1) + ": " + e.msg);
    }
    BridgeConfig c;
    if (root.IsNull()) {
      return c;  // an empty file means the defaults
    }
    if (!root.IsMap()) {
      fail(root, "", "the file must be a mapping");
    }
    check_keys(root, "", {"robotraconteur", "capabilities", "exposure", "fast_path"});
    if (root["fast_path"]) {
      c.fast_path = boolean(root["fast_path"], "fast_path");
    }
    if (const YAML::Node rr = root["robotraconteur"]) {
      map(rr, "robotraconteur");
      check_keys(
        rr, "robotraconteur", {"node_name", "service_name", "transports", "node_announce",
          "node_discovery_listening", "authentication"});
      if (rr["node_name"]) {
        c.node_name = scalar(rr["node_name"], "robotraconteur.node_name");
      }
      if (rr["service_name"]) {
        c.service_name = scalar(rr["service_name"], "robotraconteur.service_name");
      }
      if (const YAML::Node t = rr["transports"]) {
        const std::string k = "robotraconteur.transports";
        map(t, k);
        check_keys(t, k, {"intra", "local", "tcp"});
        if (t["intra"]) {
          c.intra_transport = boolean(t["intra"], k + ".intra");
        }
        if (t["local"]) {
          c.local_transport = boolean(t["local"], k + ".local");
        }
        if (const YAML::Node tcp = t["tcp"]) {
          map(tcp, k + ".tcp");
          check_keys(tcp, k + ".tcp", {"enabled", "port", "localhost_only", "tls"});
          if (tcp["enabled"]) {
            c.tcp.enabled = boolean(tcp["enabled"], k + ".tcp.enabled");
          }
          if (tcp["port"]) {
            int port = -1;
            if (!tcp["port"].IsScalar() || !YAML::convert<int>::decode(tcp["port"], port) ||
              port < 1 || port > 65535)
            {
              fail(tcp["port"], k + ".tcp.port", "expected a port number 1-65535");
            }
            c.tcp.port = static_cast<uint16_t>(port);
          }
          if (tcp["localhost_only"]) {
            c.tcp.localhost_only = boolean(tcp["localhost_only"], k + ".tcp.localhost_only");
          }
          if (const YAML::Node tls = tcp["tls"]) {
            const std::string kt = k + ".tcp.tls";
            map(tls, kt);
            check_keys(tls, kt, {"enabled", "require"});
            if (tls["enabled"]) {
              c.tcp.tls = boolean(tls["enabled"], kt + ".enabled");
            }
            if (tls["require"]) {
              c.tcp.require_tls = boolean(tls["require"], kt + ".require");
            }
          }
        }
      }
      if (const YAML::Node a = rr["authentication"]) {
        const std::string ka = "robotraconteur.authentication";
        map(a, ka);
        check_keys(a, ka, {"enabled", "password_file"});
        if (a["enabled"]) {
          c.authentication.enabled = boolean(a["enabled"], ka + ".enabled");
        }
        if (a["password_file"]) {
          c.authentication.password_file = scalar(a["password_file"], ka + ".password_file");
        }
        if (c.authentication.enabled && c.authentication.password_file.empty()) {
          fail(a, ka + ".password_file", "needed when authentication is enabled");
        }
      }
      if (rr["node_announce"]) {
        c.node_announce = boolean(rr["node_announce"], "robotraconteur.node_announce");
      }
      if (rr["node_discovery_listening"]) {
        c.node_discovery_listening =
          boolean(rr["node_discovery_listening"], "robotraconteur.node_discovery_listening");
      }
    }
    if (const YAML::Node caps = root["capabilities"]) {
      map(caps, "capabilities");
      const auto & names = capability_names();
      check_keys(caps, "capabilities", std::set<std::string>(names.begin(), names.end()));
      for (const auto & kv : caps) {
        const std::string name = kv.first.Scalar();
        if (boolean(kv.second, "capabilities." + name)) {
          c.capabilities.insert(name);
        } else {
          c.capabilities.erase(name);
        }
      }
    }
    if (const YAML::Node ex = root["exposure"]) {
      map(ex, "exposure");
      check_keys(
        ex, "exposure", {"nodes", "topics", "services", "actions", "parameters", "components"});
      if (const YAML::Node a = ex["actions"]) {
        const std::string k = "exposure.actions";
        map(a, k);
        check_keys(a, k, {"allow", "send", "deny"});
        c.exposure.actions.allow = patterns(a["allow"], k + ".allow");
        c.exposure.actions.send = patterns(a["send"], k + ".send");
        c.exposure.actions.deny = patterns(a["deny"], k + ".deny");
      }
      if (const YAML::Node p = ex["parameters"]) {
        const std::string k = "exposure.parameters";
        map(p, k);
        check_keys(p, k, {"allow", "set", "deny"});
        c.exposure.parameters.allow = patterns(p["allow"], k + ".allow");
        c.exposure.parameters.set = patterns(p["set"], k + ".set");
        c.exposure.parameters.deny = patterns(p["deny"], k + ".deny");
      }
      if (const YAML::Node s = ex["services"]) {
        const std::string k = "exposure.services";
        map(s, k);
        check_keys(s, k, {"allow", "call", "serve", "deny"});
        c.exposure.services.allow = patterns(s["allow"], k + ".allow");
        c.exposure.services.call = patterns(s["call"], k + ".call");
        c.exposure.services.serve = patterns(s["serve"], k + ".serve");
        c.exposure.services.deny = patterns(s["deny"], k + ".deny");
      }
      const std::pair<const char *, Rules *> simple[] = {
        {"nodes", &c.exposure.nodes}, {"components", &c.exposure.components}};
      for (const auto & s : simple) {
        if (const YAML::Node n = ex[s.first]) {
          const std::string k = std::string("exposure.") + s.first;
          map(n, k);
          check_keys(n, k, {"allow", "deny"});
          s.second->allow = patterns(n["allow"], k + ".allow");
          s.second->deny = patterns(n["deny"], k + ".deny");
        }
      }
      if (const YAML::Node t = ex["topics"]) {
        map(t, "exposure.topics");
        check_keys(t, "exposure.topics", {"allow", "publish", "deny"});
        c.exposure.topics.allow = patterns(t["allow"], "exposure.topics.allow");
        c.exposure.topics.publish = patterns(t["publish"], "exposure.topics.publish");
        c.exposure.topics.deny = patterns(t["deny"], "exposure.topics.deny");
      }
    }
    if (c.node_name.empty()) {
      fail(root, "robotraconteur.node_name", "must not be empty");
    }
    if (c.service_name.empty()) {
      fail(root, "robotraconteur.service_name", "must not be empty");
    }
    if (!c.intra_transport && !c.local_transport && !c.tcp.enabled) {
      fail(root, "robotraconteur.transports", "every transport is disabled");
    }
    if ((c.tcp.tls || c.tcp.require_tls) && !c.tcp.enabled) {
      fail(root, "robotraconteur.transports.tcp.tls", "TLS needs the TCP transport");
    }
    if (c.tcp.require_tls && !c.tcp.tls) {
      fail(root, "robotraconteur.transports.tcp.tls.require", "needs tls.enabled");
    }
    if ((c.node_announce || c.node_discovery_listening) && !c.tcp.enabled) {
      fail(
        root, "robotraconteur.node_announce",
        "announce and discovery listening need the TCP transport");
    }
    return c;
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

  void map(const YAML::Node & node, const std::string & key) const
  {
    if (!node.IsMap()) {
      fail(node, key, "expected a mapping");
    }
  }

  void check_keys(
    const YAML::Node & node, const std::string & where, const std::set<std::string> & allowed) const
  {
    for (const auto & kv : node) {
      const std::string key = kv.first.Scalar();
      if (allowed.count(key) == 0) {
        std::string list;
        for (const auto & a : allowed) {
          list += (list.empty() ? "" : ", ") + a;
        }
        fail(kv.first, where.empty() ? key : where + "." + key,
          "unknown key (allowed: " + list + ")");
      }
    }
  }

  std::string scalar(const YAML::Node & node, const std::string & key) const
  {
    if (!node.IsScalar()) {
      fail(node, key, "expected a single value");
    }
    return node.Scalar();
  }

  // A list of name patterns; absent means none.
  std::vector<NamePattern> patterns(const YAML::Node & node, const std::string & key) const
  {
    std::vector<NamePattern> out;
    if (!node || node.IsNull()) {
      return out;
    }
    if (!node.IsSequence()) {
      fail(node, key, "expected a list of name patterns");
    }
    for (const auto & item : node) {
      try {
        out.emplace_back(scalar(item, key));
      } catch (const std::invalid_argument & e) {
        fail(item, key, e.what());
      }
    }
    return out;
  }

  bool boolean(const YAML::Node & node, const std::string & key) const
  {
    bool value = false;
    if (!node.IsScalar() || !YAML::convert<bool>::decode(node, value)) {
      fail(node, key, "expected true or false");
    }
    return value;
  }

  std::string source_;
};

}  // namespace

const std::vector<std::string> & capability_names()
{
  static const std::vector<std::string> names = {
    "read", "publish", "call_service", "serve_service", "action", "set_parameter",
    "load_component"};
  return names;
}

BridgeConfig parse_config(const std::string & yaml_text, const std::string & source)
{
  return Parser(source).parse(yaml_text);
}

BridgeConfig load_config_file(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw ConfigError(path + ": cannot open the file");
  }
  std::ostringstream text;
  text << in.rdbuf();
  BridgeConfig c = parse_config(text.str(), path);
  // Relative to the configuration file.
  auto & pf = c.authentication.password_file;
  if (!pf.empty() && std::filesystem::path(pf).is_relative()) {
    pf = (std::filesystem::path(path).parent_path() / pf).string();
  }
  return c;
}

uint32_t node_setup_flags(const BridgeConfig & config)
{
  namespace RR = RobotRaconteur;
  uint32_t flags = 0;
  if (config.intra_transport) {
    flags |= RR::RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT |
      RR::RobotRaconteurNodeSetupFlags_INTRA_TRANSPORT_START_SERVER;
  }
  if (config.local_transport) {
    // Not LOCAL_TRANSPORT_SERVER_PUBLIC: the socket stays private to the user.
    flags |= RR::RobotRaconteurNodeSetupFlags_ENABLE_LOCAL_TRANSPORT |
      RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_START_SERVER;
  }
  if (config.tcp.enabled) {
    flags |= RR::RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT |
      RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER;
    if (config.tcp.localhost_only) {
      flags |= RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST;
    }
  }
  if (config.node_announce) {
    flags |= RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE;
  }
  if (config.node_discovery_listening) {
    flags |= RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_DISCOVERY_LISTENING;
  }
  return flags;
}

}  // namespace robotraconteur_ros2_debug_bridge
