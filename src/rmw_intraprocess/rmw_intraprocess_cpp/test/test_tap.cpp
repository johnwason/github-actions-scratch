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

// M9: the extension API's taps (include/rmw_intraprocess_cpp/extension.h).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "rcutils/allocator.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "test_msgs/msg/basic_types.hpp"

#include "rmw_intraprocess_cpp/extension.h"
#include "./rmw_fixture.hpp"

using namespace std::chrono_literals;
using test_msgs::msg::BasicTypes;

namespace
{

/// What a tap callback saw (copied inside the callback).
struct Seen
{
  std::mutex mutex;
  std::vector<std::string> topics;
  std::vector<int32_t> int32_values;  // read through introspection
  std::vector<const void *> messages;
  std::vector<std::vector<uint8_t>> cdr;
  std::vector<uint64_t> sequence_numbers;
  int null_messages = 0;
};

/// Reads the int32_value field through the introspection type support, the
/// way a gateway would (no deserialization).
int32_t read_int32_field(const rmw_intraprocess_tap_sample_t & s)
{
  EXPECT_STREQ(rosidl_typesupport_introspection_cpp::typesupport_identifier,
    s.introspection_identifier);
  const auto * members =
    static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
    s.introspection_members);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    const auto & m = members->members_[i];
    if (std::strcmp(m.name_, "int32_value") == 0) {
      EXPECT_EQ(rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32, m.type_id_);
      int32_t v;
      std::memcpy(&v, static_cast<const uint8_t *>(s.ros_message) + m.offset_, sizeof(v));
      return v;
    }
  }
  ADD_FAILURE() << "no int32_value member";
  return 0;
}

void record(const rmw_intraprocess_tap_sample_t * s, void * user_data)
{
  auto * seen = static_cast<Seen *>(user_data);
  std::lock_guard<std::mutex> lock(seen->mutex);
  seen->topics.emplace_back(s->topic_name);
  seen->messages.push_back(s->ros_message);
  seen->cdr.emplace_back(s->cdr, s->cdr + s->cdr_length);
  seen->sequence_numbers.push_back(s->publication_sequence_number);
  if (s->ros_message == nullptr) {
    ++seen->null_messages;
  } else {
    seen->int32_values.push_back(read_int32_field(*s));
  }
}

class Tap : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    api = rmw_intraprocess_cpp_get_extension_api(RMW_INTRAPROCESS_EXTENSION_API_VERSION);
    ASSERT_NE(nullptr, api);
    node = rmw_create_node(&context, "tap", "/");
    ASSERT_NE(nullptr, node);
  }

  void TearDown() override
  {
    for (auto * pub : pubs) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
    }
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    RmwContextFixture::TearDown();
  }

  rmw_publisher_t * make_publisher(const char * topic)
  {
    rmw_publisher_options_t po = rmw_get_default_publisher_options();
    auto * pub = rmw_create_publisher(
      node, rosidl_typesupport_cpp::get_message_type_support_handle<BasicTypes>(), topic,
      &rmw_qos_profile_default, &po);
    EXPECT_NE(nullptr, pub);
    pubs.push_back(pub);
    return pub;
  }

  const rmw_intraprocess_extension_api_t * api = nullptr;
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_node_t * node = nullptr;
  std::vector<rmw_publisher_t *> pubs;
};

}  // namespace

TEST_F(Tap, VersionNegotiation)
{
  EXPECT_EQ(nullptr, rmw_intraprocess_cpp_get_extension_api(0));
  EXPECT_EQ(
    nullptr, rmw_intraprocess_cpp_get_extension_api(RMW_INTRAPROCESS_EXTENSION_API_VERSION + 1));
  EXPECT_EQ(RMW_INTRAPROCESS_EXTENSION_API_VERSION, api->version);
  EXPECT_EQ(sizeof(rmw_intraprocess_extension_api_t), api->struct_size);
}

TEST_F(Tap, ObservesNativeMessageWithoutSubscription)
{
  auto * pub = make_publisher("/m9_tap");
  Seen seen;
  const uint64_t handle = api->add_tap("/m9_tap", &record, &seen);
  ASSERT_NE(0u, handle);

  BasicTypes m;
  for (int32_t i = 1; i <= 3; ++i) {
    m.int32_value = 100 + i;
    ASSERT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));
  }
  ASSERT_EQ(RMW_RET_OK, api->remove_tap(handle));
  m.int32_value = 999;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));  // not tapped any more

  std::lock_guard<std::mutex> lock(seen.mutex);
  ASSERT_EQ(3u, seen.topics.size());
  EXPECT_EQ((std::vector<int32_t>{101, 102, 103}), seen.int32_values);
  EXPECT_EQ((std::vector<uint64_t>{1, 2, 3}), seen.sequence_numbers);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ("/m9_tap", seen.topics[i]);
    EXPECT_EQ(&m, seen.messages[i]);  // the publisher's own message, not a copy
  }
  // The CDR is what rmw_serialize produces for the same message.
  rmw_serialized_message_t ser = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RMW_RET_OK, rmw_serialized_message_init(&ser, 0, &allocator));
  m.int32_value = 103;
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_serialize(&m, rosidl_typesupport_cpp::get_message_type_support_handle<BasicTypes>(), &ser));
  EXPECT_EQ(std::vector<uint8_t>(ser.buffer, ser.buffer + ser.buffer_length), seen.cdr[2]);
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&ser));
}

TEST_F(Tap, TopicFilterAllTopicsAndSerializedPublish)
{
  auto * a = make_publisher("/m9_a");
  auto * b = make_publisher("/m9_b");
  Seen only_a;
  Seen all;
  const uint64_t ha = api->add_tap("/m9_a", &record, &only_a);
  const uint64_t hall = api->add_tap(nullptr, &record, &all);
  BasicTypes m;
  m.int32_value = 1;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(a, &m, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(b, &m, nullptr));
  // A serialized publish is tapped with its CDR and no native message.
  rmw_serialized_message_t ser = rmw_get_zero_initialized_serialized_message();
  ASSERT_EQ(RMW_RET_OK, rmw_serialized_message_init(&ser, 0, &allocator));
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_serialize(&m, rosidl_typesupport_cpp::get_message_type_support_handle<BasicTypes>(), &ser));
  ASSERT_EQ(RMW_RET_OK, rmw_publish_serialized_message(a, &ser, nullptr));
  EXPECT_EQ(RMW_RET_OK, api->remove_tap(ha));
  EXPECT_EQ(RMW_RET_OK, api->remove_tap(hall));

  std::lock_guard<std::mutex> la(only_a.mutex);
  std::lock_guard<std::mutex> lall(all.mutex);
  EXPECT_EQ((std::vector<std::string>{"/m9_a", "/m9_a"}), only_a.topics);
  EXPECT_EQ((std::vector<std::string>{"/m9_a", "/m9_b", "/m9_a"}), all.topics);
  EXPECT_EQ(1, only_a.null_messages);
  ASSERT_EQ(2u, only_a.cdr.size());
  EXPECT_EQ(std::vector<uint8_t>(ser.buffer, ser.buffer + ser.buffer_length), only_a.cdr[1]);
  EXPECT_EQ(only_a.cdr[0], only_a.cdr[1]);
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&ser));
}

TEST_F(Tap, RemoveInsideOwnCallback)
{
  auto * pub = make_publisher("/m9_self");
  struct Self
  {
    const rmw_intraprocess_extension_api_t * api;
    uint64_t handle = 0;
    int calls = 0;
    rmw_ret_t removed = RMW_RET_ERROR;
  } self{api};
  self.handle = api->add_tap(
    "/m9_self", [](const rmw_intraprocess_tap_sample_t *, void * user_data) {
      auto * s = static_cast<Self *>(user_data);
      ++s->calls;
      s->removed = s->api->remove_tap(s->handle);  // must not wait for itself
    }, &self);
  BasicTypes m;
  ASSERT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));
  EXPECT_EQ(1, self.calls);
  EXPECT_EQ(RMW_RET_OK, self.removed);
}

TEST_F(Tap, RemoveWaitsForCallInProgress)
{
  auto * pub = make_publisher("/m9_wait");
  struct Slow
  {
    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
  } slow;
  const uint64_t handle = api->add_tap(
    "/m9_wait", [](const rmw_intraprocess_tap_sample_t *, void * user_data) {
      auto * s = static_cast<Slow *>(user_data);
      s->entered = true;
      std::this_thread::sleep_for(200ms);
      s->finished = true;
    }, &slow);
  std::thread publisher([pub]() {
      BasicTypes m;
      EXPECT_EQ(RMW_RET_OK, rmw_publish(pub, &m, nullptr));
    });
  while (!slow.entered) {
    std::this_thread::yield();
  }
  EXPECT_EQ(RMW_RET_OK, api->remove_tap(handle));
  // remove_tap returned: the callback is no longer running.
  EXPECT_TRUE(slow.finished);
  publisher.join();
}

TEST_F(Tap, BadArguments)
{
  EXPECT_EQ(0u, api->add_tap("/x", nullptr, nullptr));
  EXPECT_EQ(0u, api->add_tap("relative", &record, nullptr));
  EXPECT_EQ(RMW_RET_ERROR, api->remove_tap(0));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_ERROR, api->remove_tap(123456789));
  rmw_reset_error();
}

TEST_F(Tap, IdentifierBlockLeadsToThisInstance)
{
  // extension.hpp's find_api() reaches the API through the identifier string
  // of the RMW instance in use.
  const char * identifier = rmw_get_implementation_identifier();
  ASSERT_STREQ("rmw_intraprocess_cpp", identifier);
  const auto * block = reinterpret_cast<const rmw_intraprocess_identifier_block_t *>(identifier);
  EXPECT_EQ(RMW_INTRAPROCESS_IDENTIFIER_BLOCK_MAGIC, block->magic);
  ASSERT_NE(nullptr, block->get_extension_api);
  EXPECT_EQ(api, block->get_extension_api(RMW_INTRAPROCESS_EXTENSION_API_VERSION));
}
