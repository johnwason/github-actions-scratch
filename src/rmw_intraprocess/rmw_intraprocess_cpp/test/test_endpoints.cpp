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

#include <cstring>
#include <string>

#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "test_msgs/msg/basic_types.h"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/msg/nested.hpp"

#include "guard_condition.hpp"
#include "registry.hpp"
#include "./rmw_fixture.hpp"

using rmw_intraprocess_cpp::GuardCondition;
using rmw_intraprocess_cpp::Registry;

namespace
{
const rosidl_message_type_support_t * cpp_ts()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::BasicTypes>();
}
const rosidl_message_type_support_t * c_ts()
{
  return ROSIDL_GET_MSG_TYPE_SUPPORT(test_msgs, msg, BasicTypes);
}
const rosidl_message_type_support_t * no_type_support(
  const rosidl_message_type_support_t *, const char *)
{
  return nullptr;
}
}  // namespace

class Endpoints : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    node = rmw_create_node(&context, "endpoints", "/");
    ASSERT_NE(nullptr, node);
  }
  void TearDown() override
  {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    RmwContextFixture::TearDown();
  }
  rmw_node_t * node = nullptr;
  rmw_publisher_options_t pub_options = rmw_get_default_publisher_options();
  rmw_subscription_options_t sub_options = rmw_get_default_subscription_options();
};

TEST_F(Endpoints, CreateDestroyPublisherCppAndC)
{
  const std::size_t topics_before = Registry::instance().topic_count();
  for (auto ts : {cpp_ts(), c_ts()}) {
    rmw_publisher_t * pub =
      rmw_create_publisher(node, ts, "/chatter", &rmw_qos_profile_default, &pub_options);
    ASSERT_NE(nullptr, pub) << rmw_get_error_string().str;
    EXPECT_STREQ("/chatter", pub->topic_name);
    EXPECT_FALSE(pub->can_loan_messages);
    EXPECT_EQ(topics_before + 1, Registry::instance().topic_count());
    rmw_qos_profile_t qos{};
    EXPECT_EQ(RMW_RET_OK, rmw_publisher_get_actual_qos(pub, &qos));
    EXPECT_EQ(RMW_QOS_POLICY_HISTORY_KEEP_LAST, qos.history);
    EXPECT_EQ(10u, qos.depth);
    EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, qos.reliability);
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
    EXPECT_EQ(topics_before, Registry::instance().topic_count());
  }
}

TEST_F(Endpoints, CreateDestroySubscription)
{
  rmw_subscription_t * sub =
    rmw_create_subscription(node, cpp_ts(), "/chatter", &rmw_qos_profile_sensor_data, &sub_options);
  ASSERT_NE(nullptr, sub) << rmw_get_error_string().str;
  EXPECT_STREQ("/chatter", sub->topic_name);
  EXPECT_FALSE(sub->can_loan_messages);
  EXPECT_FALSE(sub->is_cft_enabled);
  rmw_qos_profile_t qos{};
  EXPECT_EQ(RMW_RET_OK, rmw_subscription_get_actual_qos(sub, &qos));
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, qos.reliability);
  EXPECT_EQ(5u, qos.depth);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, sub));
}

TEST_F(Endpoints, SystemDefaultQosIsResolved)
{
  rmw_publisher_t * pub =
    rmw_create_publisher(node, cpp_ts(), "/t", &rmw_qos_profile_system_default, &pub_options);
  ASSERT_NE(nullptr, pub);
  rmw_qos_profile_t qos{};
  EXPECT_EQ(RMW_RET_OK, rmw_publisher_get_actual_qos(pub, &qos));
  EXPECT_EQ(RMW_QOS_POLICY_HISTORY_KEEP_LAST, qos.history);
  EXPECT_EQ(1u, qos.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, qos.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_VOLATILE, qos.durability);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS_AUTOMATIC, qos.liveliness);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
}

TEST_F(Endpoints, GidsAreUnique)
{
  rmw_publisher_t * a =
    rmw_create_publisher(node, cpp_ts(), "/t", &rmw_qos_profile_default, &pub_options);
  rmw_publisher_t * b =
    rmw_create_publisher(node, cpp_ts(), "/t", &rmw_qos_profile_default, &pub_options);
  ASSERT_NE(nullptr, a);
  ASSERT_NE(nullptr, b);
  rmw_gid_t ga{}, gb{};
  EXPECT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(a, &ga));
  EXPECT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(b, &gb));
  bool equal = true;
  EXPECT_EQ(RMW_RET_OK, rmw_compare_gids_equal(&ga, &gb, &equal));
  EXPECT_FALSE(equal);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, a));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, b));
}

TEST_F(Endpoints, BadArguments)
{
  const rmw_qos_profile_t * q = &rmw_qos_profile_default;
  EXPECT_EQ(nullptr, rmw_create_publisher(nullptr, cpp_ts(), "/t", q, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, nullptr, "/t", q, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, cpp_ts(), nullptr, q, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, cpp_ts(), "", q, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, cpp_ts(), "not/absolute", q, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, cpp_ts(), "/t", nullptr, &pub_options));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_publisher(node, cpp_ts(), "/t", q, nullptr));
  rmw_reset_error();
  EXPECT_EQ(nullptr, rmw_create_subscription(node, cpp_ts(), "/bad topic", q, &sub_options));
  rmw_reset_error();

  // A type support with no introspection behind it is rejected.
  rosidl_message_type_support_t bogus = *cpp_ts();
  bogus.typesupport_identifier = "bogus";
  bogus.func = no_type_support;
  EXPECT_EQ(nullptr, rmw_create_publisher(node, &bogus, "/t", q, &pub_options));
  EXPECT_NE(
    std::string::npos,
    std::string(rmw_get_error_string().str).find("rosidl_typesupport_fastrtps"));
  rmw_reset_error();

  rmw_publisher_t * pub = rmw_create_publisher(node, cpp_ts(), "/t", q, &pub_options);
  ASSERT_NE(nullptr, pub);
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_destroy_publisher(nullptr, pub));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_destroy_publisher(node, nullptr));
  rmw_reset_error();
  const char * id = pub->implementation_identifier;
  pub->implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_destroy_publisher(node, pub));
  rmw_reset_error();
  pub->implementation_identifier = id;
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
}

TEST_F(Endpoints, EndpointChangesTriggerGraphGuardCondition)
{
  auto graph = static_cast<GuardCondition *>(rmw_node_get_graph_guard_condition(node)->data);
  graph->take_triggered();
  rmw_subscription_t * sub = rmw_create_subscription(
    node, cpp_ts(), "/g", &rmw_qos_profile_default, &sub_options);
  ASSERT_NE(nullptr, sub);
  EXPECT_TRUE(graph->take_triggered());
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(node, sub));
  EXPECT_TRUE(graph->take_triggered());
}

TEST(TypeNames, FromIntrospection)
{
  // Exercised through the registry-free resolver.
  rmw_intraprocess_cpp::MessageTypeSupport cpp, c, nested;
  ASSERT_TRUE(rmw_intraprocess_cpp::resolve_message_type_support(cpp_ts(), cpp));
  ASSERT_TRUE(rmw_intraprocess_cpp::resolve_message_type_support(c_ts(), c));
  ASSERT_TRUE(
    rmw_intraprocess_cpp::resolve_message_type_support(
      rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::Nested>(), nested));
  EXPECT_EQ("test_msgs/msg/BasicTypes", cpp.type_name);
  EXPECT_EQ("test_msgs/msg/BasicTypes", c.type_name);
  EXPECT_EQ("test_msgs/msg/Nested", nested.type_name);
  EXPECT_EQ(rmw_intraprocess_cpp::MessageTypeSupport::Language::kCpp, cpp.language);
  EXPECT_EQ(rmw_intraprocess_cpp::MessageTypeSupport::Language::kC, c.language);
}
