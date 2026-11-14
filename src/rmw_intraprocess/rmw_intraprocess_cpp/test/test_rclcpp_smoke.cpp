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

// This test does NOT link librmw_intraprocess_cpp. It uses rclcpp, which goes
// through rmw_implementation; RMW_IMPLEMENTATION=rmw_intraprocess_cpp (set by
// CMake) makes rmw_implementation load the library dynamically.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rmw/rmw.h"

namespace
{
rclcpp::NodeOptions m1_node_options()
{
  // M1 has no publishers or services yet, so turn off the entities rclcpp
  // creates by default (rosout, parameter services/events, type descriptions).
  rclcpp::NodeOptions options;
  options.enable_rosout(false)
  .start_parameter_services(false)
  .start_parameter_event_publisher(false)
  .parameter_overrides({rclcpp::Parameter("start_type_description_service", false)});
  return options;
}
}  // namespace

TEST(RclcppSmoke, InitCreateNodeShutdown)
{
  rclcpp::init(0, nullptr);
  // Resolved through rmw_implementation, so this proves the dynamic load.
  EXPECT_STREQ("rmw_intraprocess_cpp", rmw_get_implementation_identifier());
  {
    auto node = std::make_shared<rclcpp::Node>("smoke", "/m1", m1_node_options());
    EXPECT_EQ(std::string("/m1/smoke"), node->get_fully_qualified_name());
    EXPECT_NE(nullptr, node->get_node_graph_interface()->get_graph_guard_condition());
  }
  EXPECT_TRUE(rclcpp::shutdown());
}

TEST(RclcppSmoke, RepeatedInitShutdownAndMultipleContexts)
{
  for (int i = 0; i < 5; ++i) {
    auto ctx_a = std::make_shared<rclcpp::Context>();
    auto ctx_b = std::make_shared<rclcpp::Context>();
    rclcpp::InitOptions init_a;
    rclcpp::InitOptions init_b;
    init_a.set_domain_id(1);
    init_b.set_domain_id(2);  // ignored: single domain
    ctx_a->init(0, nullptr, init_a);
    ctx_b->init(0, nullptr, init_b);
    auto options_a = m1_node_options();
    auto options_b = m1_node_options();
    options_a.context(ctx_a);
    options_b.context(ctx_b);
    {
      auto a = std::make_shared<rclcpp::Node>("a", "/m1", options_a);
      auto b = std::make_shared<rclcpp::Node>("b", "/m1", options_b);
      EXPECT_EQ(std::string("a"), a->get_name());
      EXPECT_EQ(std::string("b"), b->get_name());
    }
    EXPECT_TRUE(ctx_a->shutdown("done"));
    EXPECT_TRUE(ctx_b->shutdown("done"));
  }
}

TEST(RclcppSmoke, DefaultNodeOptions)
{
  // Since M4: rosout, parameter services/events and the type description
  // service all work, so a node with default options can be created.
  rclcpp::init(0, nullptr);
  {
    auto node = std::make_shared<rclcpp::Node>("defaults", "/m4");
    EXPECT_EQ(std::string("/m4/defaults"), node->get_fully_qualified_name());
    node->declare_parameter("p", 1);
    EXPECT_EQ(1, node->get_parameter("p").as_int());
  }
  EXPECT_TRUE(rclcpp::shutdown());
}
