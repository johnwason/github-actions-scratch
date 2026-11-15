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

#include "graph_object.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/expand_topic_or_service_name.hpp"
#include "rmw/qos_string_conversions.h"
#include "rmw/time.h"

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

const char kActionInfix[] = "/_action/";
const char kFeedbackSuffix[] = "/_action/feedback";
const char kFeedbackTypeSuffix[] = "_FeedbackMessage";

bool ends_with(const std::string & s, const std::string & suffix)
{
  if (s.size() < suffix.size()) {
    return false;
  }
  return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string str_or(const char * s, const char * fallback)
{
  return s != nullptr ? std::string(s) : std::string(fallback);
}

double seconds(const rmw_time_t & t)
{
  if (rmw_time_equal(t, RMW_DURATION_INFINITE) || rmw_time_equal(t, RMW_DURATION_UNSPECIFIED)) {
    return 0.0;
  }
  return static_cast<double>(t.sec) + static_cast<double>(t.nsec) * 1e-9;
}

}  // namespace

GraphMonitor::GraphMonitor(rclcpp::Node & node, std::shared_ptr<const ExposurePolicy> policy)
: node_(node), policy_(std::move(policy)) {}

GraphData GraphMonitor::take() const
{
  GraphData g;
  for (const auto & n : node_.get_node_names()) {
    if (policy_->node_visible(n)) {
      g.nodes.push_back(n);
    }
  }
  std::sort(g.nodes.begin(), g.nodes.end());
  g.nodes.erase(std::unique(g.nodes.begin(), g.nodes.end()), g.nodes.end());

  for (const auto & kv : node_.get_topic_names_and_types()) {
    const std::string & name = kv.first;
    if (ends_with(name, kFeedbackSuffix)) {
      // "<action>/_action/feedback", type "pkg/action/X_FeedbackMessage"
      const std::string action = name.substr(0, name.size() - (sizeof(kFeedbackSuffix) - 1));
      if (!policy_->action_visible(action)) {
        continue;
      }
      GraphEntity a;
      a.name = action;
      for (const auto & t : kv.second) {
        if (ends_with(t, kFeedbackTypeSuffix)) {
          a.types.push_back(t.substr(0, t.size() - (sizeof(kFeedbackTypeSuffix) - 1)));
        }
      }
      g.actions.push_back(std::move(a));
      continue;
    }
    if (name.find(kActionInfix) != std::string::npos || !policy_->topic_visible(name)) {
      continue;
    }
    GraphEntity t;
    t.name = name;
    t.types = kv.second;
    t.publisher_count = static_cast<int32_t>(node_.count_publishers(name));
    t.subscription_count = static_cast<int32_t>(node_.count_subscribers(name));
    g.topics.push_back(std::move(t));
  }
  for (const auto & kv : node_.get_service_names_and_types()) {
    if (kv.first.find(kActionInfix) != std::string::npos || !policy_->service_visible(kv.first)) {
      continue;
    }
    GraphEntity s;
    s.name = kv.first;
    s.types = kv.second;
    g.services.push_back(std::move(s));
  }
  auto by_name = [](const GraphEntity & a, const GraphEntity & b) {return a.name < b.name;};
  std::sort(g.topics.begin(), g.topics.end(), by_name);
  std::sort(g.services.begin(), g.services.end(), by_name);
  std::sort(g.actions.begin(), g.actions.end(), by_name);
  return g;
}

GraphData GraphMonitor::current()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (polled_) {
      return last_;
    }
  }
  poll();
  std::lock_guard<std::mutex> lock(mutex_);
  return last_;
}

bool GraphMonitor::poll()
{
  GraphData g = take();  // outside the lock: queries the graph
  std::lock_guard<std::mutex> lock(mutex_);
  if (polled_ && g.same(last_)) {
    return false;
  }
  g.version = last_.version + 1;
  last_ = std::move(g);
  polled_ = true;
  return true;
}

std::vector<EndpointData> GraphMonitor::endpoints(const std::string & topic)
{
  std::string fq;
  try {
    fq = rclcpp::expand_topic_or_service_name(
      topic, node_.get_name(), node_.get_namespace(), false);
  } catch (const std::exception &) {
    throw std::invalid_argument("Invalid topic name '" + topic + "'");
  }
  if (!policy_->topic_visible(fq) || fq.find(kActionInfix) != std::string::npos) {
    throw std::invalid_argument("Topic '" + fq + "' is not available");
  }
  std::vector<EndpointData> out;
  auto add = [&](const std::vector<rclcpp::TopicEndpointInfo> & infos) {
      for (const auto & i : infos) {
        EndpointData e;
        const std::string ns = i.node_namespace();
        const std::string full = (ns == "/" ? "" : ns) + "/" + i.node_name();
        if (policy_->node_visible(full)) {
          e.node_name = i.node_name();
          e.node_namespace = ns;
        }
        e.topic_type = i.topic_type();
        e.endpoint_type = i.endpoint_type() == rclcpp::EndpointType::Publisher ?
          "publisher" : "subscription";
        const rmw_qos_profile_t q = i.qos_profile().get_rmw_qos_profile();
        e.reliability = str_or(rmw_qos_reliability_policy_to_str(q.reliability), "unknown");
        e.durability = str_or(rmw_qos_durability_policy_to_str(q.durability), "unknown");
        e.history = str_or(rmw_qos_history_policy_to_str(q.history), "unknown");
        e.depth = q.depth;
        e.deadline = seconds(q.deadline);
        e.lifespan = seconds(q.lifespan);
        e.liveliness = str_or(rmw_qos_liveliness_policy_to_str(q.liveliness), "unknown");
        e.liveliness_lease_duration = seconds(q.liveliness_lease_duration);
        out.push_back(std::move(e));
      }
    };
  add(node_.get_publishers_info_by_topic(fq));
  add(node_.get_subscriptions_info_by_topic(fq));
  return out;
}

}  // namespace robotraconteur_ros2_debug_bridge
