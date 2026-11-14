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

#include <memory>
#include <string>
#include <vector>

#include "rcpputils/shared_library.hpp"

// Every function that rmw_implementation (Jazzy) resolves from an RMW library.
// rmw_implementation prefetches all of them, so a missing one breaks loading.
static const char * const kRmwSymbols[] = {
  "rmw_get_implementation_identifier",
  "rmw_init_options_init",
  "rmw_init_options_copy",
  "rmw_init_options_fini",
  "rmw_shutdown",
  "rmw_context_fini",
  "rmw_get_serialization_format",
  "rmw_create_node",
  "rmw_destroy_node",
  "rmw_node_get_graph_guard_condition",
  "rmw_init_publisher_allocation",
  "rmw_fini_publisher_allocation",
  "rmw_create_publisher",
  "rmw_destroy_publisher",
  "rmw_borrow_loaned_message",
  "rmw_return_loaned_message_from_publisher",
  "rmw_publish",
  "rmw_publish_loaned_message",
  "rmw_publisher_count_matched_subscriptions",
  "rmw_publisher_get_actual_qos",
  "rmw_publisher_event_init",
  "rmw_publish_serialized_message",
  "rmw_get_serialized_message_size",
  "rmw_publisher_assert_liveliness",
  "rmw_publisher_wait_for_all_acked",
  "rmw_serialize",
  "rmw_deserialize",
  "rmw_init_subscription_allocation",
  "rmw_fini_subscription_allocation",
  "rmw_create_subscription",
  "rmw_destroy_subscription",
  "rmw_subscription_count_matched_publishers",
  "rmw_subscription_get_actual_qos",
  "rmw_subscription_event_init",
  "rmw_subscription_set_content_filter",
  "rmw_subscription_get_content_filter",
  "rmw_take",
  "rmw_take_sequence",
  "rmw_take_with_info",
  "rmw_take_serialized_message",
  "rmw_take_serialized_message_with_info",
  "rmw_take_loaned_message",
  "rmw_take_loaned_message_with_info",
  "rmw_return_loaned_message_from_subscription",
  "rmw_create_client",
  "rmw_destroy_client",
  "rmw_send_request",
  "rmw_take_response",
  "rmw_client_request_publisher_get_actual_qos",
  "rmw_client_response_subscription_get_actual_qos",
  "rmw_create_service",
  "rmw_destroy_service",
  "rmw_take_request",
  "rmw_send_response",
  "rmw_service_response_publisher_get_actual_qos",
  "rmw_service_request_subscription_get_actual_qos",
  "rmw_take_event",
  "rmw_create_guard_condition",
  "rmw_destroy_guard_condition",
  "rmw_trigger_guard_condition",
  "rmw_create_wait_set",
  "rmw_destroy_wait_set",
  "rmw_wait",
  "rmw_get_publisher_names_and_types_by_node",
  "rmw_get_subscriber_names_and_types_by_node",
  "rmw_get_service_names_and_types_by_node",
  "rmw_get_client_names_and_types_by_node",
  "rmw_get_topic_names_and_types",
  "rmw_get_service_names_and_types",
  "rmw_get_node_names",
  "rmw_get_node_names_with_enclaves",
  "rmw_count_publishers",
  "rmw_count_subscribers",
  "rmw_count_clients",
  "rmw_count_services",
  "rmw_get_gid_for_client",
  "rmw_get_gid_for_publisher",
  "rmw_compare_gids_equal",
  "rmw_service_server_is_available",
  "rmw_set_log_severity",
  "rmw_get_publishers_info_by_topic",
  "rmw_get_subscriptions_info_by_topic",
  "rmw_qos_profile_check_compatible",
  "rmw_publisher_get_network_flow_endpoints",
  "rmw_subscription_get_network_flow_endpoints",
  "rmw_subscription_set_on_new_message_callback",
  "rmw_service_set_on_new_request_callback",
  "rmw_client_set_on_new_response_callback",
  "rmw_event_set_callback",
  "rmw_feature_supported",
  "rmw_event_type_is_supported",
  "rmw_take_dynamic_message",
  "rmw_take_dynamic_message_with_info",
  "rmw_serialization_support_init",
  "rmw_init",
};

TEST(Symbols, LibraryExportsTheWholeRmwApi)
{
  std::shared_ptr<rcpputils::SharedLibrary> lib;
  ASSERT_NO_THROW(
    lib = std::make_shared<rcpputils::SharedLibrary>(RMW_INTRAPROCESS_CPP_LIBRARY_PATH));
  std::vector<std::string> missing;
  for (const char * name : kRmwSymbols) {
    if (!lib->has_symbol(name)) {
      missing.push_back(name);
    }
  }
  EXPECT_TRUE(missing.empty()) << "missing: " << ::testing::PrintToString(missing);
  EXPECT_EQ(95u, sizeof(kRmwSymbols) / sizeof(kRmwSymbols[0]));
}
