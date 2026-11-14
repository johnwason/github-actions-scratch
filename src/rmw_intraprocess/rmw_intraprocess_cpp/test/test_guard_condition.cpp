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

#include <gtest/gtest.h>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "guard_condition.hpp"
#include "./rmw_fixture.hpp"

using rmw_intraprocess_cpp::GuardCondition;

namespace
{
GuardCondition * impl(const rmw_guard_condition_t * gc)
{
  return static_cast<GuardCondition *>(gc->data);
}
}  // namespace

class GuardConditionTest : public RmwContextFixture {};

TEST_F(GuardConditionTest, CreateTriggerDestroy)
{
  rmw_guard_condition_t * gc = rmw_create_guard_condition(&context);
  ASSERT_NE(nullptr, gc) << rmw_get_error_string().str;
  EXPECT_STREQ("rmw_intraprocess_cpp", gc->implementation_identifier);
  EXPECT_EQ(&context, gc->context);
  EXPECT_FALSE(impl(gc)->is_triggered());
  EXPECT_EQ(RMW_RET_OK, rmw_trigger_guard_condition(gc));
  EXPECT_TRUE(impl(gc)->take_triggered());
  EXPECT_FALSE(impl(gc)->take_triggered());
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_guard_condition(gc));
}

TEST_F(GuardConditionTest, BadArguments)
{
  EXPECT_EQ(nullptr, rmw_create_guard_condition(nullptr));
  rmw_reset_error();
  rmw_context_t zero = rmw_get_zero_initialized_context();
  EXPECT_EQ(nullptr, rmw_create_guard_condition(&zero));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_trigger_guard_condition(nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_destroy_guard_condition(nullptr));
  rmw_reset_error();
  rmw_guard_condition_t * gc = rmw_create_guard_condition(&context);
  ASSERT_NE(nullptr, gc);
  const char * id = gc->implementation_identifier;
  gc->implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_trigger_guard_condition(gc));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_destroy_guard_condition(gc));
  rmw_reset_error();
  gc->implementation_identifier = id;
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_guard_condition(gc));
}

TEST_F(GuardConditionTest, GraphGuardConditionFiresOnNodeChanges)
{
  rmw_node_t * a = rmw_create_node(&context, "a", "/");
  ASSERT_NE(nullptr, a);
  const rmw_guard_condition_t * graph_a = rmw_node_get_graph_guard_condition(a);
  ASSERT_NE(nullptr, graph_a);
  impl(graph_a)->take_triggered();  // clear the trigger from a's own creation

  // A second context still shares the single domain/registry.
  rmw_context_t other = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &other));
  rmw_node_t * b = rmw_create_node(&other, "b", "/");
  ASSERT_NE(nullptr, b);
  EXPECT_TRUE(impl(graph_a)->take_triggered());
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(b));
  EXPECT_TRUE(impl(graph_a)->take_triggered());

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(a));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&other));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&other));
}
