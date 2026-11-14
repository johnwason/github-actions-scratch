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

#include "./rmw_fixture.hpp"

class Node : public RmwContextFixture {};

TEST_F(Node, CreateAndDestroy)
{
  rmw_node_t * node = rmw_create_node(&context, "my_node", "/my_ns");
  ASSERT_NE(nullptr, node) << rmw_get_error_string().str;
  EXPECT_STREQ("rmw_intraprocess_cpp", node->implementation_identifier);
  EXPECT_STREQ("my_node", node->name);
  EXPECT_STREQ("/my_ns", node->namespace_);
  EXPECT_EQ(&context, node->context);
  const rmw_guard_condition_t * gc = rmw_node_get_graph_guard_condition(node);
  ASSERT_NE(nullptr, gc);
  EXPECT_STREQ("rmw_intraprocess_cpp", gc->implementation_identifier);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
}

TEST_F(Node, BadArguments)
{
  EXPECT_EQ(nullptr, rmw_create_node(nullptr, "n", "/"));
  rmw_reset_error();
  rmw_context_t zero = rmw_get_zero_initialized_context();
  EXPECT_EQ(nullptr, rmw_create_node(&zero, "n", "/"));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_node(&context, nullptr, "/"));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_node(&context, "foo bar", "/"));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_node(&context, "n", nullptr));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_node(&context, "n", "not absolute"));
  rmw_reset_error();
  const char * id = context.implementation_identifier;
  context.implementation_identifier = "not-this-rmw";
  EXPECT_EQ(nullptr, rmw_create_node(&context, "n", "/"));
  rmw_reset_error();
  context.implementation_identifier = id;

  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_destroy_node(nullptr));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_node_get_graph_guard_condition(nullptr));
  rmw_reset_error();
  rmw_node_t * node = rmw_create_node(&context, "n", "/");
  ASSERT_NE(nullptr, node);
  node->implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_destroy_node(node));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_node_get_graph_guard_condition(node));
  rmw_reset_error();
  node->implementation_identifier = id;
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
}

TEST_F(Node, NoNodesAfterShutdown)
{
  ASSERT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(nullptr, rmw_create_node(&context, "n", "/"));
  rmw_reset_error();
}

TEST_F(Node, ContextFiniRefusedWhileNodesExist)
{
  rmw_node_t * node = rmw_create_node(&context, "n", "/");
  ASSERT_NE(nullptr, node);
  ASSERT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_ERROR, rmw_context_fini(&context));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
}

TEST_F(Node, DuplicateNamesAreAllowed)
{
  // ROS 2 warns about duplicate node names but the RMW must not reject them.
  rmw_node_t * a = rmw_create_node(&context, "same", "/ns");
  rmw_node_t * b = rmw_create_node(&context, "same", "/ns");
  ASSERT_NE(nullptr, a);
  ASSERT_NE(nullptr, b);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(a));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(b));
}

TEST_F(Node, ManyNodes)
{
  constexpr int kCount = 200;
  rmw_node_t * nodes[kCount];
  for (int i = 0; i < kCount; ++i) {
    nodes[i] = rmw_create_node(&context, "n", "/many");
    ASSERT_NE(nullptr, nodes[i]);
  }
  for (int i = kCount - 1; i >= 0; --i) {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(nodes[i]));
  }
}
