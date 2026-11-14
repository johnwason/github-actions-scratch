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

#include <string>

#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/features.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/rmw.h"

#include "./rmw_fixture.hpp"

namespace
{
std::string take_error()
{
  std::string s = rmw_get_error_string().str;
  rmw_reset_error();
  return s;
}
}  // namespace

class Unsupported : public RmwContextFixture {};

TEST_F(Unsupported, UnsupportedFunctionsReportWhy)
{
  rmw_node_t * node = rmw_create_node(&context, "n", "/");
  ASSERT_NE(nullptr, node);

  EXPECT_FALSE(rmw_event_type_is_supported(RMW_EVENT_LIVELINESS_LOST));
  EXPECT_FALSE(rmw_event_type_is_supported(RMW_EVENT_REQUESTED_DEADLINE_MISSED));
  EXPECT_TRUE(rmw_event_type_is_supported(RMW_EVENT_PUBLICATION_MATCHED));
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED, rmw_publisher_get_network_flow_endpoints(nullptr, nullptr, nullptr));
  EXPECT_NE(std::string::npos, take_error().find("intra-process"));
  rmw_publisher_allocation_t pub_alloc;
  EXPECT_EQ(RMW_RET_UNSUPPORTED, rmw_init_publisher_allocation(nullptr, nullptr, &pub_alloc));
  EXPECT_NE(std::string::npos, take_error().find("rmw_fastrtps_cpp"));
  rmw_subscription_allocation_t sub_alloc;
  EXPECT_EQ(
    RMW_RET_UNSUPPORTED, rmw_init_subscription_allocation(nullptr, nullptr, &sub_alloc));
  EXPECT_NE(std::string::npos, take_error().find("rmw_fastrtps_cpp"));
  EXPECT_EQ(RMW_RET_UNSUPPORTED, rmw_borrow_loaned_message(nullptr, nullptr, nullptr));
  EXPECT_NE(std::string::npos, take_error().find("loaned"));

  EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
}

TEST(Features, Advertised)
{
  EXPECT_TRUE(rmw_feature_supported(RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER));
  EXPECT_TRUE(rmw_feature_supported(RMW_FEATURE_MESSAGE_INFO_RECEPTION_SEQUENCE_NUMBER));
  EXPECT_FALSE(rmw_feature_supported(RMW_MIDDLEWARE_CAN_TAKE_DYNAMIC_MESSAGE));
  EXPECT_FALSE(rmw_event_type_is_supported(RMW_EVENT_LIVELINESS_CHANGED));
}

TEST(Gids, CompareEqual)
{
  rmw_gid_t a{};
  rmw_gid_t b{};
  a.implementation_identifier = rmw_get_implementation_identifier();
  b.implementation_identifier = rmw_get_implementation_identifier();
  a.data[0] = 1;
  b.data[0] = 1;
  bool equal = false;
  EXPECT_EQ(RMW_RET_OK, rmw_compare_gids_equal(&a, &b, &equal));
  EXPECT_TRUE(equal);
  b.data[RMW_GID_STORAGE_SIZE - 1] = 2;
  EXPECT_EQ(RMW_RET_OK, rmw_compare_gids_equal(&a, &b, &equal));
  EXPECT_FALSE(equal);
  b.implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_compare_gids_equal(&a, &b, &equal));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_compare_gids_equal(&a, nullptr, &equal));
  rmw_reset_error();
}

TEST(LogSeverity, SetAndReject)
{
  EXPECT_EQ(RMW_RET_OK, rmw_set_log_severity(RMW_LOG_SEVERITY_WARN));
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_set_log_severity(static_cast<rmw_log_severity_t>(7)));
  rmw_reset_error();
}
