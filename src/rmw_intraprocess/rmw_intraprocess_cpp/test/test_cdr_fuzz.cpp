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

// M10 hardening: malformed CDR must never crash the RMW. Serialized data can
// come from outside the process (a gateway forwarding serialized messages,
// rmw_publish_serialized_message), so rmw_deserialize and rmw_take of such data
// must fail cleanly: truncations, bit flips, random bytes and huge sequence or
// string lengths are fed to rmw_deserialize for several message types
// (deterministic seeds). Run under ASan/UBSan in the gate.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "rcutils/allocator.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/serialized_message.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "test_msgs/msg/arrays.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/msg/bounded_sequences.hpp"
#include "test_msgs/msg/multi_nested.hpp"
#include "test_msgs/msg/strings.hpp"
#include "test_msgs/msg/unbounded_sequences.hpp"
#include "test_msgs/msg/w_strings.hpp"

namespace
{

constexpr int kIterations = 3000;

template<class MsgT>
std::vector<uint8_t> serialize(const MsgT & msg)
{
  auto allocator = rcutils_get_default_allocator();
  rmw_serialized_message_t ser = rmw_get_zero_initialized_serialized_message();
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_init(&ser, 0, &allocator));
  EXPECT_EQ(
    RMW_RET_OK,
    rmw_serialize(&msg, rosidl_typesupport_cpp::get_message_type_support_handle<MsgT>(), &ser));
  std::vector<uint8_t> out(ser.buffer, ser.buffer + ser.buffer_length);
  EXPECT_EQ(RMW_RET_OK, rmw_serialized_message_fini(&ser));
  return out;
}

/// Deserializes `data` into a fresh message; returns whether it was accepted.
/// Either outcome is fine; crashing, leaking or tripping a sanitizer is not.
template<class MsgT>
bool try_deserialize(const std::vector<uint8_t> & data)
{
  rmw_serialized_message_t ser = rmw_get_zero_initialized_serialized_message();
  ser.buffer = const_cast<uint8_t *>(data.data());
  ser.buffer_length = data.size();
  ser.buffer_capacity = data.size();
  ser.allocator = rcutils_get_default_allocator();
  MsgT msg;
  const rmw_ret_t ret = rmw_deserialize(
    &ser, rosidl_typesupport_cpp::get_message_type_support_handle<MsgT>(), &msg);
  rmw_reset_error();
  EXPECT_TRUE(ret == RMW_RET_OK || ret == RMW_RET_ERROR || ret == RMW_RET_BAD_ALLOC) << ret;
  return ret == RMW_RET_OK;
}

/// Mutates a valid buffer: truncation, bit flips, 32-bit length fields set to
/// huge values, or random bytes.
std::vector<uint8_t> mutate(
  const std::vector<uint8_t> & valid, std::mt19937 & rng, bool huge_lengths)
{
  std::vector<uint8_t> d = valid;
  auto kind = static_cast<uint32_t>(rng() % 5);
  if (!huge_lengths) {
    // Only mutations that keep length fields intact (see the WStrings test).
    kind = kind % 2 == 0 ? 0 : 4;
  }
  switch (kind) {
    case 0:  // truncation
      d.resize(d.empty() ? 0 : rng() % d.size());
      break;
    case 1:  // bit flips
      for (int i = 0; i < 1 + static_cast<int>(rng() % 8) && !d.empty(); ++i) {
        d[rng() % d.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
      }
      break;
    case 2:  // a huge 32-bit value (sequence/string length) at an aligned offset
      if (d.size() >= 8) {
        const size_t off = 4 + 4 * (rng() % ((d.size() - 4) / 4));
        const uint32_t huge = rng() % 2 ? 0xFFFFFFFFu : 0x7FFFFFF0u;
        if (off + 4 <= d.size()) {
          std::memcpy(&d[off], &huge, 4);
        }
      }
      break;
    case 3:  // random bytes after a valid encapsulation header
      d.resize(rng() % (valid.size() + 64));
      for (size_t i = 4; i < d.size(); ++i) {
        d[i] = static_cast<uint8_t>(rng());
      }
      break;
    default:  // append garbage
      for (int i = 0; i < static_cast<int>(rng() % 64); ++i) {
        d.push_back(static_cast<uint8_t>(rng()));
      }
      break;
  }
  return d;
}

template<class MsgT>
void fuzz(const MsgT & sample, uint32_t seed, bool huge_lengths = true)
{
  const auto valid = serialize(sample);
  ASSERT_TRUE(try_deserialize<MsgT>(valid)) << "the valid buffer must deserialize";
  std::mt19937 rng(seed);
  int accepted = 0;
  for (int i = 0; i < kIterations; ++i) {
    if (try_deserialize<MsgT>(mutate(valid, rng, huge_lengths))) {
      ++accepted;
    }
  }
  // Nothing to assert about the split; record it for the log.
  ::testing::Test::RecordProperty("accepted", accepted);
}

}  // namespace

TEST(CdrFuzz, BasicTypes)
{
  test_msgs::msg::BasicTypes m;
  m.int64_value = 42;
  m.float64_value = 1.5;
  fuzz(m, 1);
}

TEST(CdrFuzz, Strings)
{
  test_msgs::msg::Strings m;
  m.string_value = "hello torture";
  m.bounded_string_value = "bounded";
  fuzz(m, 2);
}

TEST(CdrFuzz, WStrings)
{
  test_msgs::msg::WStrings m;
  m.wstring_value = u"wide";
  // No huge length fields: rosidl_typesupport_fastrtps resizes a wstring to its
  // declared length before checking the remaining buffer (up to 8 GiB per
  // field; upstream defect, docs/unsupported.md); bit flips and random bytes
  // can produce such a length too. Truncations and appended garbage still run.
  fuzz(m, 3, false);
}

TEST(CdrFuzz, UnboundedSequences)
{
  test_msgs::msg::UnboundedSequences m;
  m.int32_values = {1, 2, 3, 4};
  m.string_values = {"a", "bb", "ccc"};
  m.basic_types_values.resize(2);
  fuzz(m, 4);
}

TEST(CdrFuzz, BoundedSequences)
{
  test_msgs::msg::BoundedSequences m;
  m.int32_values = {1, 2};
  m.string_values = {"x"};
  fuzz(m, 5);
}

TEST(CdrFuzz, ArraysAndNested)
{
  fuzz(test_msgs::msg::Arrays(), 6);
  fuzz(test_msgs::msg::MultiNested(), 7);
}

TEST(CdrFuzz, PureRandomAndEmpty)
{
  std::mt19937 rng(8);
  EXPECT_FALSE(try_deserialize<test_msgs::msg::Strings>({}));
  for (int i = 0; i < kIterations; ++i) {
    std::vector<uint8_t> d(rng() % 256);
    for (auto & b : d) {
      b = static_cast<uint8_t>(rng());
    }
    try_deserialize<test_msgs::msg::UnboundedSequences>(d);
  }
}
