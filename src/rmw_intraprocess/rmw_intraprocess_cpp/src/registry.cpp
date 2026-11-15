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

#include "registry.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "identifier.hpp"
#include "qos.hpp"

namespace rmw_intraprocess_cpp
{

namespace
{

/// First policy that makes check_qos_compatible report an error.
rmw_qos_policy_kind_t offending_policy(const rmw_qos_profile_t & pub, const rmw_qos_profile_t & sub)
{
  rmw_qos_compatibility_type_t c = RMW_QOS_COMPATIBILITY_OK;
  auto incompatible = [&c](const rmw_qos_profile_t & p, const rmw_qos_profile_t & s) {
      return check_qos_compatible(p, s, &c, nullptr, 0u) == RMW_RET_OK &&
             c == RMW_QOS_COMPATIBILITY_ERROR;
    };
  // Test one policy at a time against an otherwise-compatible copy.
  rmw_qos_profile_t p = pub;
  rmw_qos_profile_t s = pub;
  s.reliability = sub.reliability;
  if (incompatible(p, s)) {return RMW_QOS_POLICY_RELIABILITY;}
  s = pub;
  s.durability = sub.durability;
  if (incompatible(p, s)) {return RMW_QOS_POLICY_DURABILITY;}
  s = pub;
  s.deadline = sub.deadline;
  if (incompatible(p, s)) {return RMW_QOS_POLICY_DEADLINE;}
  s = pub;
  s.liveliness = sub.liveliness;
  if (incompatible(p, s)) {return RMW_QOS_POLICY_LIVELINESS;}
  s = pub;
  s.liveliness_lease_duration = sub.liveliness_lease_duration;
  // DDS has one LIVELINESS policy that includes the lease duration, and the DDS
  // RMWs (and rcl's test_events) report a lease mismatch as LIVELINESS.
  if (incompatible(p, s)) {return RMW_QOS_POLICY_LIVELINESS;}
  return RMW_QOS_POLICY_INVALID;
}

}  // namespace

Relation relate(
  const PublisherImpl & publisher, const SubscriptionImpl & subscription,
  rmw_qos_policy_kind_t * policy)
{
  if (publisher.type.type_name != subscription.type.type_name) {
    return Relation::kIncompatibleType;
  }
  rmw_qos_compatibility_type_t compat = RMW_QOS_COMPATIBILITY_OK;
  if (check_qos_compatible(publisher.qos, subscription.qos, &compat, nullptr, 0u) !=
    RMW_RET_OK || compat == RMW_QOS_COMPATIBILITY_ERROR)
  {
    if (policy != nullptr) {
      *policy = offending_policy(publisher.qos, subscription.qos);
    }
    return Relation::kIncompatibleQos;
  }
  return Relation::kMatched;
}

bool endpoints_match(const PublisherImpl & publisher, const SubscriptionImpl & subscription)
{
  return relate(publisher, subscription) == Relation::kMatched;
}

Registry & Registry::instance()
{
  // Leaked on purpose: see the header.
  static Registry * registry = new Registry();
  return *registry;
}

void Registry::record_pair_locked(
  PublisherImpl & pub, SubscriptionImpl & sub, int delta,
  bool publisher_survives, bool subscription_survives, EventList & fire)
{
  auto add = [&fire](EndpointImpl & e, rmw_event_type_t type, auto record) {
      const std::shared_ptr<EventState> & ev = e.events[type];
      if (ev && record(*ev)) {
        fire.push_back(ev);
      }
    };
  rmw_qos_policy_kind_t policy = RMW_QOS_POLICY_INVALID;
  switch (relate(pub, sub, &policy)) {
    case Relation::kMatched:
      if (publisher_survives) {
        add(pub, RMW_EVENT_PUBLICATION_MATCHED, [delta](EventState & e) {
            return e.record_matched(delta);
          });
      }
      if (subscription_survives) {
        add(sub, RMW_EVENT_SUBSCRIPTION_MATCHED, [delta](EventState & e) {
            return e.record_matched(delta);
          });
      }
      break;
    case Relation::kIncompatibleQos:
      if (delta > 0) {
        add(pub, RMW_EVENT_OFFERED_QOS_INCOMPATIBLE, [policy](EventState & e) {
            return e.record_incompatible_qos(policy);
          });
        add(sub, RMW_EVENT_REQUESTED_QOS_INCOMPATIBLE, [policy](EventState & e) {
            return e.record_incompatible_qos(policy);
          });
      }
      break;
    case Relation::kIncompatibleType:
      if (delta > 0) {
        add(pub, RMW_EVENT_PUBLISHER_INCOMPATIBLE_TYPE, [](EventState & e) {
            return e.record_incompatible_type();
          });
        add(sub, RMW_EVENT_SUBSCRIPTION_INCOMPATIBLE_TYPE, [](EventState & e) {
            return e.record_incompatible_type();
          });
      }
      break;
  }
}

void Registry::fire(const EventList & events)
{
  for (const auto & e : events) {
    e->fire_callback();
  }
}

void Registry::add_node(NodeImpl * node)
{
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.push_back(node);
  notify_graph_change_locked();
}

void Registry::remove_node(NodeImpl * node)
{
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), node), nodes_.end());
  notify_graph_change_locked();
}

void Registry::add_publisher(PublisherImpl * publisher)
{
  EventList to_fire;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    TopicEntry & entry = topics_[publisher->topic_name];
    entry.publishers.push_back(publisher);
    for (const auto & sub : entry.subscriptions) {
      record_pair_locked(*publisher, *sub, +1, true, true, to_fire);
    }
    notify_graph_change_locked();
  }
  fire(to_fire);
}

void Registry::remove_publisher(PublisherImpl * publisher)
{
  EventList to_fire;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topics_.find(publisher->topic_name);
    if (it != topics_.end()) {
      auto & pubs = it->second.publishers;
      pubs.erase(std::remove(pubs.begin(), pubs.end(), publisher), pubs.end());
      for (const auto & sub : it->second.subscriptions) {
        record_pair_locked(*publisher, *sub, -1, false, true, to_fire);
      }
      if (pubs.empty() && it->second.subscriptions.empty()) {
        topics_.erase(it);
      }
    }
    notify_graph_change_locked();
  }
  fire(to_fire);
}

void Registry::add_subscription(std::shared_ptr<SubscriptionImpl> subscription)
{
  EventList to_fire;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    TopicEntry & entry = topics_[subscription->topic_name];
    std::vector<Sample> history;
    for (PublisherImpl * pub : entry.publishers) {
      record_pair_locked(*pub, *subscription, +1, true, true, to_fire);
      if (subscription->qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL &&
        pub->qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL &&
        endpoints_match(*pub, *subscription) &&
        !(subscription->options.ignore_local_publications &&
        subscription->context == pub->context))
      {
        history.insert(history.end(), pub->history.begin(), pub->history.end());
      }
    }
    // Under the registry lock, so no concurrent publish can deliver a newer
    // sample before the history (see prepare_publish).
    if (!history.empty()) {
      subscription->deliver_history(history);
    }
    entry.subscriptions.push_back(std::move(subscription));
    notify_graph_change_locked();
  }
  fire(to_fire);
}

void Registry::remove_subscription(SubscriptionImpl * subscription)
{
  std::shared_ptr<SubscriptionImpl> last_ref;  // released after the lock
  EventList to_fire;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topics_.find(subscription->topic_name);
    if (it != topics_.end()) {
      auto & subs = it->second.subscriptions;
      auto s = std::find_if(
        subs.begin(), subs.end(),
        [subscription](const std::shared_ptr<SubscriptionImpl> & p) {
          return p.get() == subscription;
        });
      if (s != subs.end()) {
        last_ref = std::move(*s);
        subs.erase(s);
      }
      for (PublisherImpl * pub : it->second.publishers) {
        record_pair_locked(*pub, *subscription, -1, true, false, to_fire);
      }
      if (subs.empty() && it->second.publishers.empty()) {
        topics_.erase(it);
      }
    }
    notify_graph_change_locked();
  }
  fire(to_fire);
}

void Registry::prepare_publish(
  PublisherImpl & publisher, const Sample & sample,
  std::vector<std::shared_ptr<SubscriptionImpl>> & targets)
{
  targets.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  if (publisher.qos.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL) {
    auto & h = publisher.history;
    h.push_back(sample);
    const std::size_t cap = publisher.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST ?
      publisher.qos.depth : keep_all_limit();
    while (h.size() > cap) {
      h.pop_front();
    }
  }
  auto it = topics_.find(publisher.topic_name);
  if (it == topics_.end()) {
    return;
  }
  for (const auto & sub : it->second.subscriptions) {
    if (sub->options.ignore_local_publications && sub->context == publisher.context) {
      continue;
    }
    if (endpoints_match(publisher, *sub)) {
      targets.push_back(sub);
    }
  }
}

namespace
{
/// Removes `item` from map[key] (and the key if empty). Returns the owning
/// reference so it can be released after the registry lock.
template<typename T>
std::shared_ptr<T> remove_owned(
  std::map<std::string, std::vector<std::shared_ptr<T>>, std::less<>> & map,
  const std::string & key, T * item)
{
  std::shared_ptr<T> out;
  auto it = map.find(key);
  if (it == map.end()) {
    return out;
  }
  auto & v = it->second;
  auto found = std::find_if(
    v.begin(), v.end(), [item](const std::shared_ptr<T> & p) {return p.get() == item;});
  if (found != v.end()) {
    out = std::move(*found);
    v.erase(found);
  }
  if (v.empty()) {
    map.erase(it);
  }
  return out;
}
}  // namespace

void Registry::add_service(std::shared_ptr<ServiceImpl> service)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string name = service->service_name;
  services_[name].push_back(std::move(service));
  notify_graph_change_locked();
}

void Registry::remove_service(ServiceImpl * service)
{
  std::shared_ptr<ServiceImpl> last_ref;  // released after the lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_ref = remove_owned(services_, service->service_name, service);
    notify_graph_change_locked();
  }
}

void Registry::add_client(std::shared_ptr<ClientImpl> client)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string name = client->service_name;
  clients_[name].push_back(std::move(client));
  notify_graph_change_locked();
}

void Registry::remove_client(ClientImpl * client)
{
  std::shared_ptr<ClientImpl> last_ref;  // released after the lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_ref = remove_owned(clients_, client->service_name, client);
    notify_graph_change_locked();
  }
}

void Registry::request_targets(
  const ClientImpl & client, std::vector<std::shared_ptr<ServiceImpl>> & out) const
{
  out.clear();
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = services_.find(client.service_name);
  if (it == services_.end()) {
    return;
  }
  for (const auto & s : it->second) {
    if (service_endpoints_match(client, *s)) {
      out.push_back(s);
    }
  }
}

std::shared_ptr<ClientImpl> Registry::find_client(
  const ServiceImpl & service, const std::uint8_t (& writer_guid)[RMW_GID_STORAGE_SIZE]) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = clients_.find(service.service_name);
  if (it == clients_.end()) {
    return nullptr;
  }
  for (const auto & c : it->second) {
    if (std::memcmp(c->gid.data, writer_guid, RMW_GID_STORAGE_SIZE) == 0) {
      return c;
    }
  }
  return nullptr;
}

bool Registry::service_available(const ClientImpl & client) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = services_.find(client.service_name);
  if (it == services_.end()) {
    return false;
  }
  for (const auto & s : it->second) {
    if (service_endpoints_match(client, *s)) {
      return true;
    }
  }
  return false;
}

std::size_t Registry::count_matched_subscriptions(const PublisherImpl & publisher) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = topics_.find(publisher.topic_name);
  if (it == topics_.end()) {
    return 0;
  }
  std::size_t n = 0;
  for (const auto & sub : it->second.subscriptions) {
    n += endpoints_match(publisher, *sub) ? 1u : 0u;
  }
  return n;
}

std::size_t Registry::count_matched_publishers(const SubscriptionImpl & subscription) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = topics_.find(subscription.topic_name);
  if (it == topics_.end()) {
    return 0;
  }
  std::size_t n = 0;
  for (const PublisherImpl * pub : it->second.publishers) {
    n += endpoints_match(*pub, subscription) ? 1u : 0u;
  }
  return n;
}

rmw_gid_t Registry::new_gid()
{
  std::uint64_t id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = next_gid_++;
  }
  rmw_gid_t gid{};
  gid.implementation_identifier = kIdentifier;
  static_assert(RMW_GID_STORAGE_SIZE >= sizeof(id), "gid storage too small");
  std::memcpy(gid.data, &id, sizeof(id));
  return gid;
}

std::size_t Registry::node_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return nodes_.size();
}

std::size_t Registry::topic_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return topics_.size();
}

std::vector<Registry::GraphNode> Registry::graph_nodes() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GraphNode> out;
  out.reserve(nodes_.size());
  for (const NodeImpl * n : nodes_) {
    out.push_back(GraphNode{n->name, n->namespace_, n->enclave});
  }
  return out;
}

bool Registry::graph_names_and_types(
  std::initializer_list<EndpointKind> kinds, const char * node_name,
  const char * node_namespace, NamesAndTypes & out) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto selected = [node_name, node_namespace](const NodeImpl * n) {
      return node_name == nullptr ||
             (n != nullptr && n->name == node_name && n->namespace_ == node_namespace);
    };
  if (node_name != nullptr &&
    std::none_of(nodes_.begin(), nodes_.end(), selected))
  {
    return false;
  }
  for (EndpointKind kind : kinds) {
    switch (kind) {
      case EndpointKind::kPublisher:
        for (const auto & t : topics_) {
          for (const PublisherImpl * p : t.second.publishers) {
            if (selected(p->node)) {
              out[t.first].insert(p->type.type_name);
            }
          }
        }
        break;
      case EndpointKind::kSubscription:
        for (const auto & t : topics_) {
          for (const auto & s : t.second.subscriptions) {
            if (selected(s->node)) {
              out[t.first].insert(s->type.type_name);
            }
          }
        }
        break;
      case EndpointKind::kService:
        for (const auto & e : services_) {
          for (const auto & s : e.second) {
            if (selected(s->node)) {
              out[e.first].insert(s->type.type_name);
            }
          }
        }
        break;
      case EndpointKind::kClient:
        for (const auto & e : clients_) {
          for (const auto & c : e.second) {
            if (selected(c->node)) {
              out[e.first].insert(c->type.type_name);
            }
          }
        }
        break;
    }
  }
  return true;
}

std::vector<Registry::GraphTopicEndpoint> Registry::graph_topic_endpoints(
  bool publishers, const char * topic) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<GraphTopicEndpoint> out;
  auto it = topics_.find(topic);
  if (it == topics_.end()) {
    return out;
  }
  auto add = [&out](const EndpointImpl & e) {
      out.push_back(
        GraphTopicEndpoint{
        e.node->name, e.node->namespace_, e.type.type_name, e.type.type_hash, e.gid, e.qos});
    };
  if (publishers) {
    for (const PublisherImpl * p : it->second.publishers) {
      add(*p);
    }
  } else {
    for (const auto & s : it->second.subscriptions) {
      add(*s);
    }
  }
  return out;
}

std::size_t Registry::count_endpoints(EndpointKind kind, const char * name) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  switch (kind) {
    case EndpointKind::kPublisher:
    case EndpointKind::kSubscription: {
        auto it = topics_.find(name);
        if (it == topics_.end()) {
          return 0;
        }
        return kind == EndpointKind::kPublisher ?
               it->second.publishers.size() : it->second.subscriptions.size();
      }
    case EndpointKind::kService: {
        auto it = services_.find(name);
        return it == services_.end() ? 0 : it->second.size();
      }
    case EndpointKind::kClient: {
        auto it = clients_.find(name);
        return it == clients_.end() ? 0 : it->second.size();
      }
  }
  return 0;
}

void Registry::notify_graph_change_locked()
{
  for (NodeImpl * n : nodes_) {
    n->graph_guard_condition.trigger();
  }
}

}  // namespace rmw_intraprocess_cpp
