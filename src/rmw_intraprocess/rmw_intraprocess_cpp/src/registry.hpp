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

#ifndef REGISTRY_HPP_
#define REGISTRY_HPP_

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "rmw/types.h"
#include "rosidl_runtime_c/type_hash.h"

#include "endpoint.hpp"
#include "event.hpp"
#include "node.hpp"
#include "service.hpp"

namespace rmw_intraprocess_cpp
{

/// Endpoints on one topic name. Endpoints may have different types; only
/// endpoints with equal types match.
struct TopicEntry
{
  std::vector<PublisherImpl *> publishers;
  std::vector<std::shared_ptr<SubscriptionImpl>> subscriptions;
};

/// How a publisher and a subscription on the same topic relate.
enum class Relation { kMatched, kIncompatibleQos, kIncompatibleType };

/// Relation of a publisher and a subscription on the same topic. For
/// kIncompatibleQos, *policy (if non-null) is the first offending policy.
Relation relate(
  const PublisherImpl & publisher, const SubscriptionImpl & subscription,
  rmw_qos_policy_kind_t * policy = nullptr);

/// True if they match (same type, QoS compatible).
bool endpoints_match(const PublisherImpl & publisher, const SubscriptionImpl & subscription);

/// Process-wide entity registry. There is a single domain: every context in the
/// process shares this registry, and domain ids are ignored.
///
/// Locking order (docs/design.md): registry lock -> topic/service entry lock ->
/// endpoint queue / event lock -> waiter lock. Event callbacks are called after
/// the registry lock is released.
class Registry
{
public:
  /// The registry is created on first use and intentionally never destroyed,
  /// so there is no static destruction order dependency across DLLs.
  static Registry & instance();

  void add_node(NodeImpl * node);
  void remove_node(NodeImpl * node);

  void add_publisher(PublisherImpl * publisher);
  void remove_publisher(PublisherImpl * publisher);
  /// Takes shared ownership of the subscription and delivers TRANSIENT_LOCAL
  /// history from matched publishers.
  void add_subscription(std::shared_ptr<SubscriptionImpl> subscription);
  /// Drops the registry's ownership; in-flight deliveries may keep it alive.
  void remove_subscription(SubscriptionImpl * subscription);

  /// Atomically (w.r.t. add_subscription) records `sample` in the publisher's
  /// TRANSIENT_LOCAL history and collects the subscriptions to deliver it to:
  /// matched, and not ignoring local publications from the publisher's context.
  void prepare_publish(
    PublisherImpl & publisher, const Sample & sample,
    std::vector<std::shared_ptr<SubscriptionImpl>> & targets);

  void add_service(std::shared_ptr<ServiceImpl> service);
  void remove_service(ServiceImpl * service);
  void add_client(std::shared_ptr<ClientImpl> client);
  void remove_client(ClientImpl * client);
  /// Services a request from `client` goes to (same name and type).
  void request_targets(
    const ClientImpl & client, std::vector<std::shared_ptr<ServiceImpl>> & out) const;
  /// The client a response from `service` goes to, or null if it is gone.
  std::shared_ptr<ClientImpl> find_client(
    const ServiceImpl & service, const std::uint8_t (& writer_guid)[RMW_GID_STORAGE_SIZE]) const;
  bool service_available(const ClientImpl & client) const;

  std::size_t count_matched_subscriptions(const PublisherImpl & publisher) const;
  std::size_t count_matched_publishers(const SubscriptionImpl & subscription) const;

  /// A GID unique within the process for its lifetime.
  rmw_gid_t new_gid();

  /// Counts, for tests and invariants.
  std::size_t node_count() const;
  std::size_t topic_count() const;

  // --- Graph queries (M5). Snapshots taken under the registry lock. ---

  /// Graph endpoint kinds.
  enum class EndpointKind { kPublisher, kSubscription, kService, kClient };

  /// One node in the graph.
  struct GraphNode
  {
    std::string name;
    std::string namespace_;
    std::string enclave;
  };

  /// One publisher or subscription, for rmw_get_*_info_by_topic.
  struct GraphTopicEndpoint
  {
    std::string node_name;
    std::string node_namespace;
    std::string type_name;
    rosidl_type_hash_t type_hash;
    rmw_gid_t gid;
    rmw_qos_profile_t qos;
  };

  /// Names (topic or service) to their sorted, unique types.
  using NamesAndTypes = std::map<std::string, std::set<std::string>>;

  std::vector<GraphNode> graph_nodes() const;

  /// Collects the names and types of the endpoints of the given kinds. If
  /// node_name is non-null, only endpoints of the node(s) with that name and
  /// namespace; returns false (and collects nothing) if there is no such node.
  bool graph_names_and_types(
    std::initializer_list<EndpointKind> kinds, const char * node_name,
    const char * node_namespace, NamesAndTypes & out) const;

  /// Publishers (or subscriptions) on a topic name, any type.
  std::vector<GraphTopicEndpoint> graph_topic_endpoints(bool publishers, const char * topic) const;

  /// Number of endpoints of `kind` with the given topic/service name, any type.
  /// Does not allocate (the upstream tests check this).
  std::size_t count_endpoints(EndpointKind kind, const char * name) const;

private:
  using EventList = std::vector<std::shared_ptr<EventState>>;

  Registry() = default;

  /// Records the events caused by `publisher` and `subscription` appearing
  /// (delta +1) or the publisher/subscription disappearing (delta -1, only the
  /// survivor's matched count changes). Caller holds mutex_.
  static void record_pair_locked(
    PublisherImpl & publisher, SubscriptionImpl & subscription, int delta,
    bool publisher_survives, bool subscription_survives, EventList & fire);
  static void fire(const EventList & events);

  /// Triggers the graph guard condition of every node. Caller holds mutex_.
  void notify_graph_change_locked();

  mutable std::mutex mutex_;
  std::vector<NodeImpl *> nodes_;
  // Transparent comparators: lookups by const char * do not allocate.
  std::map<std::string, TopicEntry, std::less<>> topics_;
  std::map<std::string, std::vector<std::shared_ptr<ServiceImpl>>, std::less<>> services_;
  std::map<std::string, std::vector<std::shared_ptr<ClientImpl>>, std::less<>> clients_;
  std::uint64_t next_gid_ = 1;
};

}  // namespace rmw_intraprocess_cpp

#endif  // REGISTRY_HPP_
