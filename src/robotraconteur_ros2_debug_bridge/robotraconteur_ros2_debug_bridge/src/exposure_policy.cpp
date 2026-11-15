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

#include "exposure_policy.hpp"

#include <utility>

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

bool valid_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_' || c == '/' || c == '~' || c == '*' || c == '?';
}

// Glob match; p and n are positions in pattern and name.
bool match(const std::string & pat, size_t p, const std::string & name, size_t n)
{
  while (p < pat.size()) {
    if (pat[p] == '*') {
      const bool deep = p + 1 < pat.size() && pat[p + 1] == '*';
      const size_t next = p + (deep ? 2 : 1);
      for (size_t k = n; ; ++k) {
        if (match(pat, next, name, k)) {
          return true;
        }
        if (k >= name.size() || (!deep && name[k] == '/')) {
          return false;
        }
      }
    }
    if (n >= name.size()) {
      return false;
    }
    if (pat[p] == '?') {
      if (name[n] == '/') {
        return false;
      }
    } else if (pat[p] != name[n]) {
      return false;
    }
    ++p;
    ++n;
  }
  return n == name.size();
}

bool any(const std::vector<NamePattern> & patterns, const std::string & name)
{
  for (const auto & p : patterns) {
    if (p.matches(name)) {
      return true;
    }
  }
  return false;
}

}  // namespace

NamePattern::NamePattern(const std::string & pattern)
: text_(pattern)
{
  if (pattern.empty() || !(pattern[0] == '/' || pattern.rfind("**", 0) == 0)) {
    throw std::invalid_argument("pattern '" + pattern + "' must start with '/' or '**'");
  }
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (!valid_char(pattern[i])) {
      throw std::invalid_argument(
              "pattern '" + pattern + "' has an invalid character '" + pattern[i] + "'");
    }
    if (pattern.compare(i, 3, "***") == 0) {
      throw std::invalid_argument("pattern '" + pattern + "': '***' is not allowed");
    }
  }
}

bool NamePattern::matches(const std::string & name) const
{
  return match(text_, 0, name, 0);
}

ExposurePolicy::ExposurePolicy(ExposureConfig config, std::set<std::string> capabilities)
: config_(std::move(config)), capabilities_(std::move(capabilities))
{
}

bool ExposurePolicy::topic_visible(const std::string & topic) const
{
  if (any(config_.topics.deny, topic)) {
    return false;
  }
  return (has_capability("read") && any(config_.topics.allow, topic)) ||
         topic_publishable(topic);
}

bool ExposurePolicy::topic_subscribable(const std::string & topic) const
{
  return has_capability("read") && !any(config_.topics.deny, topic) &&
         any(config_.topics.allow, topic);
}

namespace
{

bool visible(const Rules & r, bool read, const std::string & name)
{
  return read && !any(r.deny, name) && any(r.allow, name);
}

}  // namespace

bool ExposurePolicy::node_visible(const std::string & node) const
{
  return visible(config_.nodes, has_capability("read"), node);
}

bool ExposurePolicy::service_visible(const std::string & service) const
{
  const auto & s = config_.services;
  if (any(s.deny, service)) {
    return false;
  }
  return (has_capability("read") && any(s.allow, service)) || service_callable(service) ||
         service_servable(service);
}

bool ExposurePolicy::service_callable(const std::string & service) const
{
  return has_capability("call_service") && !any(config_.services.deny, service) &&
         any(config_.services.call, service);
}

bool ExposurePolicy::service_servable(const std::string & service) const
{
  return has_capability("serve_service") && !any(config_.services.deny, service) &&
         any(config_.services.serve, service);
}

bool ExposurePolicy::action_visible(const std::string & action) const
{
  const auto & a = config_.actions;
  if (any(a.deny, action)) {
    return false;
  }
  return (has_capability("read") && any(a.allow, action)) || action_sendable(action);
}

bool ExposurePolicy::action_sendable(const std::string & action) const
{
  return has_capability("action") && !any(config_.actions.deny, action) &&
         any(config_.actions.send, action);
}

bool ExposurePolicy::parameters_readable(const std::string & node) const
{
  const auto & r = config_.parameters;
  if (any(r.deny, node)) {
    return false;
  }
  return (has_capability("read") && any(r.allow, node)) || parameters_settable(node);
}

bool ExposurePolicy::parameters_settable(const std::string & node) const
{
  const auto & r = config_.parameters;
  return has_capability("set_parameter") && !any(r.deny, node) && any(r.set, node);
}

bool ExposurePolicy::container_usable(const std::string & container) const
{
  return visible(config_.components, has_capability("load_component"), container);
}

bool ExposurePolicy::topic_publishable(const std::string & topic) const
{
  return has_capability("publish") && !any(config_.topics.deny, topic) &&
         any(config_.topics.publish, topic);
}

}  // namespace robotraconteur_ros2_debug_bridge
