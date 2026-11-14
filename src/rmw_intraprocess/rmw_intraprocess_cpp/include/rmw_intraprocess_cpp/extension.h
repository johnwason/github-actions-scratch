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

/// \file
/// Extension API of rmw_intraprocess_cpp (M9): the "tap", which lets an
/// in-process gateway (the Robot Raconteur bridge) observe every message
/// published on a topic, with the publisher's native message and its
/// introspection type support, without subscribing and without a CDR round
/// trip.
///
/// The API is a versioned C ABI: one exported function returns a table of
/// function pointers. A client should not link this library: it looks the
/// function up at run time in the RMW library that rmw_implementation loaded
/// (when rmw_get_implementation_identifier() is "rmw_intraprocess_cpp"), so it
/// builds and runs unchanged with any other RMW (feature detection). See
/// extension.hpp for a C++ helper.
///
/// Thread safety: all functions may be called from any thread.

#ifndef RMW_INTRAPROCESS_CPP__EXTENSION_H_
#define RMW_INTRAPROCESS_CPP__EXTENSION_H_

#include <stddef.h>
#include <stdint.h>

#include "rmw/types.h"

#include "rmw_intraprocess_cpp/visibility_control.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// Version of the API described here. A later version only appends members to
/// rmw_intraprocess_extension_api_t; check `struct_size` before using one.
#define RMW_INTRAPROCESS_EXTENSION_API_VERSION 1u

/// Name of the exported entry point (for run-time symbol lookup).
#define RMW_INTRAPROCESS_EXTENSION_API_SYMBOL "rmw_intraprocess_cpp_get_extension_api"

/// One published message, as seen by a tap. Every pointer is valid only for
/// the duration of the callback; copy what is needed.
typedef struct rmw_intraprocess_tap_sample_s
{
  /// Fully qualified topic name, e.g. "/chatter".
  const char * topic_name;
  /// ROS type name, e.g. "std_msgs/msg/String".
  const char * type_name;
  /// The publisher's native message, or NULL when it was published serialized
  /// (rmw_publish_serialized_message, rclcpp GenericPublisher). Its layout is
  /// the one described by introspection_members.
  const void * ros_message;
  /// rosidl_typesupport_introspection_c__MessageMembers or
  /// rosidl_typesupport_introspection_cpp::MessageMembers of the publisher's
  /// type (see introspection_identifier); NULL if the type has none.
  const void * introspection_members;
  /// "rosidl_typesupport_introspection_c" or
  /// "rosidl_typesupport_introspection_cpp"; NULL with introspection_members.
  const char * introspection_identifier;
  /// The message serialized as CDR (with the encapsulation header), as
  /// delivered to subscriptions. Always set.
  const uint8_t * cdr;
  size_t cdr_length;
  /// Publisher identity and the sample's metadata, as subscriptions see them.
  rmw_gid_t publisher_gid;
  rmw_time_point_value_t source_timestamp;
  uint64_t publication_sequence_number;
} rmw_intraprocess_tap_sample_t;

/// Tap callback. Called synchronously on the publishing thread, inside
/// rmw_publish, after the sample was delivered to the subscriptions, with no
/// RMW lock held. It must be quick and must not block on the publishing
/// thread's own work. It may publish (the publication is tapped too).
typedef void (* rmw_intraprocess_tap_callback_t)(
  const rmw_intraprocess_tap_sample_t * sample, void * user_data);

/// Function table returned by rmw_intraprocess_cpp_get_extension_api().
typedef struct rmw_intraprocess_extension_api_s
{
  /// RMW_INTRAPROCESS_EXTENSION_API_VERSION of the library.
  uint32_t version;
  /// sizeof(rmw_intraprocess_extension_api_t) in the library.
  size_t struct_size;

  /// Registers a tap. topic_name: a fully qualified topic name, or NULL for
  /// every topic. Taps are process-wide (all contexts) and invisible to the
  /// graph. Returns a nonzero handle, or 0 on invalid arguments.
  uint64_t (* add_tap)(
    const char * topic_name, rmw_intraprocess_tap_callback_t callback, void * user_data);

  /// Removes a tap. When it returns, the callback is not running and will not
  /// be called again (it waits for calls in progress on other threads; called
  /// from inside the tap's own callback, it does not wait for that call).
  /// Returns RMW_RET_OK, or RMW_RET_ERROR for an unknown handle.
  rmw_ret_t (* remove_tap)(uint64_t handle);
} rmw_intraprocess_extension_api_t;

/// Returns the extension API if `version` is supported (1 ..
/// RMW_INTRAPROCESS_EXTENSION_API_VERSION), else NULL. The table is static:
/// valid as long as the library is loaded.
RMW_INTRAPROCESS_CPP_PUBLIC
const rmw_intraprocess_extension_api_t *
rmw_intraprocess_cpp_get_extension_api(uint32_t version);

/// Signature of rmw_intraprocess_cpp_get_extension_api (for symbol lookup).
typedef const rmw_intraprocess_extension_api_t * (* rmw_intraprocess_get_extension_api_t)(
  uint32_t version);

/// Finding the API of the RMW instance that is actually in use: the string
/// returned by rmw_get_implementation_identifier() is the first member of
/// this block, so a client that sees the identifier "rmw_intraprocess_cpp"
/// can reach the entry point of that very library instance, without looking
/// the library up by name (which can load a second copy of the library from
/// another directory: on Windows that is a separate module with its own taps).
#define RMW_INTRAPROCESS_IDENTIFIER_BLOCK_MAGIC 0x52495031u  /* "RIP1" */

typedef struct rmw_intraprocess_identifier_block_s
{
  /// "rmw_intraprocess_cpp", NUL terminated.
  char identifier[32];
  /// RMW_INTRAPROCESS_IDENTIFIER_BLOCK_MAGIC.
  uint32_t magic;
  /// rmw_intraprocess_cpp_get_extension_api of this instance.
  rmw_intraprocess_get_extension_api_t get_extension_api;
} rmw_intraprocess_identifier_block_t;

#ifdef __cplusplus
}
#endif

#endif  // RMW_INTRAPROCESS_CPP__EXTENSION_H_
