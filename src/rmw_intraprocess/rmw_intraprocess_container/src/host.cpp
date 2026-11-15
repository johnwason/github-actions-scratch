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

#include "host.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ament_index_cpp/get_resource.hpp"
#include "class_loader/class_loader.hpp"
#include "composition_interfaces/srv/load_node.hpp"
#include "composition_interfaces/srv/unload_node.hpp"
#include "rclcpp/executors/multi_threaded_executor.hpp"
#include "rclcpp/executors/single_threaded_executor.hpp"
#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp_components/component_manager.hpp"
#include "rclcpp_components/component_manager_isolated.hpp"
#include "rcpputils/shared_library.hpp"

namespace rmw_intraprocess_container
{

std::vector<std::pair<std::string, std::string>> component_resources(const std::string & package)
{
  // Same lookup as rclcpp_components::ComponentManager::get_component_resources.
  std::string content;
  std::string base_path;
  if (!ament_index_cpp::get_resource("rclcpp_components", package, content, &base_path)) {
    throw LoadError(
            "package '" + package + "' registers no components (not in the ament index under "
            "rclcpp_components; is its install prefix in AMENT_PREFIX_PATH?)");
  }
  std::vector<std::pair<std::string, std::string>> out;
  std::istringstream lines(content);
  for (std::string line; std::getline(lines, line); ) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t sep = line.find(';');
    if (sep == std::string::npos) {
      continue;
    }
    std::filesystem::path lib(line.substr(sep + 1));
    if (lib.is_relative()) {
      lib = std::filesystem::path(base_path) / lib;
    }
    out.emplace_back(line.substr(0, sep), lib.string());
  }
  return out;
}

/// The container node, seen without its executor model (shared or isolated).
class Loader
{
public:
  virtual ~Loader() = default;
  virtual rclcpp::Node::SharedPtr node() = 0;
  virtual LoadedComponent load(
    const ComponentConfig & component, const std::vector<std::string> & extra_arguments) = 0;
  virtual void unload(uint64_t unique_id) = 0;
  /// Every loaded component, including those loaded through the container's
  /// load_node service, oldest first.
  virtual std::vector<uint64_t> unique_ids() = 0;
  /// Moves the class loaders out of the container so destroying it does not
  /// unload the component libraries.
  virtual void keep_libraries_loaded() = 0;
};

namespace
{

using LoadNode = composition_interfaces::srv::LoadNode;
using UnloadNode = composition_interfaces::srv::UnloadNode;

/// Component libraries stay mapped until the process ends. Unloading them at
/// exit gains nothing and is not safe: the type support libraries they pulled
/// in hold rosidl_typesupport_cpp/_c's per-type caches (unreachable, so
/// reported as leaks, once unmapped), and class_loader's own static library
/// list dlcloses everything it loaded during static destruction. So the class
/// loaders are kept (not destroyed with the container), and each library gets
/// one extra handle that is never closed. Both live in leaked, reachable
/// containers (not reported as leaks).
struct KeptLibraries
{
  std::vector<class_loader::ClassLoader *> loaders;
  std::vector<rcpputils::SharedLibrary *> handles;
};

KeptLibraries & kept_libraries()
{
  static auto * kept = new KeptLibraries();
  return *kept;
}

/// rclcpp_components' own loading code (ament index lookup, class_loader,
/// node options from a LoadNode request), driven by the configuration instead
/// of a service call, plus parameter files and per-node --ros-args, which a
/// LoadNode request cannot carry.
template<class ManagerT>
class HostManager : public ManagerT, public Loader
{
public:
  HostManager(
    std::weak_ptr<rclcpp::Executor> executor, const std::string & name,
    const rclcpp::NodeOptions & options)
  : ManagerT(executor, name, options) {}

  rclcpp::Node::SharedPtr node() override {return this->shared_from_this();}

  LoadedComponent load(
    const ComponentConfig & component, const std::vector<std::string> & extra_arguments) override
  {
    auto request = std::make_shared<LoadNode::Request>();
    request->package_name = component.package;
    request->plugin_name = component.plugin;
    request->node_name = component.name;
    request->node_namespace = component.node_namespace;
    request->remap_rules = component.remappings;
    for (const auto & p : component.parameters) {
      request->parameters.push_back(p.to_parameter_msg());
    }
    request->extra_arguments.push_back(
      rclcpp::Parameter("use_intra_process_comms", component.use_intra_process_comms)
      .to_parameter_msg());

    auto response = std::make_shared<LoadNode::Response>();
    pending_arguments_ = extra_arguments;
    try {
      this->on_load_node(std::make_shared<rmw_request_id_t>(), request, response);
    } catch (const std::exception & e) {
      response->success = false;
      response->error_message = e.what();
    }
    pending_arguments_.clear();
    if (!response->success) {
      throw LoadError(response->error_message);
    }
    return LoadedComponent{response->unique_id, response->full_node_name, component};
  }

  void unload(uint64_t unique_id) override
  {
    auto request = std::make_shared<UnloadNode::Request>();
    request->unique_id = unique_id;
    auto response = std::make_shared<UnloadNode::Response>();
    this->on_unload_node(std::make_shared<rmw_request_id_t>(), request, response);
    if (!response->success) {
      throw LoadError(response->error_message);
    }
  }

  std::vector<uint64_t> unique_ids() override
  {
    std::vector<uint64_t> ids;
    for (const auto & entry : this->node_wrappers_) {  // std::map: ascending ids
      ids.push_back(entry.first);
    }
    return ids;
  }

  void keep_libraries_loaded() override
  {
    KeptLibraries & kept = kept_libraries();
    for (auto & entry : this->loaders_) {
      try {
        kept.handles.push_back(new rcpputils::SharedLibrary(entry.first));
      } catch (const std::exception & e) {
        RCLCPP_WARN(
          this->get_logger(), "Cannot pin component library %s: %s", entry.first.c_str(),
          e.what());
      }
      kept.loaders.push_back(entry.second.release());
    }
    this->loaders_.clear();
  }

protected:
  rclcpp::NodeOptions create_node_options(const std::shared_ptr<LoadNode::Request> request)
  override
  {
    rclcpp::NodeOptions options = ManagerT::create_node_options(request);
    if (!pending_arguments_.empty()) {
      std::vector<std::string> arguments = options.arguments();
      arguments.push_back("--ros-args");
      arguments.insert(arguments.end(), pending_arguments_.begin(), pending_arguments_.end());
      options.arguments(arguments);
    }
    return options;
  }

private:
  std::vector<std::string> pending_arguments_;
};

std::shared_ptr<rclcpp::Executor> make_executor(const ExecutorConfig & config)
{
  if (config.isolated) {
    // Only the container's own node runs here; each component has its own.
    return std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  }
  switch (config.type) {
    case ExecutorType::MultiThreaded:
      return std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
        rclcpp::ExecutorOptions(), config.threads);
    case ExecutorType::Events:
      return std::make_shared<rclcpp::experimental::executors::EventsExecutor>();
    case ExecutorType::SingleThreaded:
      break;
  }
  return std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
}

template<class ManagerT>
std::shared_ptr<Loader> make_manager(
  const std::shared_ptr<rclcpp::Executor> & executor, const HostConfig & config)
{
  auto options = rclcpp::NodeOptions()
    .start_parameter_services(false)
    .start_parameter_event_publisher(false)
    .arguments({"--ros-args", "-r", "__ns:=" + config.container_namespace});
  return std::make_shared<HostManager<ManagerT>>(executor, config.container_name, options);
}

std::shared_ptr<Loader> make_loader(
  const std::shared_ptr<rclcpp::Executor> & executor, const HostConfig & config)
{
  if (!config.executor.isolated) {
    return make_manager<rclcpp_components::ComponentManager>(executor, config);
  }
  if (config.executor.type == ExecutorType::MultiThreaded) {
    return make_manager<rclcpp_components::ComponentManagerIsolated<
               rclcpp::executors::MultiThreadedExecutor>>(executor, config);
  }
  return make_manager<rclcpp_components::ComponentManagerIsolated<
             rclcpp::executors::SingleThreadedExecutor>>(executor, config);
}

}  // namespace

ComponentHost::ComponentHost(const HostConfig & config)
: config_(config)
{
  executor_ = make_executor(config_.executor);
  loader_ = make_loader(executor_, config_);
  executor_->add_node(loader_->node());
}

ComponentHost::~ComponentHost()
{
  shutdown();
}

rclcpp::Node::SharedPtr ComponentHost::container_node() const
{
  return loader_ ? loader_->node() : nullptr;
}

LoadedComponent ComponentHost::load(const ComponentConfig & component)
{
  if (!loader_) {
    throw LoadError("the host has been shut down");
  }
  std::vector<std::string> arguments;
  std::vector<std::string> files = config_.parameter_files;
  files.insert(files.end(), component.parameter_files.begin(), component.parameter_files.end());
  for (const auto & file : files) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
      throw LoadError("parameter file '" + file + "' not found");
    }
    arguments.push_back("--params-file");
    arguments.push_back(file);
  }
  arguments.insert(arguments.end(), component.ros_arguments.begin(), component.ros_arguments.end());
  LoadedComponent loaded = loader_->load(component, arguments);
  loaded_.push_back(loaded);
  return loaded;
}

bool ComponentHost::load_all()
{
  const auto logger = container_node()->get_logger();
  bool ok = true;
  for (const auto & component : config_.components) {
    if (!component.enabled) {
      RCLCPP_INFO(logger, "Skipping disabled component %s", component.describe().c_str());
      continue;
    }
    try {
      const LoadedComponent loaded = load(component);
      RCLCPP_INFO(
        logger, "Loaded %s as %s", component.describe().c_str(), loaded.full_node_name.c_str());
    } catch (const std::exception & e) {
      ok = false;
      RCLCPP_ERROR(
        logger, "Failed to load %s (configuration line %d): %s", component.describe().c_str(),
        component.line, e.what());
      if (!config_.continue_on_error) {
        break;
      }
    }
  }
  return ok;
}

void ComponentHost::unload(uint64_t unique_id)
{
  if (!loader_) {
    throw LoadError("the host has been shut down");
  }
  // Forget the entry even if the container no longer has the component
  // (unloaded through its unload_node service), then report that.
  loaded_.erase(
    std::remove_if(
      loaded_.begin(), loaded_.end(),
      [unique_id](const LoadedComponent & c) {return c.unique_id == unique_id;}),
    loaded_.end());
  loader_->unload(unique_id);
}

void ComponentHost::spin()
{
  if (!executor_) {
    return;
  }
  // Pairs with request_stop(): one of the two sees the other's flag.
  spinning_.store(true);
  if (!stop_requested_.load()) {
    executor_->spin();
  }
  spinning_.store(false);
}

void ComponentHost::request_stop()
{
  stop_requested_.store(true);
  if (executor_) {
    executor_->cancel();
  }
}

void ComponentHost::stop()
{
  request_stop();
  // A cancel() that lands before spin() has started is lost; repeat it.
  while (spinning_.load()) {
    executor_->cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void ComponentHost::shutdown()
{
  if (!loader_) {
    return;
  }
  stop();
  // Components first (newest first), with the executor no longer spinning
  // them; then the container node; then the executor (an EventsExecutor must
  // not outlive the entities it holds callbacks for, nor the reverse while it
  // runs).
  loaded_.clear();
  const std::vector<uint64_t> ids = loader_->unique_ids();
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    const uint64_t id = *it;
    try {
      loader_->unload(id);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(
        loader_->node()->get_logger(), "Failed to unload component %llu: %s",
        static_cast<unsigned long long>(id), e.what());  // NOLINT(runtime/int)
    }
  }
  executor_->remove_node(loader_->node());
  loader_->keep_libraries_loaded();
  executor_.reset();
  loader_.reset();
}

}  // namespace rmw_intraprocess_container
