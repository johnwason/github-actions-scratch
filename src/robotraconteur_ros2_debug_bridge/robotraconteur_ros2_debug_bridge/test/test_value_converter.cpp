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

// B3: ROS message memory <-> Robot Raconteur values.
//
// Property tests: randomized messages of every interface of the B2 packages
// (messages, service requests/responses, action goals/results/feedback) make
// the round trip C++ -> RR -> C -> RR -> C++ unchanged, also through Robot
// Raconteur's wire serialization. Conformance: for a set of definitions,
// RobotRaconteurGen's generated code unpacks our values and packs them back
// identically. Then the rejection paths.

#include <gtest/gtest.h>

#include <RobotRaconteur.h>

#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "conformance_thunk/robotraconteur_generated.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "type_mapper.hpp"
#include "typesupport.hpp"
#include "value_converter.hpp"

namespace RR = RobotRaconteur;
namespace ti = rosidl_typesupport_introspection_cpp;
using robotraconteur_ros2_debug_bridge::ConversionError;
using robotraconteur_ros2_debug_bridge::DynamicMessage;
using robotraconteur_ros2_debug_bridge::Flavor;
using robotraconteur_ros2_debug_bridge::IntrospectionCache;
using robotraconteur_ros2_debug_bridge::message_struct_type;
using robotraconteur_ros2_debug_bridge::package_interfaces;
using robotraconteur_ros2_debug_bridge::parse_interface_type;
using robotraconteur_ros2_debug_bridge::InterfaceKind;
using robotraconteur_ros2_debug_bridge::ros_to_rr;
using robotraconteur_ros2_debug_bridge::rr_to_ros;
using robotraconteur_ros2_debug_bridge::StructurePtr;

namespace
{

const std::vector<std::string> kPackages = {
  "action_tutorials_interfaces", "example_interfaces", "geometry_msgs", "nav_msgs",
  "sensor_msgs", "std_msgs", "test_msgs", "turtlesim"};

IntrospectionCache & cache()
{
  static IntrospectionCache c;
  return c;
}

// Every message-shaped type of the packages: messages, and the parts of
// services and actions.
std::vector<std::string> all_message_types()
{
  std::vector<std::string> out;
  for (const auto & pkg : kPackages) {
    for (const auto & t : package_interfaces(pkg)) {
      const auto it = parse_interface_type(t);
      switch (it.kind) {
        case InterfaceKind::Message:
          out.push_back(t);
          break;
        case InterfaceKind::Service:
          out.push_back(t + "_Request");
          out.push_back(t + "_Response");
          break;
        case InterfaceKind::Action:
          out.push_back(t + "_Goal");
          out.push_back(t + "_Result");
          out.push_back(t + "_Feedback");
          break;
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Random C++ messages and deep comparison (independent of the converter).

class Random
{
public:
  explicit Random(uint64_t seed)
  : g_(seed) {}
  uint64_t bits() {return g_();}
  size_t below(size_t n) {return n == 0 ? 0 : static_cast<size_t>(g_() % n);}

  std::string bytes(size_t max)
  {
    std::string s(below(max + 1), '\0');
    for (auto & c : s) {
      c = static_cast<char>(bits() & 0xFF);
    }
    return s;
  }

  // Valid UTF-16 (BMP characters and surrogate pairs), at most `max` units.
  std::u16string utf16(size_t max)
  {
    std::u16string s;
    const size_t n = below(max + 1);
    while (s.size() < n) {
      if (n - s.size() >= 2 && below(4) == 0) {
        const uint32_t cp = 0x10000 + static_cast<uint32_t>(below(0x100000));
        s.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
        s.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
      } else {
        uint32_t u = static_cast<uint32_t>(below(0xFFFF)) + 1;
        if (u >= 0xD800 && u <= 0xDFFF) {
          u -= 0x800;
        }
        s.push_back(static_cast<char16_t>(u));
      }
    }
    return s;
  }

private:
  std::mt19937_64 g_;
};

size_t scalar_size(uint8_t t)
{
  switch (t) {
    case ti::ROS_TYPE_FLOAT: return 4;
    case ti::ROS_TYPE_DOUBLE: return 8;
    case ti::ROS_TYPE_CHAR: case ti::ROS_TYPE_OCTET: case ti::ROS_TYPE_UINT8:
    case ti::ROS_TYPE_INT8: return 1;
    case ti::ROS_TYPE_WCHAR: case ti::ROS_TYPE_UINT16: case ti::ROS_TYPE_INT16: return 2;
    case ti::ROS_TYPE_UINT32: case ti::ROS_TYPE_INT32: return 4;
    case ti::ROS_TYPE_UINT64: case ti::ROS_TYPE_INT64: return 8;
    default: return 0;
  }
}

bool placeholder(const ti::MessageMembers * m)
{
  return m->member_count_ == 1 &&
         std::string(m->members_[0].name_) == "structure_needs_at_least_one_member";
}

void fill(const ti::MessageMembers * m, void * msg, Random & r, int depth = 0);

void fill_element(const ti::MessageMember & f, void * p, Random & r, int depth)
{
  switch (f.type_id_) {
    case ti::ROS_TYPE_MESSAGE:
      fill(static_cast<const ti::MessageMembers *>(f.members_->data), p, r, depth + 1);
      break;
    case ti::ROS_TYPE_STRING:
      *static_cast<std::string *>(p) =
        r.bytes(f.string_upper_bound_ ? f.string_upper_bound_ : 12);
      break;
    case ti::ROS_TYPE_WSTRING:
      *static_cast<std::u16string *>(p) =
        r.utf16(f.string_upper_bound_ ? f.string_upper_bound_ : 12);
      break;
    default: {
        const uint64_t v = r.bits();
        std::memcpy(p, &v, scalar_size(f.type_id_));
      }
  }
}

void fill(const ti::MessageMembers * m, void * msg, Random & r, int depth)
{
  if (placeholder(m)) {
    return;  // an empty message: no fields
  }
  for (uint32_t i = 0; i < m->member_count_; ++i) {
    const auto & f = m->members_[i];
    void * field = static_cast<uint8_t *>(msg) + f.offset_;
    if (!f.is_array_) {
      if (f.type_id_ == ti::ROS_TYPE_BOOLEAN) {
        *static_cast<bool *>(field) = (r.bits() & 1) != 0;
      } else {
        fill_element(f, field, r, depth);
      }
      continue;
    }
    const bool fixed = !f.is_upper_bound_ && f.array_size_ > 0;
    size_t n = f.array_size_;
    if (!fixed) {
      const size_t max = depth > 2 ? 1 : 4;
      n = r.below((f.is_upper_bound_ ? std::min(f.array_size_, max) : max) + 1);
      f.resize_function(field, n);
    }
    for (size_t k = 0; k < n; ++k) {
      if (f.type_id_ == ti::ROS_TYPE_BOOLEAN) {
        const bool b = (r.bits() & 1) != 0;
        f.assign_function(field, k, &b);
      } else {
        fill_element(f, f.get_function(field, k), r, depth);
      }
    }
  }
}

bool equal(
  const ti::MessageMembers * m, const void * a, const void * b, std::string & why,
  const std::string & path = "");

bool element_equal(
  const ti::MessageMember & f, const void * a, const void * b, std::string & why,
  const std::string & path)
{
  switch (f.type_id_) {
    case ti::ROS_TYPE_MESSAGE:
      return equal(static_cast<const ti::MessageMembers *>(f.members_->data), a, b, why, path);
    case ti::ROS_TYPE_STRING:
      if (*static_cast<const std::string *>(a) == *static_cast<const std::string *>(b)) {
        return true;
      }
      break;
    case ti::ROS_TYPE_WSTRING:
      if (*static_cast<const std::u16string *>(a) == *static_cast<const std::u16string *>(b)) {
        return true;
      }
      break;
    default:
      if (std::memcmp(a, b, scalar_size(f.type_id_)) == 0) {
        return true;
      }
  }
  why = path + " differs";
  return false;
}

bool equal(
  const ti::MessageMembers * m, const void * a, const void * b, std::string & why,
  const std::string & path)
{
  if (placeholder(m)) {
    return true;  // no fields
  }
  for (uint32_t i = 0; i < m->member_count_; ++i) {
    const auto & f = m->members_[i];
    const std::string p = path + "." + f.name_;
    const void * fa = static_cast<const uint8_t *>(a) + f.offset_;
    const void * fb = static_cast<const uint8_t *>(b) + f.offset_;
    if (!f.is_array_) {
      if (f.type_id_ == ti::ROS_TYPE_BOOLEAN) {
        if (*static_cast<const bool *>(fa) != *static_cast<const bool *>(fb)) {
          why = p + " differs";
          return false;
        }
      } else if (!element_equal(f, fa, fb, why, p)) {
        return false;
      }
      continue;
    }
    const size_t na = f.size_function(fa);
    if (na != f.size_function(fb)) {
      why = p + " size differs";
      return false;
    }
    for (size_t k = 0; k < na; ++k) {
      const std::string pk = p + "[" + std::to_string(k) + "]";
      if (f.type_id_ == ti::ROS_TYPE_BOOLEAN) {
        bool ba = false;
        bool bb = false;
        f.fetch_function(fa, k, &ba);
        f.fetch_function(fb, k, &bb);
        if (ba != bb) {
          why = pk + " differs";
          return false;
        }
      } else {
        const void * ea = f.get_const_function(fa, k);
        const void * eb = f.get_const_function(fb, k);
        if (!element_equal(f, ea, eb, why, pk)) {
          return false;
        }
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Robot Raconteur value trees.

// ElementFlags is not compared (it depends on how an element was built).
bool rr_equal(
  const RR::MessageElementPtr & a, const RR::MessageElementPtr & b, std::string & why,
  const std::string & path);

bool rr_list_equal(
  const StructurePtr & a, const StructurePtr & b, std::string & why,
  const std::string & path)
{
  if (a->Type != b->Type || a->TypeName.str() != b->TypeName.str()) {
    why = path + ": nested type differs (" + a->TypeName.str().to_string() + " vs " +
      b->TypeName.str().to_string() + ")";
    return false;
  }
  if (a->Elements.size() != b->Elements.size()) {
    why = path + ": element count differs";
    return false;
  }
  for (size_t i = 0; i < a->Elements.size(); ++i) {
    if (!rr_equal(a->Elements[i], b->Elements[i], why, path)) {
      return false;
    }
  }
  return true;
}

// ElementFlags is not compared (it depends on how an element was built).
bool rr_equal(
  const RR::MessageElementPtr & a, const RR::MessageElementPtr & b, std::string & why,
  const std::string & path)
{
  const std::string p = path + "/" + a->ElementName.str().to_string() + "#" +
    std::to_string(a->ElementNumber);
  if (a->ElementName.str() != b->ElementName.str() || a->ElementNumber != b->ElementNumber ||
    a->ElementType != b->ElementType)
  {
    why = p + ": element header differs (" + b->ElementName.str().to_string() + "#" +
      std::to_string(b->ElementNumber) + " type " + std::to_string(a->ElementType) + "/" +
      std::to_string(b->ElementType) + " flags " + std::to_string(a->ElementFlags) + "/" +
      std::to_string(b->ElementFlags) + ")";
    return false;
  }
  if (a->ElementType == RR::DataTypes_structure_t || a->ElementType == RR::DataTypes_list_t) {
    return rr_list_equal(a->CastDataToNestedList(), b->CastDataToNestedList(), why, p);
  }
  if (a->ElementType == RR::DataTypes_void_t) {
    return true;
  }
  auto aa = a->CastData<RR::RRBaseArray>();
  auto ab = b->CastData<RR::RRBaseArray>();
  if (aa->size() != ab->size() || aa->ElementSize() != ab->ElementSize() ||
    std::memcmp(aa->void_ptr(), ab->void_ptr(), aa->size() * aa->ElementSize()) != 0)
  {
    why = p + ": data differs";
    return false;
  }
  return true;
}

bool rr_equal(const StructurePtr & a, const StructurePtr & b, std::string & why)
{
  return rr_list_equal(a, b, why, "");
}

// Through Robot Raconteur's wire format and back.
StructurePtr through_wire(const StructurePtr & s)
{
  auto m = RR::CreateMessage();
  m->header = RR::CreateMessageHeader();
  auto entry = RR::CreateMessageEntry(RR::MessageEntryType_PropertyGetRes, "value");
  entry->AddElement("value", s);
  m->entries.push_back(entry);
  const uint32_t size = m->ComputeSize();
  std::vector<uint8_t> buf(size);
  RR::ArrayBinaryWriter w(buf.data(), 0, size);
  m->Write(w);
  RR::ArrayBinaryReader r(buf.data(), 0, size);
  auto m2 = RR::CreateMessage();
  m2->Read(r);
  return m2->entries.at(0)->FindElement("value")->CastDataToNestedList();
}

}  // namespace

// AC: property-style round trips for all B2 packages, both introspection
// flavors.
TEST(ValueConverter, RandomRoundTripsAllPackages)
{
  const auto types = all_message_types();
  ASSERT_GT(types.size(), 150u);
  Random r(20261114);
  size_t checked = 0;
  for (const auto & type : types) {
    const auto * cpp = cache().message(Flavor::Cpp, type);
    const auto * c = cache().message(Flavor::C, type);
    const auto * members = static_cast<const ti::MessageMembers *>(cpp);
    for (int iter = 0; iter < 8; ++iter) {
      DynamicMessage original(Flavor::Cpp, cpp);
      fill(members, original.data(), r);

      const StructurePtr rr1 = ros_to_rr(Flavor::Cpp, cpp, original.data());
      DynamicMessage as_c(Flavor::C, c);
      // Message::Write rewrites list numbers into names in place: send a copy.
      const StructurePtr wire = through_wire(ros_to_rr(Flavor::Cpp, cpp, original.data()));
      ASSERT_NO_THROW(rr_to_ros(Flavor::C, c, wire, as_c.data())) << type;
      const StructurePtr rr2 = ros_to_rr(Flavor::C, c, as_c.data());
      DynamicMessage back(Flavor::Cpp, cpp);
      ASSERT_NO_THROW(rr_to_ros(Flavor::Cpp, cpp, rr2, back.data())) << type;

      std::string why;
      ASSERT_TRUE(equal(members, original.data(), back.data(), why)) << type << ": " << why;
      ASSERT_TRUE(rr_equal(rr1, rr2, why)) << type << ": " << why;
      ++checked;
    }
  }
  EXPECT_GT(checked, 1200u);
}

TEST(ValueConverter, StructTypeNames)
{
  EXPECT_EQ(
    message_struct_type(Flavor::Cpp, cache().message(Flavor::Cpp, "std_msgs/msg/Header")),
    "rosmsg_std_msgs__Header.Header");
  EXPECT_EQ(
    message_struct_type(Flavor::C, cache().message(Flavor::C, "std_msgs/msg/Header")),
    "rosmsg_std_msgs__Header.Header");
  // Field names are fixed like in the definitions.
  DynamicMessage m(Flavor::Cpp, cache().message(Flavor::Cpp, "sensor_msgs/msg/NavSatStatus"));
  const auto s = ros_to_rr(Flavor::Cpp, m.members(), m.data());
  RR::MessageElementPtr e;
  EXPECT_TRUE(RR::MessageElement::TryFindElement(s->Elements, "rosservice", e));
  EXPECT_TRUE(RR::MessageElement::TryFindElement(s->Elements, "status", e));
}

// The generated code of these definitions (test/CMakeLists conformance
// thunk) is the reference packing.
TEST(ValueConverter, MatchesRobotRaconteurGeneratedCode)
{
  auto node = RR_MAKE_SHARED<RR::RobotRaconteurNode>();
  node->Init();
  const std::vector<RR_SHARED_PTR<RR::ServiceFactory>> factories = ROBOTRACONTEUR_SERVICE_TYPES;
  for (const auto & f : factories) {
    node->RegisterServiceType(f);
  }
  const std::vector<std::string> types = {
    "test_msgs/msg/Arrays", "test_msgs/msg/BasicTypes", "test_msgs/msg/BoundedPlainSequences",
    "test_msgs/msg/BoundedSequences", "test_msgs/msg/Builtins", "test_msgs/msg/Constants",
    "test_msgs/msg/Defaults", "test_msgs/msg/Empty", "test_msgs/msg/MultiNested",
    "test_msgs/msg/Nested", "test_msgs/msg/Strings", "test_msgs/msg/UnboundedSequences",
    "test_msgs/msg/WStrings", "sensor_msgs/msg/JointState", "nav_msgs/msg/Odometry"};
  Random r(7);
  for (const auto & type : types) {
    const auto * cpp = cache().message(Flavor::Cpp, type);
    for (int iter = 0; iter < 20; ++iter) {
      DynamicMessage original(Flavor::Cpp, cpp);
      fill(static_cast<const ti::MessageMembers *>(cpp), original.data(), r);
      const StructurePtr ours = ros_to_rr(Flavor::Cpp, cpp, original.data());
      RR_INTRUSIVE_PTR<RR::RRStructure> generated;
      ASSERT_NO_THROW(generated = RR::rr_cast<RR::RRStructure>(node->UnpackStructure(ours)))
        << type;
      const StructurePtr repacked = node->PackStructure(generated);
      std::string why;
      ASSERT_TRUE(rr_equal(ours, repacked, why)) << type << ": " << why;
      DynamicMessage back(Flavor::Cpp, cpp);
      ASSERT_NO_THROW(rr_to_ros(Flavor::Cpp, cpp, repacked, back.data())) << type;
      ASSERT_TRUE(equal(static_cast<const ti::MessageMembers *>(cpp), original.data(),
        back.data(), why)) << type << ": " << why;
    }
  }
  node->Shutdown();
}

// ---------------------------------------------------------------------------
// Rejection paths (writes into ROS), for both flavors.

namespace
{

RR::MessageElementPtr find(const StructurePtr & s, const std::string & name)
{
  return RR::MessageElement::FindElement(s->Elements, name);
}

// A valid value of `type` with field `name` replaced.
StructurePtr with_field(
  Flavor flavor, const std::string & type, const std::string & name,
  const RR::MessageElementDataPtr & data)
{
  DynamicMessage m(flavor, cache().message(flavor, type));
  auto s = ros_to_rr(flavor, m.members(), m.data());
  find(s, name)->SetData(data);
  return s;
}

void expect_rejected(
  Flavor flavor, const std::string & type, const StructurePtr & value, const std::string & needle)
{
  DynamicMessage m(flavor, cache().message(flavor, type));
  try {
    rr_to_ros(flavor, m.members(), value, m.data());
    ADD_FAILURE() << type << ": accepted, expected an error containing '" << needle << "'";
  } catch (const ConversionError & e) {
    EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << e.what();
  }
}

class Rejection : public ::testing::TestWithParam<Flavor> {};

RR::MessageElementDataPtr strings(const std::vector<std::string> & v)
{
  std::vector<RR::MessageElementPtr> items;
  for (size_t i = 0; i < v.size(); ++i) {
    items.push_back(RR::CreateMessageElement(static_cast<int32_t>(i), RR::stringToRRArray(v[i])));
  }
  return RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(items));
}

}  // namespace

TEST_P(Rejection, BoundedStringTooLong)
{
  const Flavor fl = GetParam();
  // bounded_string_value is string<=22.
  expect_rejected(
    fl, "test_msgs/msg/Strings",
    with_field(fl, "test_msgs/msg/Strings", "bounded_string_value",
    RR::stringToRRArray(std::string(23, 'x'))),
    "bounded_string_value': at most 22");
  DynamicMessage ok(fl, cache().message(fl, "test_msgs/msg/Strings"));
  EXPECT_NO_THROW(
    rr_to_ros(
      fl, ok.members(),
      with_field(fl, "test_msgs/msg/Strings", "bounded_string_value",
      RR::stringToRRArray(std::string(22, 'x'))), ok.data()));
}

TEST_P(Rejection, BoundedSequenceTooLong)
{
  const Flavor fl = GetParam();
  expect_rejected(
    fl, "test_msgs/msg/BoundedSequences",
    with_field(fl, "test_msgs/msg/BoundedSequences", "int32_values",
    RR::AttachRRArrayCopy<int32_t>(std::vector<int32_t>{1, 2, 3, 4}.data(), 4)),
    "int32_values': at most 3 elements, got 4");
  expect_rejected(
    fl, "test_msgs/msg/BoundedSequences",
    with_field(fl, "test_msgs/msg/BoundedSequences", "string_values",
    strings({"a", "b", "c", "d"})),
    "string_values': at most 3 elements");
}

TEST_P(Rejection, FixedArrayLength)
{
  const Flavor fl = GetParam();
  expect_rejected(
    fl, "test_msgs/msg/Arrays",
    with_field(fl, "test_msgs/msg/Arrays", "float64_values",
    RR::AttachRRArrayCopy<double>(std::vector<double>{1, 2}.data(), 2)),
    "float64_values': fixed length 3, got 2");
  expect_rejected(
    fl, "test_msgs/msg/Arrays",
    with_field(fl, "test_msgs/msg/Arrays", "string_values", strings({"a"})),
    "string_values': fixed length 3, got 1");
}

TEST_P(Rejection, WrongTypes)
{
  const Flavor fl = GetParam();
  expect_rejected(
    fl, "test_msgs/msg/BasicTypes",
    with_field(fl, "test_msgs/msg/BasicTypes", "float64_value", RR::ScalarToRRArray<int32_t>(1)),
    "float64_value': expected double, got int32");
  expect_rejected(
    fl, "test_msgs/msg/BasicTypes",
    with_field(fl, "test_msgs/msg/BasicTypes", "int32_value",
    RR::AttachRRArrayCopy<int32_t>(std::vector<int32_t>{1, 2}.data(), 2)),
    "int32_value': expected a scalar, got 2 values");
  expect_rejected(
    fl, "test_msgs/msg/BasicTypes",
    with_field(fl, "test_msgs/msg/BasicTypes", "bool_value", RR::stringToRRArray("x")),
    "bool_value': expected bool, got string");
  // A nested structure of another type.
  DynamicMessage t(fl, cache().message(fl, "builtin_interfaces/msg/Duration"));
  expect_rejected(
    fl, "std_msgs/msg/Header",
    with_field(fl, "std_msgs/msg/Header", "stamp", ros_to_rr(fl, t.members(), t.data())),
    "stamp': expected rosmsg_builtin_interfaces__Time.Time, got "
    "rosmsg_builtin_interfaces__Duration.Duration");
  // The top-level structure type.
  DynamicMessage h(fl, cache().message(fl, "std_msgs/msg/Header"));
  expect_rejected(
    fl, "builtin_interfaces/msg/Time", ros_to_rr(fl, h.members(), h.data()),
    "expected rosmsg_builtin_interfaces__Time.Time, got rosmsg_std_msgs__Header.Header");
}

TEST_P(Rejection, InvalidListAndUtf8)
{
  const Flavor fl = GetParam();
  std::vector<RR::MessageElementPtr> items;
  items.push_back(RR::CreateMessageElement(1, RR::stringToRRArray("a")));  // should be 0
  expect_rejected(
    fl, "test_msgs/msg/UnboundedSequences",
    with_field(fl, "test_msgs/msg/UnboundedSequences", "string_values",
    RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(items))),
    "invalid list");
  for (const std::string & bad : {std::string("\xC3"), std::string("\xC0\x80"),
      std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"), std::string("\xFF")})
  {
    expect_rejected(
      fl, "test_msgs/msg/WStrings",
      with_field(fl, "test_msgs/msg/WStrings", "wstring_value", RR::stringToRRArray(bad)),
      "wstring_value': invalid UTF-8");
  }
}

TEST_P(Rejection, MissingAndNullKeepDefaults)
{
  const Flavor fl = GetParam();
  const auto * members = cache().message(fl, "test_msgs/msg/Defaults");
  DynamicMessage defaults(fl, members);
  const auto expected = ros_to_rr(fl, members, defaults.data());
  // An empty structure of the right type: every field missing.
  DynamicMessage m(fl, members);
  rr_to_ros(
    fl, members,
    RR::CreateMessageElementNestedElementList(
      RR::DataTypes_structure_t, "rosmsg_test_msgs__Defaults.Defaults",
      std::vector<RR::MessageElementPtr>()),
    m.data());
  std::string why;
  EXPECT_TRUE(rr_equal(expected, ros_to_rr(fl, members, m.data()), why)) << why;
  // A null nested structure.
  const auto * hm = cache().message(fl, "std_msgs/msg/Header");
  DynamicMessage h(fl, hm);
  auto value = ros_to_rr(fl, hm, h.data());
  find(value, "stamp")->SetData(RR::MessageElementDataPtr());
  EXPECT_NO_THROW(rr_to_ros(fl, hm, value, h.data()));
}

INSTANTIATE_TEST_SUITE_P(
  Flavors, Rejection, ::testing::Values(Flavor::Cpp, Flavor::C),
  [](const ::testing::TestParamInfo<Flavor> & i) {
    return i.param == Flavor::Cpp ? std::string("Cpp") : std::string("C");
  });

TEST(ValueConverter, LoneSurrogatesBecomeReplacementCharacters)
{
  const auto * members = cache().message(Flavor::Cpp, "test_msgs/msg/WStrings");
  DynamicMessage m(Flavor::Cpp, members);
  const auto * mm = static_cast<const ti::MessageMembers *>(members);
  for (uint32_t i = 0; i < mm->member_count_; ++i) {
    if (std::string(mm->members_[i].name_) == "wstring_value") {
      *reinterpret_cast<std::u16string *>(static_cast<uint8_t *>(m.data()) +
      mm->members_[i].offset_) = std::u16string{u'a', static_cast<char16_t>(0xD800), u'b'};
    }
  }
  const auto s = ros_to_rr(Flavor::Cpp, members, m.data());
  EXPECT_EQ(RR::RRArrayToString(find(s, "wstring_value")->CastData<RR::RRArray<char>>()),
    "a\xEF\xBF\xBD" "b");
}
