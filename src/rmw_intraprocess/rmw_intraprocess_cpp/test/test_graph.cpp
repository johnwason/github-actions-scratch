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

// RMW-level graph API tests (M5).

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rcutils/types/string_array.h"
#include "rmw/error_handling.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/rmw.h"
#include "rmw/topic_endpoint_info_array.h"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
#include "test_msgs/msg/basic_types.hpp"
#include "test_msgs/msg/strings.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "./rmw_fixture.hpp"

namespace
{
const rosidl_message_type_support_t * basic_ts()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::BasicTypes>();
}
const rosidl_message_type_support_t * strings_ts()
{
  return rosidl_typesupport_cpp::get_message_type_support_handle<test_msgs::msg::Strings>();
}
const rosidl_service_type_support_t * srv_ts()
{
  return rosidl_typesupport_cpp::get_service_type_support_handle<test_msgs::srv::BasicTypes>();
}

using NamesAndTypes = std::map<std::string, std::vector<std::string>>;

/// Converts and finalizes an rmw_names_and_types_t.
NamesAndTypes take(rmw_names_and_types_t & nt)
{
  NamesAndTypes out;
  for (size_t i = 0; i < nt.names.size; ++i) {
    std::vector<std::string> & types = out[nt.names.data[i]];
    for (size_t j = 0; j < nt.types[i].size; ++j) {
      types.push_back(nt.types[i].data[j]);
    }
  }
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_fini(&nt));
  return out;
}

std::vector<std::string> take(rcutils_string_array_t & a)
{
  std::vector<std::string> out(a.data, a.data + a.size);
  EXPECT_EQ(RCUTILS_RET_OK, rcutils_string_array_fini(&a));
  return out;
}
}  // namespace

class Graph : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    node = rmw_create_node(&context, "graph_node", "/g");
    ASSERT_NE(nullptr, node) << rmw_get_error_string().str;
    other = rmw_create_node(&context, "other_node", "/");
    ASSERT_NE(nullptr, other) << rmw_get_error_string().str;
  }

  void TearDown() override
  {
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(other));
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    RmwContextFixture::TearDown();
  }

  rmw_publisher_t * publisher(
    rmw_node_t * n, const char * topic,
    const rosidl_message_type_support_t * ts = basic_ts())
  {
    rmw_publisher_options_t opts = rmw_get_default_publisher_options();
    rmw_publisher_t * p = rmw_create_publisher(n, ts, topic, &rmw_qos_profile_default, &opts);
    EXPECT_NE(nullptr, p) << rmw_get_error_string().str;
    return p;
  }

  rmw_subscription_t * subscription(
    rmw_node_t * n, const char * topic,
    const rosidl_message_type_support_t * ts = basic_ts())
  {
    rmw_subscription_options_t opts = rmw_get_default_subscription_options();
    rmw_subscription_t * s =
      rmw_create_subscription(n, ts, topic, &rmw_qos_profile_default, &opts);
    EXPECT_NE(nullptr, s) << rmw_get_error_string().str;
    return s;
  }

  NamesAndTypes topics()
  {
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rmw_names_and_types_t nt = rmw_get_zero_initialized_names_and_types();
    EXPECT_EQ(RMW_RET_OK, rmw_get_topic_names_and_types(node, &allocator, false, &nt));
    return take(nt);
  }

  NamesAndTypes services()
  {
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    rmw_names_and_types_t nt = rmw_get_zero_initialized_names_and_types();
    EXPECT_EQ(RMW_RET_OK, rmw_get_service_names_and_types(node, &allocator, &nt));
    return take(nt);
  }

  size_t count(rmw_ret_t (* fn)(const rmw_node_t *, const char *, size_t *), const char * name)
  {
    size_t n = 1234;
    EXPECT_EQ(RMW_RET_OK, fn(node, name, &n));
    return n;
  }

  rmw_node_t * node = nullptr;
  rmw_node_t * other = nullptr;
};

TEST_F(Graph, NodeNamesWithEnclaves)
{
  rcutils_string_array_t names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t namespaces = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclaves = rcutils_get_zero_initialized_string_array();
  ASSERT_EQ(
    RMW_RET_OK, rmw_get_node_names_with_enclaves(node, &names, &namespaces, &enclaves));
  std::vector<std::string> n = take(names);
  std::vector<std::string> ns = take(namespaces);
  std::vector<std::string> e = take(enclaves);
  ASSERT_EQ(2u, n.size());
  ASSERT_EQ(2u, ns.size());
  ASSERT_EQ(2u, e.size());
  std::set<std::string> qualified;
  for (size_t i = 0; i < n.size(); ++i) {
    qualified.insert(ns[i] + "|" + n[i]);
    EXPECT_EQ("/", e[i]);
  }
  EXPECT_EQ((std::set<std::string>{"/g|graph_node", "/|other_node"}), qualified);

  // A second context is in the same (single) domain, so its nodes are listed too.
  rmw_context_t context2 = rmw_get_zero_initialized_context();
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context2));
  rmw_node_t * node2 = rmw_create_node(&context2, "third", "/");
  ASSERT_NE(nullptr, node2);
  names = rcutils_get_zero_initialized_string_array();
  namespaces = rcutils_get_zero_initialized_string_array();
  ASSERT_EQ(RMW_RET_OK, rmw_get_node_names(node, &names, &namespaces));
  EXPECT_EQ(3u, take(names).size());
  EXPECT_EQ(3u, take(namespaces).size());
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node2));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context2));
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context2));

  names = rcutils_get_zero_initialized_string_array();
  namespaces = rcutils_get_zero_initialized_string_array();
  ASSERT_EQ(RMW_RET_OK, rmw_get_node_names(node, &names, &namespaces));
  EXPECT_EQ(2u, take(names).size());
  EXPECT_EQ(2u, take(namespaces).size());
}

TEST_F(Graph, TopicNamesAndTypesComeAndGo)
{
  EXPECT_TRUE(topics().empty());
  rmw_publisher_t * pub = publisher(node, "/a");
  rmw_subscription_t * sub_other_type = subscription(other, "/a", strings_ts());
  rmw_subscription_t * sub_b = subscription(other, "/b");
  ASSERT_NE(nullptr, pub);
  ASSERT_NE(nullptr, sub_other_type);
  ASSERT_NE(nullptr, sub_b);

  NamesAndTypes expected{
    {"/a", {"test_msgs/msg/BasicTypes", "test_msgs/msg/Strings"}},
    {"/b", {"test_msgs/msg/BasicTypes"}}};
  EXPECT_EQ(expected, topics());
  // Services are not topics.
  EXPECT_TRUE(services().empty());

  EXPECT_EQ(1u, count(rmw_count_publishers, "/a"));
  EXPECT_EQ(1u, count(rmw_count_subscribers, "/a"));
  EXPECT_EQ(0u, count(rmw_count_publishers, "/b"));
  EXPECT_EQ(1u, count(rmw_count_subscribers, "/b"));
  EXPECT_EQ(0u, count(rmw_count_subscribers, "/nothing"));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, sub_b));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, sub_other_type));
  expected = {{"/a", {"test_msgs/msg/BasicTypes"}}};
  EXPECT_EQ(expected, topics());
  EXPECT_EQ(0u, count(rmw_count_subscribers, "/b"));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
  EXPECT_TRUE(topics().empty());
  EXPECT_EQ(0u, count(rmw_count_publishers, "/a"));
}

TEST_F(Graph, NamesAndTypesByNode)
{
  rmw_publisher_t * pub = publisher(node, "/p");
  rmw_subscription_t * sub = subscription(other, "/s", strings_ts());
  rmw_service_t * srv = rmw_create_service(
    other, srv_ts(), "/srv", &rmw_qos_profile_services_default);
  rmw_client_t * cli = rmw_create_client(
    node, srv_ts(), "/cli", &rmw_qos_profile_services_default);
  ASSERT_NE(nullptr, pub);
  ASSERT_NE(nullptr, sub);
  ASSERT_NE(nullptr, srv);
  ASSERT_NE(nullptr, cli);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_names_and_types_t nt = rmw_get_zero_initialized_names_and_types();
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_publisher_names_and_types_by_node(node, &allocator, "graph_node", "/g", false, &nt));
  EXPECT_EQ((NamesAndTypes{{"/p", {"test_msgs/msg/BasicTypes"}}}), take(nt));
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_publisher_names_and_types_by_node(node, &allocator, "other_node", "/", false, &nt));
  EXPECT_TRUE(take(nt).empty());
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_subscriber_names_and_types_by_node(node, &allocator, "other_node", "/", false, &nt));
  EXPECT_EQ((NamesAndTypes{{"/s", {"test_msgs/msg/Strings"}}}), take(nt));
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_service_names_and_types_by_node(node, &allocator, "other_node", "/", &nt));
  EXPECT_EQ((NamesAndTypes{{"/srv", {"test_msgs/srv/BasicTypes"}}}), take(nt));
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_client_names_and_types_by_node(node, &allocator, "graph_node", "/g", &nt));
  EXPECT_EQ((NamesAndTypes{{"/cli", {"test_msgs/srv/BasicTypes"}}}), take(nt));
  ASSERT_EQ(
    RMW_RET_OK,
    rmw_get_service_names_and_types_by_node(node, &allocator, "graph_node", "/g", &nt));
  EXPECT_TRUE(take(nt).empty());

  // Wrong namespace: no such node.
  EXPECT_EQ(
    RMW_RET_NODE_NAME_NON_EXISTENT,
    rmw_get_publisher_names_and_types_by_node(node, &allocator, "graph_node", "/", false, &nt));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_names_and_types_check_zero(&nt));

  // A service name is in the graph if a server or a client uses it.
  EXPECT_EQ(
    (NamesAndTypes{
    {"/cli", {"test_msgs/srv/BasicTypes"}}, {"/srv", {"test_msgs/srv/BasicTypes"}}}),
    services());
  EXPECT_EQ(1u, count(rmw_count_services, "/srv"));
  EXPECT_EQ(0u, count(rmw_count_clients, "/srv"));
  EXPECT_EQ(1u, count(rmw_count_clients, "/cli"));
  EXPECT_EQ(0u, count(rmw_count_services, "/cli"));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, cli));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(other, srv));
  EXPECT_TRUE(services().empty());
  EXPECT_EQ(0u, count(rmw_count_services, "/srv"));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, sub));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
}

TEST_F(Graph, EndpointInfoByTopic)
{
  rmw_publisher_t * pub = publisher(node, "/info");
  rmw_subscription_t * sub = subscription(other, "/info");
  ASSERT_NE(nullptr, pub);
  ASSERT_NE(nullptr, sub);
  rmw_gid_t pub_gid;
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_publisher(pub, &pub_gid));

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_topic_endpoint_info_array_t info = rmw_get_zero_initialized_topic_endpoint_info_array();
  ASSERT_EQ(RMW_RET_OK, rmw_get_publishers_info_by_topic(node, &allocator, "/info", false, &info));
  ASSERT_EQ(1u, info.size);
  const rmw_topic_endpoint_info_t & p = info.info_array[0];
  EXPECT_STREQ("graph_node", p.node_name);
  EXPECT_STREQ("/g", p.node_namespace);
  EXPECT_STREQ("test_msgs/msg/BasicTypes", p.topic_type);
  EXPECT_EQ(RMW_ENDPOINT_PUBLISHER, p.endpoint_type);
  EXPECT_EQ(0, std::memcmp(pub_gid.data, p.endpoint_gid, RMW_GID_STORAGE_SIZE));
  EXPECT_EQ(rmw_qos_profile_default.depth, p.qos_profile.depth);
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, p.qos_profile.reliability);
  const rosidl_type_hash_t * hash = basic_ts()->get_type_hash_func(basic_ts());
  ASSERT_NE(nullptr, hash);
  EXPECT_EQ(hash->version, p.topic_type_hash.version);
  EXPECT_EQ(0, std::memcmp(hash->value, p.topic_type_hash.value, ROSIDL_TYPE_HASH_SIZE));
  EXPECT_NE(0, p.topic_type_hash.version);
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_fini(&info, &allocator));

  ASSERT_EQ(
    RMW_RET_OK, rmw_get_subscriptions_info_by_topic(node, &allocator, "/info", false, &info));
  ASSERT_EQ(1u, info.size);
  EXPECT_STREQ("other_node", info.info_array[0].node_name);
  EXPECT_STREQ("/", info.info_array[0].node_namespace);
  EXPECT_EQ(RMW_ENDPOINT_SUBSCRIPTION, info.info_array[0].endpoint_type);
  EXPECT_EQ(RMW_RET_OK, rmw_topic_endpoint_info_array_fini(&info, &allocator));

  ASSERT_EQ(
    RMW_RET_OK, rmw_get_subscriptions_info_by_topic(node, &allocator, "/none", false, &info));
  EXPECT_EQ(0u, info.size);

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, sub));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, pub));
}

TEST_F(Graph, BestAvailableAdaptsToExistingEndpoints)
{
  // Subscription: copies the existing publishers' policies.
  rmw_qos_profile_t pub_qos = rmw_qos_profile_default;
  pub_qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  pub_qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  pub_qos.deadline = rmw_time_t{0, 2000000};
  pub_qos.liveliness_lease_duration = rmw_time_t{0, 3000000};
  rmw_publisher_options_t popts = rmw_get_default_publisher_options();
  rmw_publisher_t * p = rmw_create_publisher(node, basic_ts(), "/ba_sub", &pub_qos, &popts);
  ASSERT_NE(nullptr, p) << rmw_get_error_string().str;
  rmw_subscription_options_t sopts = rmw_get_default_subscription_options();
  rmw_subscription_t * s = rmw_create_subscription(
    other, basic_ts(), "/ba_sub", &rmw_qos_profile_best_available, &sopts);
  ASSERT_NE(nullptr, s) << rmw_get_error_string().str;
  rmw_qos_profile_t actual;
  ASSERT_EQ(RMW_RET_OK, rmw_subscription_get_actual_qos(s, &actual));
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, actual.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL, actual.durability);
  EXPECT_EQ(2000000u, actual.deadline.nsec);
  EXPECT_EQ(3000000u, actual.liveliness_lease_duration.nsec);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, s));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, p));

  // Publisher: RELIABLE + TRANSIENT_LOCAL, and meets the subscriptions'
  // deadline/liveliness.
  rmw_qos_profile_t sub_qos = rmw_qos_profile_default;
  sub_qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  sub_qos.deadline = rmw_time_t{0, 2000000};
  sub_qos.liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  sub_qos.liveliness_lease_duration = rmw_time_t{0, 3000000};
  s = rmw_create_subscription(other, basic_ts(), "/ba_pub", &sub_qos, &sopts);
  ASSERT_NE(nullptr, s) << rmw_get_error_string().str;
  p = rmw_create_publisher(node, basic_ts(), "/ba_pub", &rmw_qos_profile_best_available, &popts);
  ASSERT_NE(nullptr, p) << rmw_get_error_string().str;
  ASSERT_EQ(RMW_RET_OK, rmw_publisher_get_actual_qos(p, &actual));
  EXPECT_EQ(RMW_QOS_POLICY_RELIABILITY_RELIABLE, actual.reliability);
  EXPECT_EQ(RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL, actual.durability);
  EXPECT_EQ(2000000u, actual.deadline.nsec);
  EXPECT_EQ(RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC, actual.liveliness);
  EXPECT_EQ(3000000u, actual.liveliness_lease_duration.nsec);
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_publisher(node, p));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_subscription(other, s));
}

TEST_F(Graph, BadArguments)
{
  size_t n = 0;
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_publishers(node, "relative", &n));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_count_services(node, "/bad name", &n));
  rmw_reset_error();
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_names_and_types_t nt = rmw_get_zero_initialized_names_and_types();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_publisher_names_and_types_by_node(node, &allocator, "graph_node", "g", false, &nt));
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_get_client_names_and_types_by_node(node, &allocator, "bad name", "/g", &nt));
  rmw_reset_error();
}
