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

#ifndef HOST_HPP_
#define HOST_HPP_

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rclcpp/executor.hpp"
#include "rclcpp/node.hpp"

#include "config.hpp"

namespace rmw_intraprocess_container
{

/// A component that could not be loaded; the message gives the reason.
class LoadError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct LoadedComponent
{
  uint64_t unique_id = 0;
  std::string full_node_name;
  ComponentConfig config;
};

/// The (class name, library path) pairs a package registers in the ament
/// index for rclcpp_components, as rclcpp_components::ComponentManager reads
/// them. Throws LoadError if the package registers none.
std::vector<std::pair<std::string, std::string>> component_resources(const std::string & package);

class Loader;

/// Owns the executor and an rclcpp_components::ComponentManager node (the
/// container), and loads components into them as configured. The container
/// keeps the standard ~/_container/load_node, unload_node and list_nodes
/// services, reachable from within the process.
///
/// Threading: construct, load, unload and shut down from one thread (rcl's
/// per-node state is not thread-safe, see docs/unsupported.md), and not while
/// spin() runs; spin() may run on another thread while that thread waits in
/// stop(). To load or unload while spinning, call the container's load_node
/// and unload_node services (they run on the executor).
class ComponentHost
{
public:
  /// rclcpp must be initialized.
  explicit ComponentHost(const HostConfig & config);
  ~ComponentHost();

  ComponentHost(const ComponentHost &) = delete;
  ComponentHost & operator=(const ComponentHost &) = delete;

  /// Loads one component. Throws LoadError.
  LoadedComponent load(const ComponentConfig & component);

  /// Loads every enabled component of the configuration, in order. Logs each
  /// failure; stops at the first one unless continue_on_error is set. Returns
  /// true if all loaded.
  bool load_all();

  /// Unloads one component. Throws LoadError for an unknown id.
  void unload(uint64_t unique_id);

  /// The components this host loaded (load(), load_all()) and has not
  /// unloaded itself; components loaded or unloaded through the container's
  /// services are not tracked here (shutdown() unloads those too).
  std::vector<LoadedComponent> loaded() const {return loaded_;}

  rclcpp::Node::SharedPtr container_node() const;

  /// Spins until rclcpp shuts down or stop()/request_stop() is called.
  void spin();
  /// Non-blocking; safe from any thread, including a callback.
  void request_stop();
  /// Requests a stop and waits until spin() has returned.
  void stop();

  /// Stops, unloads every component (in reverse load order) and releases the
  /// executor and the container. Component libraries stay mapped until the
  /// process exits (see docs/container.md). Called by the destructor.
  void shutdown();

private:
  HostConfig config_;
  std::shared_ptr<rclcpp::Executor> executor_;
  std::shared_ptr<Loader> loader_;
  std::vector<LoadedComponent> loaded_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> spinning_{false};
};

}  // namespace rmw_intraprocess_container

#endif  // HOST_HPP_
