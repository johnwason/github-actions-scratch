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

// B2: ROS interface types -> Robot Raconteur definitions.
//
// Golden files: test/golden/<definition>.robdef, for every interface of the
// golden packages and everything they import. Regenerate with
// scripts/update-golden.sh after an intended change, and review the diff.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "type_mapper.hpp"

using robotraconteur_ros2_debug_bridge::fixname;
using robotraconteur_ros2_debug_bridge::interface_packages;
using robotraconteur_ros2_debug_bridge::InterfaceKind;
using robotraconteur_ros2_debug_bridge::package_interfaces;
using robotraconteur_ros2_debug_bridge::parse_interface_type;
using robotraconteur_ros2_debug_bridge::TypeMapper;
using robotraconteur_ros2_debug_bridge::TypeMapperError;
using robotraconteur_ros2_debug_bridge::verify_definitions;

namespace
{

const std::vector<std::string> kGoldenPackages = {
  "action_tutorials_interfaces", "example_interfaces", "geometry_msgs", "nav_msgs",
  "sensor_msgs", "std_msgs", "test_msgs", "turtlesim"};

std::string read_file(const std::filesystem::path & p)
{
  std::ifstream f(p, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

std::vector<std::string> texts_of(const TypeMapper & m, const std::string & root)
{
  std::vector<std::string> texts;
  for (const auto & n : m.closure(root)) {
    texts.push_back(m.definition(n));
  }
  return texts;
}

}  // namespace

TEST(Fixname, UpstreamReservedWordsAndPrefixes)
{
  for (const char * w : {"object", "end", "service", "struct", "field", "pipe", "wire",
      "callback", "memory", "void", "int8", "uint64", "single", "double", "varvalue",
      "varobject", "exception", "using", "import", "as"})
  {
    EXPECT_EQ(fixname(w), std::string("ros") + w) << w;
  }
  EXPECT_EQ(fixname("rr_thing"), "rosrr_thing");
  EXPECT_EQ(fixname("RRthing"), "rosRRthing");
  EXPECT_EQ(fixname("RobotRaconteurX"), "rosRobotRaconteurX");
}

TEST(Fixname, NamesRobotRaconteurRejectsBeyondUpstream)
{
  for (const char * w : {"string", "bool", "constant", "enum", "pod", "namedarray",
      "cdouble", "csingle", "stdver", "this", "self", "Me"})
  {
    EXPECT_EQ(fixname(w), std::string("ros") + w) << w;
  }
  EXPECT_EQ(fixname("get_x"), "rosget_x");
  EXPECT_EQ(fixname("Set_y"), "rosSet_y");
  EXPECT_EQ(fixname("async_z"), "rosasync_z");
}

TEST(Fixname, OrdinaryNamesUnchanged)
{
  for (const char * w : {"x", "data", "header", "position_covariance", "status", "me",
      "strings", "objects", "getter", "settings", "error"})
  {
    EXPECT_EQ(fixname(w), w);
  }
}

TEST(InterfaceType, Forms)
{
  auto t = parse_interface_type("std_msgs/msg/String");
  EXPECT_EQ(t.package, "std_msgs");
  EXPECT_EQ(t.name, "String");
  EXPECT_EQ(t.kind, InterfaceKind::Message);
  EXPECT_EQ(t.full_name(), "std_msgs/msg/String");
  t = parse_interface_type("example_interfaces/AddTwoInts", InterfaceKind::Service);
  EXPECT_EQ(t.full_name(), "example_interfaces/srv/AddTwoInts");
  EXPECT_EQ(
    parse_interface_type("action_tutorials_interfaces/action/Fibonacci").kind,
    InterfaceKind::Action);
}

TEST(InterfaceType, Rejected)
{
  EXPECT_THROW(parse_interface_type("std_msgs/String"), TypeMapperError);  // kind needed
  EXPECT_THROW(parse_interface_type("std_msgs/msg/string"), TypeMapperError);
  EXPECT_THROW(parse_interface_type("std_msgs/foo/String"), TypeMapperError);
  EXPECT_THROW(parse_interface_type("std_msgs/msg/String/x"), TypeMapperError);
  EXPECT_THROW(parse_interface_type("../std_msgs/msg/String"), TypeMapperError);
  EXPECT_THROW(parse_interface_type(""), TypeMapperError);
  EXPECT_THROW(
    parse_interface_type("std_msgs/msg/String", InterfaceKind::Service), TypeMapperError);
}

TEST(TypeMapper, UnknownTypesThrow)
{
  TypeMapper m;
  EXPECT_THROW(m.message("std_msgs/msg/NoSuchType"), TypeMapperError);
  EXPECT_THROW(m.message("no_such_package_xyz/msg/Foo"), TypeMapperError);
  EXPECT_THROW(m.service("example_interfaces/srv/NoSuch"), TypeMapperError);
  EXPECT_THROW(m.action("example_interfaces/action/NoSuch"), TypeMapperError);
  EXPECT_THROW(m.definition("rosmsg_std_msgs__String"), TypeMapperError);  // not generated
  EXPECT_THROW(package_interfaces("no_such_package_xyz"), TypeMapperError);
  // The mapper still works after failures.
  EXPECT_EQ(m.message("std_msgs/msg/String"), "rosmsg_std_msgs__String");
}

TEST(TypeMapper, ClosureListsImportsFirst)
{
  TypeMapper m;
  const auto root = m.topic("nav_msgs/msg/Odometry");
  EXPECT_EQ(root, "rostopic_nav_msgs__Odometry");
  const auto c = m.closure(root);
  ASSERT_FALSE(c.empty());
  EXPECT_EQ(c.back(), root);
  auto pos = [&](const std::string & n) {
      return std::find(c.begin(), c.end(), n) - c.begin();
    };
  EXPECT_LT(pos("rosmsg_builtin_interfaces__Time"), pos("rosmsg_std_msgs__Header"));
  EXPECT_LT(pos("rosmsg_std_msgs__Header"), pos("rosmsg_nav_msgs__Odometry"));
  EXPECT_LT(pos("rosmsg_geometry_msgs__Point"), pos("rosmsg_geometry_msgs__Pose"));
  EXPECT_LT(pos("rosmsg_nav_msgs__Odometry"), pos(root));
  EXPECT_EQ(std::set<std::string>(c.begin(), c.end()).size(), c.size());
}

TEST(TypeMapper, ConstantsAndTypeEdges)
{
  TypeMapper m;
  m.message("test_msgs/msg/Constants");
  const auto constants = m.definition("rosmsg_test_msgs__Constants");
  // 8-bit and bool constants are widened (docs/upstream_issues.md).
  EXPECT_NE(constants.find("constant int16 INT8_CONST -50\n"), std::string::npos);
  EXPECT_NE(constants.find("constant uint16 UINT8_CONST 200\n"), std::string::npos);
  EXPECT_NE(constants.find("constant uint16 BOOL_CONST 1\n"), std::string::npos);
  EXPECT_NE(constants.find("constant single FLOAT32_CONST 1.125\n"), std::string::npos);
  EXPECT_NE(constants.find("constant uint64 UINT64_CONST 50000000\n"), std::string::npos);
  // The placeholder member rosidl gives an empty message is not a field.
  EXPECT_EQ(constants.find("structure_needs_at_least_one_member"), std::string::npos);

  m.message("test_msgs/msg/BoundedSequences");
  const auto bounded = m.definition("rosmsg_test_msgs__BoundedSequences");
  EXPECT_NE(bounded.find("field bool[] bool_values\n"), std::string::npos);
  EXPECT_NE(bounded.find("field uint8[] byte_values\n"), std::string::npos);
  EXPECT_NE(bounded.find("field uint8[] char_values\n"), std::string::npos);
  EXPECT_NE(bounded.find("field single[] float32_values\n"), std::string::npos);
  EXPECT_NE(bounded.find("field string{list} string_values\n"), std::string::npos);
  EXPECT_NE(
    bounded.find("field rosmsg_test_msgs__BasicTypes.BasicTypes{list} basic_types_values\n"),
    std::string::npos);

  m.message("test_msgs/msg/Arrays");
  const auto arrays = m.definition("rosmsg_test_msgs__Arrays");
  EXPECT_NE(arrays.find("field int32[3] int32_values\n"), std::string::npos);
  EXPECT_NE(arrays.find("field string{list} string_values\n"), std::string::npos);

  m.message("test_msgs/msg/WStrings");
  const auto wstrings = m.definition("rosmsg_test_msgs__WStrings");
  EXPECT_NE(wstrings.find("field string wstring_value\n"), std::string::npos);

  m.message("std_msgs/msg/Empty");
  EXPECT_NE(m.definition("rosmsg_std_msgs__Empty").find("struct Empty\nend\n"), std::string::npos);
}

TEST(TypeMapper, RenamedFieldsMatchUpstream)
{
  TypeMapper m;
  m.message("sensor_msgs/msg/NavSatStatus");
  // Upstream's fixname renames "service".
  EXPECT_NE(
    m.definition("rosmsg_sensor_msgs__NavSatStatus").find("field uint16 rosservice\n"),
    std::string::npos);
}

TEST(TypeMapper, ConcurrentUse)
{
  TypeMapper m;
  const auto types = package_interfaces("geometry_msgs");
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back(
      [&m, &types, i]() {
        for (size_t k = 0; k < types.size(); ++k) {
          const auto & t = types[(k + static_cast<size_t>(i) * 7) % types.size()];
          const auto root = m.interface(t);
          for (const auto & n : m.closure(root)) {
            EXPECT_FALSE(m.definition(n).empty());
          }
        }
      });
  }
  for (auto & t : threads) {
    t.join();
  }
}

// AC: golden files for the B2 packages; every definition parses and
// verifies in Robot Raconteur.
TEST(Golden, MatchesAndVerifies)
{
  const std::filesystem::path dir = GOLDEN_DIR;
  TypeMapper m;
  std::set<std::string> generated;
  for (const auto & pkg : kGoldenPackages) {
    const auto types = package_interfaces(pkg);
    ASSERT_FALSE(types.empty()) << pkg;
    for (const auto & t : types) {
      std::string root;
      ASSERT_NO_THROW(root = m.interface(t)) << t;
      const auto texts = texts_of(m, root);
      EXPECT_NO_THROW(verify_definitions(texts)) << t;
      for (const auto & n : m.closure(root)) {
        generated.insert(n);
      }
    }
  }
  std::set<std::string> golden;
  for (const auto & e : std::filesystem::directory_iterator(dir)) {
    if (e.path().extension() == ".robdef") {
      golden.insert(e.path().stem().string());
    }
  }
  for (const auto & n : generated) {
    if (golden.count(n) == 0) {
      ADD_FAILURE() << "no golden file for " << n << " (scripts/update-golden.sh)";
      continue;
    }
    EXPECT_EQ(m.definition(n), read_file(dir / (n + ".robdef")))
      << n << " differs from its golden file (scripts/update-golden.sh)";
  }
  for (const auto & n : golden) {
    EXPECT_EQ(generated.count(n), 1u) << "stale golden file " << n << ".robdef";
  }
  EXPECT_GT(generated.size(), 200u);
}

// Every interface installed in the image maps to definitions Robot
// Raconteur accepts. Lists the ones that do not (none expected).
TEST(AllInstalled, MapAndVerify)
{
  TypeMapper m;
  std::vector<std::string> failures;
  size_t count = 0;
  for (const auto & pkg : interface_packages()) {
    for (const auto & t : package_interfaces(pkg)) {
      ++count;
      try {
        verify_definitions(texts_of(m, m.interface(t)));
      } catch (const std::exception & e) {
        failures.push_back(t + ": " + e.what());
      }
    }
  }
  std::ostringstream all;
  for (const auto & f : failures) {
    all << "  " << f << "\n";
  }
  EXPECT_TRUE(failures.empty()) << failures.size() << " of " << count << ":\n" << all.str();
  EXPECT_GT(count, 250u);
}
