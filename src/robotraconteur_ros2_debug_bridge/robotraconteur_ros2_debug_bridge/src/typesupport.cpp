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

#include "typesupport.hpp"

#include <cstddef>
#include <cstring>
#include <new>
#include <string>

#include "rclcpp/typesupport_helpers.hpp"
#include "rcpputils/shared_library.hpp"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

namespace robotraconteur_ros2_debug_bridge
{

const char * typesupport_identifier(Flavor flavor)
{
  return flavor == Flavor::Cpp ?
         rosidl_typesupport_introspection_cpp::typesupport_identifier :
         rosidl_typesupport_introspection_c__identifier;
}

IntrospectionCache::IntrospectionCache() = default;
IntrospectionCache::~IntrospectionCache() = default;

std::shared_ptr<rcpputils::SharedLibrary> IntrospectionCache::library_locked(
  const char * identifier, const std::string & full_type)
{
  const auto slash = full_type.find('/');
  const std::string key = std::string(identifier) + ":" + full_type.substr(0, slash);
  auto & lib = libraries_[key];
  if (!lib) {
    try {
      lib = rclcpp::get_typesupport_library(full_type, identifier);
    } catch (const std::exception & e) {
      libraries_.erase(key);
      throw TypeSupportError(
              "cannot load the type support for '" + full_type + "': " + e.what());
    }
  }
  return lib;
}

const void * IntrospectionCache::message(Flavor flavor, const std::string & full_type)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const char * id = typesupport_identifier(flavor);
  auto lib = library_locked(id, full_type);
  try {
    return rclcpp::get_message_typesupport_handle(full_type, id, *lib)->data;
  } catch (const std::exception & e) {
    throw TypeSupportError("unknown ROS type '" + full_type + "': " + e.what());
  }
}

const void * IntrospectionCache::service(Flavor flavor, const std::string & full_type)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const char * id = typesupport_identifier(flavor);
  auto lib = library_locked(id, full_type);
  try {
    return rclcpp::get_service_typesupport_handle(full_type, id, *lib)->data;
  } catch (const std::exception & e) {
    throw TypeSupportError("unknown ROS type '" + full_type + "': " + e.what());
  }
}

const rosidl_message_type_support_t * IntrospectionCache::cpp_handle(const std::string & full_type)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const char * id = "rosidl_typesupport_cpp";
  auto lib = library_locked(id, full_type);
  try {
    return rclcpp::get_message_typesupport_handle(full_type, id, *lib);
  } catch (const std::exception & e) {
    throw TypeSupportError("unknown ROS type '" + full_type + "': " + e.what());
  }
}

const rosidl_service_type_support_t * IntrospectionCache::cpp_service_handle(
  const std::string & full_type)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const char * id = "rosidl_typesupport_cpp";
  auto lib = library_locked(id, full_type);
  try {
    return rclcpp::get_service_typesupport_handle(full_type, id, *lib);
  } catch (const std::exception & e) {
    throw TypeSupportError("unknown ROS type '" + full_type + "': " + e.what());
  }
}

// ---------------------------------------------------------------------------

namespace
{

constexpr std::align_val_t kAlign{alignof(std::max_align_t)};

}  // namespace

DynamicMessage::DynamicMessage(Flavor flavor, const void * members)
: flavor_(flavor), members_(members)
{
  if (flavor == Flavor::Cpp) {
    const auto * m = static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
      members);
    data_ = ::operator new(m->size_of_, kAlign);
    // The C init leaves members without defaults (fixed arrays) as they are.
    std::memset(data_, 0, m->size_of_);
    try {
      m->init_function(data_, rosidl_runtime_cpp::MessageInitialization::ALL);
    } catch (...) {
      ::operator delete(data_, kAlign);
      throw;
    }
  } else {
    const auto * m = static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(
      members);
    data_ = ::operator new(m->size_of_, kAlign);
    // The C init leaves members without defaults (fixed arrays) as they are.
    std::memset(data_, 0, m->size_of_);
    m->init_function(data_, ROSIDL_RUNTIME_C_MSG_INIT_ALL);
  }
}

DynamicMessage::~DynamicMessage()
{
  if (flavor_ == Flavor::Cpp) {
    static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(members_)
    ->fini_function(data_);
  } else {
    static_cast<const rosidl_typesupport_introspection_c__MessageMembers *>(members_)
    ->fini_function(data_);
  }
  ::operator delete(data_, kAlign);
}

}  // namespace robotraconteur_ros2_debug_bridge
