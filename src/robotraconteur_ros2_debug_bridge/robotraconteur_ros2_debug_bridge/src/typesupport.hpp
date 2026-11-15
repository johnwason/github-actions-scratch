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

// rosidl introspection type supports (C and C++), loaded by type name and
// kept loaded, and dynamically allocated ROS messages.

#ifndef TYPESUPPORT_HPP_
#define TYPESUPPORT_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

struct rosidl_message_type_support_t;
struct rosidl_service_type_support_t;

namespace rcpputils
{
class SharedLibrary;
}

namespace robotraconteur_ros2_debug_bridge
{

class TypeSupportError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

/// Which introspection type support: the layouts differ (std::string and
/// std::vector vs rosidl_runtime_c structs).
enum class Flavor { Cpp, C };

/// The rosidl_typesupport_introspection_{cpp,c} identifier.
const char * typesupport_identifier(Flavor flavor);

/// Loads introspection type supports on demand and keeps their libraries
/// loaded for its lifetime. Thread-safe. Pointers it returns stay valid
/// while it lives.
class IntrospectionCache
{
public:
  IntrospectionCache();
  ~IntrospectionCache();
  IntrospectionCache(const IntrospectionCache &) = delete;
  IntrospectionCache & operator=(const IntrospectionCache &) = delete;

  /// The MessageMembers (rosidl_typesupport_introspection_{cpp,c}) of
  /// "pkg/<subfolder>/Name" (subfolder msg, srv or action, for the parts of a
  /// service or action as well). Throws TypeSupportError.
  const void * message(Flavor flavor, const std::string & full_type);
  /// The ServiceMembers of "pkg/srv/Name" (or an action's service).
  const void * service(Flavor flavor, const std::string & full_type);
  /// The rosidl_typesupport_cpp handle (what rmw_serialize and
  /// rmw_deserialize take; RMW-agnostic) of a message type.
  const rosidl_message_type_support_t * cpp_handle(const std::string & full_type);
  /// The same for a service type.
  const rosidl_service_type_support_t * cpp_service_handle(const std::string & full_type);

private:
  std::shared_ptr<rcpputils::SharedLibrary> library_locked(
    const char * identifier, const std::string & full_type);

  std::mutex mutex_;
  std::map<std::string, std::shared_ptr<rcpputils::SharedLibrary>> libraries_;
};

/// A ROS message allocated and initialized from its introspection members,
/// finalized and freed on destruction.
class DynamicMessage
{
public:
  DynamicMessage(Flavor flavor, const void * members);
  ~DynamicMessage();
  DynamicMessage(const DynamicMessage &) = delete;
  DynamicMessage & operator=(const DynamicMessage &) = delete;

  void * data() {return data_;}
  const void * data() const {return data_;}
  Flavor flavor() const {return flavor_;}
  const void * members() const {return members_;}

private:
  Flavor flavor_;
  const void * members_;
  void * data_ = nullptr;
};

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // TYPESUPPORT_HPP_
