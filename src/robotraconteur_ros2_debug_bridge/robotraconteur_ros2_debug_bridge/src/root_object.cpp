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

#include "root_object.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "graph_object.hpp"
#include "topic_proxy.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

RootObject::RootObject(
  BridgeIdentity identity, std::shared_ptr<ProxyManager> proxies,
  std::shared_ptr<GraphMonitor> graph)
: identity_(std::move(identity)), proxies_(std::move(proxies)), graph_(std::move(graph)) {}

namespace
{

namespace gen = experimental::ros2_debug_bridge;

RR::RRListPtr<RR::RRArray<char>> string_list(const std::vector<std::string> & v)
{
  auto list = RR::AllocateEmptyRRList<RR::RRArray<char>>();
  for (const auto & s : v) {
    list->push_back(RR::stringToRRArray(s));
  }
  return list;
}

RootObject::GraphPtr to_rr(const GraphData & d)
{
  RootObject::GraphPtr g(new gen::Graph());
  g->version = d.version;
  g->nodes = string_list(d.nodes);
  g->topics = RR::AllocateEmptyRRList<gen::GraphTopic>();
  for (const auto & t : d.topics) {
    RR_INTRUSIVE_PTR<gen::GraphTopic> x(new gen::GraphTopic());
    x->name = t.name;
    x->types = string_list(t.types);
    x->publisher_count = t.publisher_count;
    x->subscription_count = t.subscription_count;
    g->topics->push_back(x);
  }
  g->services = RR::AllocateEmptyRRList<gen::GraphService>();
  for (const auto & s : d.services) {
    RR_INTRUSIVE_PTR<gen::GraphService> x(new gen::GraphService());
    x->name = s.name;
    x->types = string_list(s.types);
    g->services->push_back(x);
  }
  g->actions = RR::AllocateEmptyRRList<gen::GraphAction>();
  for (const auto & a : d.actions) {
    RR_INTRUSIVE_PTR<gen::GraphAction> x(new gen::GraphAction());
    x->name = a.name;
    x->types = string_list(a.types);
    g->actions->push_back(x);
  }
  return g;
}

}  // namespace

RootObject::GraphPtr RootObject::get_graph()
{
  // A fresh view (the timer's can be up to a poll period old).
  if (graph_->poll()) {
    graph_changed();
  }
  return to_rr(graph_->current());
}

RR_SHARED_PTR<RR::Wire<RootObject::GraphPtr>> RootObject::get_graph_changes()
{
  std::lock_guard<std::mutex> lock(wire_mutex_);
  return graph_wire_;
}

void RootObject::set_graph_changes(const RR_SHARED_PTR<RR::Wire<GraphPtr>> & value)
{
  auto b = RR_MAKE_SHARED<RR::WireBroadcaster<GraphPtr>>();
  b->Init(value);
  b->SetOutValue(to_rr(graph_->current()));
  std::lock_guard<std::mutex> lock(wire_mutex_);
  graph_wire_ = value;
  graph_broadcaster_ = b;
}

void RootObject::graph_changed()
{
  RR_SHARED_PTR<RR::WireBroadcaster<GraphPtr>> b;
  {
    std::lock_guard<std::mutex> lock(wire_mutex_);
    b = graph_broadcaster_;
  }
  if (b) {
    b->SetOutValue(to_rr(graph_->current()));
  }
}

RR_INTRUSIVE_PTR<RR::RRList<gen::TopicEndpoint>> RootObject::topic_endpoints(
  const std::string & topic)
{
  std::vector<EndpointData> eps;
  try {
    eps = graph_->endpoints(topic);
  } catch (const std::invalid_argument & e) {
    throw RR::InvalidArgumentException(e.what());
  }
  auto list = RR::AllocateEmptyRRList<gen::TopicEndpoint>();
  for (const auto & e : eps) {
    RR_INTRUSIVE_PTR<gen::TopicEndpoint> x(new gen::TopicEndpoint());
    x->node_name = e.node_name;
    x->node_namespace = e.node_namespace;
    x->topic_type = e.topic_type;
    x->endpoint_type = e.endpoint_type;
    x->reliability = e.reliability;
    x->durability = e.durability;
    x->history = e.history;
    x->depth = e.depth;
    x->deadline = e.deadline;
    x->lifespan = e.lifespan;
    x->liveliness = e.liveliness;
    x->liveliness_lease_duration = e.liveliness_lease_duration;
    list->push_back(x);
  }
  return list;
}

void RootObject::RRServiceObjectInit(
  RR_WEAK_PTR<RR::ServerContext> context, const std::string & service_path)
{
  proxies_->set_server_context(context, service_path);
}

namespace
{

// The client making the current request.
uint32_t current_client()
{
  auto ep = RR::ServerEndpoint::GetCurrentEndpoint();
  return ep ? ep->GetLocalEndpoint() : 0;
}

}  // namespace

int32_t RootObject::subscribe(const std::string & topic, const std::string & msgtype)
{
  return proxies_->subscribe(topic, msgtype, current_client());
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_subscribers(int32_t ind)
{
  return proxies_->object(ind, ProxyKind::Subscriber);
}

int32_t RootObject::publish(const std::string & topic, const std::string & msgtype)
{
  return proxies_->publish(topic, msgtype, current_client());
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_publishers(int32_t ind)
{
  return proxies_->object(ind, ProxyKind::Publisher);
}

int32_t RootObject::client(const std::string & service, const std::string & srvtype)
{
  return proxies_->client(service, srvtype, current_client());
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_clients(int32_t ind)
{
  return proxies_->object(ind, ProxyKind::Client);
}

int32_t RootObject::register_service(const std::string & service, const std::string & srvtype)
{
  return proxies_->register_service(service, srvtype, current_client());
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_services(int32_t ind)
{
  return proxies_->object(ind, ProxyKind::Service);
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_parameters() {return proxies_->parameters();}

RR_SHARED_PTR<RR::RRObject> RootObject::get_components() {return proxies_->components();}

int32_t RootObject::action_client(const std::string & action, const std::string & actiontype)
{
  return proxies_->action_client(action, actiontype, current_client());
}

RR_SHARED_PTR<RR::RRObject> RootObject::get_action_clients(int32_t ind)
{
  return proxies_->object(ind, ProxyKind::ActionClient);
}

std::string RootObject::get_ros_node_name() {return identity_.ros_node_name;}

std::string RootObject::get_ros_namespace() {return identity_.ros_namespace;}

std::string RootObject::get_rmw_implementation() {return identity_.rmw_implementation;}

std::string RootObject::get_bridge_version() {return identity_.bridge_version;}

RR::RRListPtr<RR::RRArray<char>> RootObject::get_capabilities()
{
  auto list = RR::AllocateEmptyRRList<RR::RRArray<char>>();
  for (const auto & c : identity_.capabilities) {
    list->push_back(RR::stringToRRArray(c));
  }
  return list;
}

}  // namespace robotraconteur_ros2_debug_bridge
