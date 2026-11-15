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

// The ROS graph as the exposure rules allow it to be seen (B5).
//
// Actions are listed once (from their "<action>/_action/feedback" topic);
// their internal topics and services ("/_action/") are not.

#ifndef GRAPH_OBJECT_HPP_
#define GRAPH_OBJECT_HPP_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "exposure_policy.hpp"
#include "rclcpp/rclcpp.hpp"

namespace robotraconteur_ros2_debug_bridge
{

struct GraphEntity
{
  std::string name;
  std::vector<std::string> types;
  int32_t publisher_count = 0;     // topics
  int32_t subscription_count = 0;  // topics

  bool operator==(const GraphEntity & o) const
  {
    return name == o.name && types == o.types && publisher_count == o.publisher_count &&
           subscription_count == o.subscription_count;
  }
};

struct GraphData
{
  uint64_t version = 0;
  std::vector<std::string> nodes;
  std::vector<GraphEntity> topics;
  std::vector<GraphEntity> services;
  std::vector<GraphEntity> actions;

  /// Same content (ignores the version).
  bool same(const GraphData & o) const
  {
    return nodes == o.nodes && topics == o.topics && services == o.services &&
           actions == o.actions;
  }
};

struct EndpointData
{
  std::string node_name;       // empty if the node is hidden
  std::string node_namespace;
  std::string topic_type;
  std::string endpoint_type;   // "publisher" or "subscription"
  std::string reliability;
  std::string durability;
  std::string history;
  uint64_t depth = 0;
  double deadline = 0;         // seconds; 0: default (infinite)
  double lifespan = 0;
  std::string liveliness;
  double liveliness_lease_duration = 0;
};

/// Thread-safe.
class GraphMonitor
{
public:
  GraphMonitor(rclcpp::Node & node, std::shared_ptr<const ExposurePolicy> policy);

  /// The last polled view (polls once if never polled).
  GraphData current();
  /// Takes a new view; true if it differs from the last (version + 1).
  bool poll();
  /// Endpoints of a visible topic (relative names are expanded). Throws
  /// std::invalid_argument ("not available") otherwise.
  std::vector<EndpointData> endpoints(const std::string & topic);

private:
  GraphData take() const;

  rclcpp::Node & node_;
  std::shared_ptr<const ExposurePolicy> policy_;
  std::mutex mutex_;
  bool polled_ = false;
  GraphData last_;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // GRAPH_OBJECT_HPP_
