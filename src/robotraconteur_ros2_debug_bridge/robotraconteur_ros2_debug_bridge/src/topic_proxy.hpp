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

// The proxy objects behind the root's handles, and their registry.
//
// Topics (B4): subscribe()/publish() give rostopic_ subscriber and publisher
// objects. A subscriber feeds its pipe and its wire from one generic
// (serialized) subscription; a publisher publishes what arrives by
// publish(), its pipe or its wire. The client chooses; every member is
// always offered. Services (B6): client()/register_service() give
// rosservice_ rosclient and rosservice objects (service_proxy.hpp).

#ifndef TOPIC_PROXY_HPP_
#define TOPIC_PROXY_HPP_

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "RobotRaconteur.h"
#include "exposure_policy.hpp"
#include "generic_service.hpp"
#include "rclcpp/rclcpp.hpp"
#include "type_mapper.hpp"
#include "typesupport.hpp"

namespace robotraconteur_ros2_debug_bridge
{

class ProxyExecutor;
class ProxyManager;

/// The root objref a handle belongs to.
enum class ProxyKind { Subscriber, Publisher, Client, Service, ActionClient };

/// Base of the objects behind handles.
class ProxyObject : public GenericObject
{
public:
  explicit ProxyObject(std::string type)
  : GenericObject(std::move(type)) {}

  /// Releases the ROS entity (on the proxy thread). Idempotent.
  virtual void close() = 0;

  void bind(std::weak_ptr<ProxyManager> manager, int32_t handle)
  {
    manager_ = std::move(manager);
    handle_id_ = handle;
  }

protected:
  std::weak_ptr<ProxyManager> manager_;
  int32_t handle_id_ = 0;
};

/// Owns the proxy objects. Thread-safe: called on Robot Raconteur threads;
/// ROS callbacks reach the objects weakly.
class ProxyManager : public std::enable_shared_from_this<ProxyManager>
{
public:
  ProxyManager(
    rclcpp::Node & node, RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> rr_node,
    std::shared_ptr<const ExposurePolicy> policy, bool fast_path = false);
  ~ProxyManager();

  /// Handles for the root's objrefs. Types are "pkg/Name", "pkg/msg/Name"
  /// or "pkg/srv/Name", or empty to take them from the graph. Throw Robot
  /// Raconteur exceptions (the client sees them).
  int32_t subscribe(const std::string & topic, const std::string & msgtype, uint32_t client);
  int32_t publish(const std::string & topic, const std::string & msgtype, uint32_t client);
  int32_t client(const std::string & service, const std::string & srvtype, uint32_t client);
  int32_t register_service(
    const std::string & service, const std::string & srvtype, uint32_t client);
  /// `actiontype` "pkg/Name" or "pkg/action/Name", or empty (from the graph).
  int32_t action_client(
    const std::string & action, const std::string & actiontype, uint32_t client);
  RR_SHARED_PTR<RobotRaconteur::RRObject> object(int32_t handle, ProxyKind kind);

  /// Removes a handle's object (unsubscribe, or its client left).
  void remove(int32_t handle);
  void client_disconnected(uint32_t client);
  /// For releasing the objrefs' cached skels when a handle goes away.
  void set_server_context(
    RR_WEAK_PTR<RobotRaconteur::ServerContext> context, const std::string & service_path);

  /// Drops every object; ROS callbacks after this do nothing.
  void shutdown();

  rclcpp::Node & node() {return node_;}
  const std::shared_ptr<ProxyExecutor> & proxy_executor() {return proxy_;}
  RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> rr_node() {return rr_node_.lock();}
  TypeMapper & mapper() {return mapper_;}
  IntrospectionCache & typesupport() {return typesupport_;}
  const ExposurePolicy & policy() const {return *policy_;}
  /// Registers a generated definition and what it imports (idempotent).
  void register_definitions(const std::string & root);
  /// Registers a hand-written definition (idempotent).
  void register_definition_text(const std::string & name, const std::string & text);

  /// The root's parameters object (B7), made on first use.
  RR_SHARED_PTR<RobotRaconteur::RRObject> parameters();
  /// The root's components object (B9), made on first use.
  RR_SHARED_PTR<RobotRaconteur::RRObject> components();

private:
  struct Resolved
  {
    std::string name;   // fully qualified
    std::string type;   // pkg/msg/Name or pkg/srv/Name
  };
  std::string expand(const std::string & name, bool is_service) const;
  Resolved resolve_topic(const std::string & fq, const std::string & msgtype) const;
  Resolved resolve_service(const std::string & fq, const std::string & srvtype) const;
  int32_t add(const RR_SHARED_PTR<ProxyObject> & object, uint32_t client, ProxyKind kind);
  void release_paths(const std::vector<std::pair<int32_t, ProxyKind>> & handles);

  rclcpp::Node & node_;
  RR_WEAK_PTR<RobotRaconteur::RobotRaconteurNode> rr_node_;
  std::shared_ptr<const ExposurePolicy> policy_;
  TypeMapper mapper_;
  IntrospectionCache typesupport_;
  std::shared_ptr<ProxyExecutor> proxy_;  // creates, spins and destroys the proxies
  const bool fast_path_;

  std::mutex mutex_;
  bool shut_down_ = false;
  int32_t next_handle_ = 1;
  struct Entry
  {
    RR_SHARED_PTR<ProxyObject> object;
    uint32_t client;
    ProxyKind kind;
  };
  std::map<int32_t, Entry> objects_;
  RR_WEAK_PTR<RobotRaconteur::ServerContext> context_;
  std::string service_path_;
  RR_SHARED_PTR<ProxyObject> parameters_;
  RR_SHARED_PTR<ProxyObject> components_;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // TOPIC_PROXY_HPP_
