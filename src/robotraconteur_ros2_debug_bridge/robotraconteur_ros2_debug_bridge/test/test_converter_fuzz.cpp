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

// B11: fuzzing what the converter receives from Robot Raconteur clients.
//
// Valid values of every message-shaped type of the B2 packages are mutated
// at random (element types, lengths, names, list numbering, structure
// types, nesting, nulls, missing and extra elements) and written into ROS
// messages of both introspection flavors. Any exception is fine; a crash,
// a sanitizer report or a hang is not. Time-boxed by FUZZ_SECONDS
// (default 20).

#include <gtest/gtest.h>

#include <RobotRaconteur.h>

#include <chrono>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "type_mapper.hpp"
#include "typesupport.hpp"
#include "value_converter.hpp"

namespace RR = RobotRaconteur;
using robotraconteur_ros2_debug_bridge::DynamicMessage;
using robotraconteur_ros2_debug_bridge::Flavor;
using robotraconteur_ros2_debug_bridge::InterfaceKind;
using robotraconteur_ros2_debug_bridge::IntrospectionCache;
using robotraconteur_ros2_debug_bridge::package_interfaces;
using robotraconteur_ros2_debug_bridge::parse_interface_type;
using robotraconteur_ros2_debug_bridge::ros_to_rr;
using robotraconteur_ros2_debug_bridge::rr_to_ros;
using robotraconteur_ros2_debug_bridge::StructurePtr;

namespace
{

std::vector<std::string> message_types()
{
  std::vector<std::string> out;
  for (const char * pkg : {"action_tutorials_interfaces", "example_interfaces", "geometry_msgs",
      "nav_msgs", "sensor_msgs", "std_msgs", "test_msgs", "turtlesim"})
  {
    for (const auto & t : package_interfaces(pkg)) {
      switch (parse_interface_type(t).kind) {
        case InterfaceKind::Message: out.push_back(t); break;
        case InterfaceKind::Service:
          out.push_back(t + "_Request");
          out.push_back(t + "_Response");
          break;
        case InterfaceKind::Action:
          out.push_back(t + "_Goal");
          out.push_back(t + "_Feedback");
          break;
      }
    }
  }
  return out;
}

class Mutator
{
public:
  explicit Mutator(uint64_t seed)
  : g_(seed) {}

  size_t below(size_t n) {return n == 0 ? 0 : static_cast<size_t>(g_() % n);}
  bool chance(size_t in) {return below(in) == 0;}

  RR::MessageElementDataPtr random_data(int depth)
  {
    static const RR::DataTypes numeric[] = {
      RR::DataTypes_double_t, RR::DataTypes_single_t, RR::DataTypes_int8_t,
      RR::DataTypes_uint8_t, RR::DataTypes_int16_t, RR::DataTypes_uint16_t,
      RR::DataTypes_int32_t, RR::DataTypes_uint32_t, RR::DataTypes_int64_t,
      RR::DataTypes_uint64_t, RR::DataTypes_bool_t, RR::DataTypes_cdouble_t};
    switch (below(depth > 3 ? 3 : 6)) {
      case 0: {
          auto a = RR::AllocateRRArrayByType(numeric[below(12)], below(40));
          auto * p = static_cast<uint8_t *>(a->void_ptr());
          for (size_t i = 0; i < a->size() * a->ElementSize(); ++i) {
            p[i] = static_cast<uint8_t>(g_());
          }
          return a;
        }
      case 1: {
          std::string s(below(50), '\0');
          for (auto & c : s) {
            c = static_cast<char>(g_());
          }
          return RR::stringToRRArray(s);
        }
      case 2:
        return RR::MessageElementDataPtr();
      default: {
          std::vector<RR::MessageElementPtr> items;
          const size_t n = below(6);
          const bool list = chance(2);
          for (size_t i = 0; i < n; ++i) {
            items.push_back(
              list ? RR::CreateMessageElement(
                static_cast<int32_t>(chance(8) ? below(10) : i), random_data(depth + 1)) :
              RR::CreateMessageElement("f" + std::to_string(below(5)), random_data(depth + 1)));
          }
          return RR::CreateMessageElementNestedElementList(
            list ? RR::DataTypes_list_t : RR::DataTypes_structure_t,
            std::string(list ? "" : "rosmsg_x__Y.Y"), std::move(items));
        }
    }
  }

  // Mutates a valid value in place, recursively.
  void mutate(const StructurePtr & s, int depth = 0)
  {
    auto & el = s->Elements;
    if (!el.empty() && chance(10)) {
      el.erase(el.begin() + static_cast<std::ptrdiff_t>(below(el.size())));
    }
    if (chance(15)) {
      el.push_back(RR::CreateMessageElement("extra", random_data(depth)));
    }
    if (chance(30)) {
      s->TypeName = "rosmsg_wrong__Type.Type";
    }
    for (auto & e : el) {
      if (chance(12)) {
        e->SetData(random_data(depth));
        continue;
      }
      if (chance(40)) {
        e->ElementName = "renamed";
      }
      if (e->ElementType == RR::DataTypes_list_t && chance(10)) {
        auto list = e->CastDataToNestedList();
        if (!list->Elements.empty()) {
          RR::MessageElement_SetElementNumber(
            list->Elements[below(list->Elements.size())], static_cast<int32_t>(below(100)));
        }
      }
      if (e->ElementType == RR::DataTypes_structure_t || e->ElementType == RR::DataTypes_list_t) {
        auto nested = e->CastDataToNestedList();
        if (e->ElementType == RR::DataTypes_structure_t) {
          mutate(nested, depth + 1);
        } else {
          for (auto & i : nested->Elements) {
            if (i->ElementType == RR::DataTypes_structure_t) {
              mutate(i->CastDataToNestedList(), depth + 1);
            } else if (chance(10)) {
              i->SetData(random_data(depth + 1));
            }
          }
        }
      }
    }
  }

private:
  std::mt19937_64 g_;
};

}  // namespace

TEST(ConverterFuzz, MutatedValuesNeverCrash)
{
  const char * env = std::getenv("FUZZ_SECONDS");
  const double seconds = env ? std::atof(env) : 20.0;
  const auto deadline =
    std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  IntrospectionCache cache;
  const auto types = message_types();
  ASSERT_GT(types.size(), 150u);
  Mutator m(20261115);
  size_t iterations = 0;
  size_t accepted = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto & type = types[m.below(types.size())];
    const auto * cpp = cache.message(Flavor::Cpp, type);
    DynamicMessage original(Flavor::Cpp, cpp);
    const StructurePtr value = ros_to_rr(Flavor::Cpp, cpp, original.data());
    m.mutate(value);
    for (Flavor fl : {Flavor::Cpp, Flavor::C}) {
      DynamicMessage target(fl, cache.message(fl, type));
      try {
        rr_to_ros(fl, target.members(), value, target.data());
        ++accepted;
        // What was accepted converts back.
        (void)ros_to_rr(fl, target.members(), target.data());
      } catch (const std::exception &) {
        // rejected: fine
      }
    }
    ++iterations;
  }
  std::printf("fuzz: %zu iterations, %zu accepted, %.0f s\n", iterations, accepted, seconds);
  EXPECT_GT(iterations, 100u);
}
