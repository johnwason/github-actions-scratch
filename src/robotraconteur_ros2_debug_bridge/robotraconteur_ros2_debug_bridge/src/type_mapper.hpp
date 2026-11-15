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

// ROS interface types -> Robot Raconteur service definitions (B2).
//
// Follows the naming and object model of the existing Python bridge
// (robotraconteur-contrib/robotraconteur_ros2_bridge) as a superset; see
// docs/type_mapping.md. Fields come from the C++ rosidl introspection type
// support. Constants, which introspection does not carry, come from the
// installed interface file (share/<pkg>/{msg,srv,action}/<Name>.*).

#ifndef TYPE_MAPPER_HPP_
#define TYPE_MAPPER_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace rcpputils
{
class SharedLibrary;
}

namespace robotraconteur_ros2_debug_bridge
{

class TypeMapperError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

enum class InterfaceKind { Message, Service, Action };

struct InterfaceType
{
  std::string package;
  InterfaceKind kind = InterfaceKind::Message;
  std::string name;

  /// "msg", "srv" or "action".
  const char * subfolder() const;
  /// "pkg/msg/Name" etc.
  std::string full_name() const;
};

/// Parses "pkg/msg/Name", "pkg/srv/Name", "pkg/action/Name", or "pkg/Name"
/// (the upstream bridge's form; the kind is then `default_kind`, required).
InterfaceType parse_interface_type(
  const std::string & type, std::optional<InterfaceKind> default_kind = std::nullopt);

/// The upstream bridge's `fixname`, extended to every name Robot Raconteur
/// rejects: reserved words and the reserved prefixes get the prefix "ros".
std::string fixname(const std::string & name);

/// Robot Raconteur service definition names.
std::string message_definition_name(const std::string & package, const std::string & name);
std::string topic_definition_name(const std::string & package, const std::string & name);
std::string service_definition_name(const std::string & package, const std::string & name);
std::string action_definition_name(const std::string & package, const std::string & name);

/// Interfaces of an installed package ("pkg/msg/Name", ...; sorted), from
/// the ament index (rosidl_interfaces). Throws TypeMapperError if unknown.
std::vector<std::string> package_interfaces(const std::string & package);
/// Every package in the ament index that has interfaces; sorted.
std::vector<std::string> interface_packages();

/// Parses and verifies definition texts together with Robot Raconteur;
/// throws TypeMapperError with Robot Raconteur's message on failure.
void verify_definitions(const std::vector<std::string> & texts);

/// Generates and caches definitions. Thread-safe. Keeps the type support
/// libraries it loads for its lifetime.
class TypeMapper
{
public:
  TypeMapper();
  ~TypeMapper();
  TypeMapper(const TypeMapper &) = delete;
  TypeMapper & operator=(const TypeMapper &) = delete;

  /// Each returns the name of the generated definition (rosmsg_, rostopic_,
  /// rosservice_ or rosaction_). Type names as in parse_interface_type(),
  /// with the default kind implied. Throws TypeMapperError.
  std::string message(const std::string & type);
  std::string topic(const std::string & message_type);
  std::string service(const std::string & type);
  std::string action(const std::string & type);
  /// Dispatches on the kind in a "pkg/<kind>/Name" type: message -> topic
  /// definition, service, action.
  std::string interface(const std::string & type);

  /// Text of a definition generated earlier.
  std::string definition(const std::string & definition_name) const;
  /// `definition_name` and everything it imports, imports first.
  std::vector<std::string> closure(const std::string & definition_name) const;

private:
  struct Entry
  {
    std::string text;
    std::vector<std::string> imports;
  };

  std::string message_locked(const InterfaceType & t);
  // `members`: a rosidl_typesupport_introspection_cpp::MessageMembers.
  std::string members_locked(const void * members);
  std::shared_ptr<rcpputils::SharedLibrary> library_locked(const InterfaceType & t);
  const void * load_message_locked(const InterfaceType & t, const std::string & name);
  void closure_locked(
    const std::string & name, std::vector<std::string> & out) const;

  mutable std::mutex mutex_;
  std::map<std::string, Entry> definitions_;
  std::map<std::string, std::shared_ptr<rcpputils::SharedLibrary>> libraries_;  // by package
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // TYPE_MAPPER_HPP_
