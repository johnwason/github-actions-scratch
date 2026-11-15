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

// M9 acceptance: a gateway-style client finds the extension API at run time
// (extension.hpp: no link to this RMW library) and observes rclcpp
// publications through a tap, reading fields through the introspection type
// support. With another RMW (rmw_fastrtps_cpp instance of this test),
// find_api() returns nullptr: the feature-detection path a gateway uses to
// fall back to ordinary subscriptions.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "test_msgs/msg/strings.hpp"

#include "rmw_intraprocess_cpp/extension.hpp"

using test_msgs::msg::Strings;

namespace
{

class RclcppTap : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  static bool is_this_rmw()
  {
    const char * rmw = std::getenv("RMW_IMPLEMENTATION");
    return rmw != nullptr && std::string(rmw) == "rmw_intraprocess_cpp";
  }
};

/// Reads the string_value member through the introspection type support.
std::string read_string_value(const rmw_intraprocess_tap_sample_t & s)
{
  const auto * members =
    static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
    s.introspection_members);
  for (uint32_t i = 0; i < members->member_count_; ++i) {
    if (std::strcmp(members->members_[i].name_, "string_value") == 0) {
      return *reinterpret_cast<const std::string *>(
        static_cast<const uint8_t *>(s.ros_message) + members->members_[i].offset_);
    }
  }
  return "<missing>";
}

}  // namespace

TEST_F(RclcppTap, FeatureDetection)
{
  const auto * api = rmw_intraprocess_cpp::extension::find_api();
  if (is_this_rmw()) {
    ASSERT_NE(nullptr, api);
    EXPECT_EQ(RMW_INTRAPROCESS_EXTENSION_API_VERSION, api->version);
  } else {
    EXPECT_EQ(nullptr, api);
  }
}

TEST_F(RclcppTap, ObservesRclcppPublications)
{
  const auto * api = rmw_intraprocess_cpp::extension::find_api();
  if (!is_this_rmw()) {
    GTEST_SKIP() << "the tap is an rmw_intraprocess_cpp extension";
  }
  ASSERT_NE(nullptr, api);
  auto node = std::make_shared<rclcpp::Node>("tap_user", "/m9");
  auto pub = node->create_publisher<Strings>("tapped", 10);  // no subscriptions

  std::mutex mutex;
  std::vector<std::string> values;
  std::vector<std::string> types;
  {
    rmw_intraprocess_cpp::extension::Tap tap(
      api, "/m9/tapped", [&](const rmw_intraprocess_tap_sample_t & s) {
        std::lock_guard<std::mutex> lock(mutex);
        types.emplace_back(s.type_name);
        EXPECT_STREQ(
          rosidl_typesupport_introspection_cpp::typesupport_identifier,
          s.introspection_identifier);
        values.push_back(read_string_value(s));
      });
    for (const char * text : {"one", "two", "three"}) {
      Strings m;
      m.string_value = text;
      pub->publish(m);
    }
  }  // tap removed
  Strings late;
  late.string_value = "not tapped";
  pub->publish(late);

  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ((std::vector<std::string>{"one", "two", "three"}), values);
  EXPECT_EQ(
    (std::vector<std::string>{"test_msgs/msg/Strings", "test_msgs/msg/Strings",
      "test_msgs/msg/Strings"}), types);
}
