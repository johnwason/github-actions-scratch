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

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
#include "test_msgs/srv/arrays.hpp"
#include "test_msgs/srv/basic_types.h"
#include "test_msgs/srv/basic_types.hpp"

#include "./rmw_fixture.hpp"

using test_msgs::srv::BasicTypes;

namespace
{
const rosidl_service_type_support_t * srv_cpp()
{
  return rosidl_typesupport_cpp::get_service_type_support_handle<BasicTypes>();
}
const rosidl_service_type_support_t * srv_c()
{
  return ROSIDL_GET_SRV_TYPE_SUPPORT(test_msgs, srv, BasicTypes);
}
struct Counter
{
  std::atomic<std::size_t> events{0};
};
void count_cb(const void * user_data, size_t n)
{
  static_cast<Counter *>(const_cast<void *>(user_data))->events += n;
}
}  // namespace

class Services : public RmwContextFixture
{
protected:
  void SetUp() override
  {
    RmwContextFixture::SetUp();
    node = rmw_create_node(&context, "services", "/");
    ASSERT_NE(nullptr, node);
  }
  void TearDown() override
  {
    for (auto c : clients) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_client(node, c));
    }
    for (auto s : services) {
      EXPECT_EQ(RMW_RET_OK, rmw_destroy_service(node, s));
    }
    EXPECT_EQ(RMW_RET_OK, rmw_destroy_node(node));
    RmwContextFixture::TearDown();
  }
  rmw_service_t * service(const char * name, const rosidl_service_type_support_t * ts = srv_cpp())
  {
    rmw_service_t * s = rmw_create_service(node, ts, name, &rmw_qos_profile_services_default);
    EXPECT_NE(nullptr, s) << rmw_get_error_string().str;
    services.push_back(s);
    return s;
  }
  rmw_client_t * client(const char * name, const rosidl_service_type_support_t * ts = srv_cpp())
  {
    rmw_client_t * c = rmw_create_client(node, ts, name, &rmw_qos_profile_services_default);
    EXPECT_NE(nullptr, c) << rmw_get_error_string().str;
    clients.push_back(c);
    return c;
  }
  /// Takes one request from s and answers int32 + int64.
  void serve_one(rmw_service_t * s)
  {
    BasicTypes::Request req;
    rmw_service_info_t header{};
    bool taken = false;
    ASSERT_EQ(RMW_RET_OK, rmw_take_request(s, &header, &req, &taken));
    ASSERT_TRUE(taken);
    BasicTypes::Response res;
    res.int64_value = req.int32_value + req.int64_value;
    res.string_value = req.string_value;
    ASSERT_EQ(RMW_RET_OK, rmw_send_response(s, &header.request_id, &res));
  }
  rmw_node_t * node = nullptr;
  std::vector<rmw_service_t *> services;
  std::vector<rmw_client_t *> clients;
};

TEST_F(Services, RequestResponseRoundTrip)
{
  auto s = service("/add");
  auto c = client("/add");
  bool available = false;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, c, &available));
  EXPECT_TRUE(available);
  BasicTypes::Request req;
  req.int32_value = 2;
  req.int64_value = 40;
  req.string_value = "hello";
  int64_t seq = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  EXPECT_EQ(1, seq);
  serve_one(s);
  BasicTypes::Response res;
  rmw_service_info_t header{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(c, &header, &res, &taken));
  ASSERT_TRUE(taken);
  EXPECT_EQ(42, res.int64_value);
  EXPECT_EQ("hello", res.string_value);
  EXPECT_EQ(1, header.request_id.sequence_number);
  rmw_gid_t gid{};
  ASSERT_EQ(RMW_RET_OK, rmw_get_gid_for_client(c, &gid));
  EXPECT_EQ(0, std::memcmp(gid.data, header.request_id.writer_guid, RMW_GID_STORAGE_SIZE));
  EXPECT_GT(header.source_timestamp, 0);
  EXPECT_GE(header.received_timestamp, header.source_timestamp);
}

TEST_F(Services, ResponsesGoOnlyToTheirClient)
{
  auto s = service("/route");
  auto c1 = client("/route");
  auto c2 = client("/route");
  BasicTypes::Request req;
  int64_t seq = 0;
  req.int32_value = 1;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c1, &req, &seq));
  req.int32_value = 100;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c2, &req, &seq));
  serve_one(s);
  serve_one(s);
  BasicTypes::Response res;
  rmw_service_info_t header{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(c1, &header, &res, &taken));
  ASSERT_TRUE(taken);
  EXPECT_EQ(1, res.int64_value);
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(c1, &header, &res, &taken));
  EXPECT_FALSE(taken);
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(c2, &header, &res, &taken));
  ASSERT_TRUE(taken);
  EXPECT_EQ(100, res.int64_value);
}

TEST_F(Services, CAndCppTypeSupportsInteroperate)
{
  auto s = service("/mixed", srv_c());
  auto c = client("/mixed", srv_cpp());
  BasicTypes::Request req;
  req.int64_value = 7;
  int64_t seq = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  test_msgs__srv__BasicTypes_Request creq;
  ASSERT_TRUE(test_msgs__srv__BasicTypes_Request__init(&creq));
  rmw_service_info_t header{};
  bool taken = false;
  ASSERT_EQ(RMW_RET_OK, rmw_take_request(s, &header, &creq, &taken));
  ASSERT_TRUE(taken);
  EXPECT_EQ(7, creq.int64_value);
  test_msgs__srv__BasicTypes_Response cres;
  ASSERT_TRUE(test_msgs__srv__BasicTypes_Response__init(&cres));
  cres.int64_value = 8;
  ASSERT_EQ(RMW_RET_OK, rmw_send_response(s, &header.request_id, &cres));
  BasicTypes::Response res;
  ASSERT_EQ(RMW_RET_OK, rmw_take_response(c, &header, &res, &taken));
  ASSERT_TRUE(taken);
  EXPECT_EQ(8, res.int64_value);
  test_msgs__srv__BasicTypes_Response__fini(&cres);
  test_msgs__srv__BasicTypes_Request__fini(&creq);
}

TEST_F(Services, AvailabilityRequiresSameNameAndType)
{
  auto c = client("/avail");
  bool available = true;
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, c, &available));
  EXPECT_FALSE(available);
  const auto arrays = rosidl_typesupport_cpp::get_service_type_support_handle<
    test_msgs::srv::Arrays>();
  service("/avail", arrays);  // other type
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, c, &available));
  EXPECT_FALSE(available);
  auto s = service("/avail");
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, c, &available));
  EXPECT_TRUE(available);
  ASSERT_EQ(RMW_RET_OK, rmw_destroy_service(node, s));
  services.pop_back();
  ASSERT_EQ(RMW_RET_OK, rmw_service_server_is_available(node, c, &available));
  EXPECT_FALSE(available);
}

TEST_F(Services, RequestWithoutServiceIsDroppedAndClientGoneIsOk)
{
  auto c = client("/nobody");
  BasicTypes::Request req;
  int64_t seq = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  auto s = service("/nobody");
  BasicTypes::Request in;
  rmw_service_info_t header{};
  bool taken = true;
  ASSERT_EQ(RMW_RET_OK, rmw_take_request(s, &header, &in, &taken));
  EXPECT_FALSE(taken);  // the earlier request had nowhere to go
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  ASSERT_EQ(RMW_RET_OK, rmw_take_request(s, &header, &in, &taken));
  ASSERT_TRUE(taken);
  ASSERT_EQ(RMW_RET_OK, rmw_destroy_client(node, c));
  clients.clear();
  BasicTypes::Response res;
  EXPECT_EQ(RMW_RET_OK, rmw_send_response(s, &header.request_id, &res));
}

TEST_F(Services, EveryMatchingServiceGetsTheRequest)
{
  auto s1 = service("/dup");
  auto s2 = service("/dup");
  auto c = client("/dup");
  BasicTypes::Request req;
  int64_t seq = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  serve_one(s1);
  serve_one(s2);
  int responses = 0;
  BasicTypes::Response res;
  rmw_service_info_t header{};
  bool taken = false;
  while (rmw_take_response(c, &header, &res, &taken) == RMW_RET_OK && taken) {
    EXPECT_EQ(seq, header.request_id.sequence_number);
    ++responses;
  }
  EXPECT_EQ(2, responses);
}

TEST_F(Services, CallbacksAndWaitReadiness)
{
  auto s = service("/cb");
  auto c = client("/cb");
  Counter req_cb, res_cb;
  BasicTypes::Request req;
  int64_t seq = 0;
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c, &req, &seq));
  ASSERT_EQ(RMW_RET_OK, rmw_service_set_on_new_request_callback(s, count_cb, &req_cb));
  EXPECT_EQ(1u, req_cb.events.load());  // already queued
  ASSERT_EQ(RMW_RET_OK, rmw_client_set_on_new_response_callback(c, count_cb, &res_cb));

  rmw_wait_set_t * ws = rmw_create_wait_set(&context, 0);
  void * srv_arr[1] = {s->data};
  void * cli_arr[1] = {c->data};
  rmw_services_t srvs{1, srv_arr};
  rmw_clients_t clis{1, cli_arr};
  const rmw_time_t zero{0, 0};
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, &srvs, &clis, nullptr, ws, &zero));
  EXPECT_NE(nullptr, srv_arr[0]);
  EXPECT_EQ(nullptr, cli_arr[0]);

  std::thread server([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      serve_one(s);
    });
  srv_arr[0] = nullptr;  // only wait for the client now
  cli_arr[0] = c->data;
  rmw_services_t no_srvs{0, srv_arr};
  const rmw_time_t five{5, 0};
  ASSERT_EQ(RMW_RET_OK, rmw_wait(nullptr, nullptr, &no_srvs, &clis, nullptr, ws, &five));
  server.join();
  EXPECT_NE(nullptr, cli_arr[0]);
  EXPECT_EQ(1u, res_cb.events.load());
  ASSERT_EQ(RMW_RET_OK, rmw_client_set_on_new_response_callback(c, nullptr, nullptr));
  ASSERT_EQ(RMW_RET_OK, rmw_service_set_on_new_request_callback(s, nullptr, nullptr));
  EXPECT_EQ(RMW_RET_OK, rmw_destroy_wait_set(ws));
}

TEST_F(Services, SequenceNumbersIncreasePerClient)
{
  service("/seq");
  auto c1 = client("/seq");
  auto c2 = client("/seq");
  BasicTypes::Request req;
  int64_t a = 0, b = 0;
  for (int64_t i = 1; i <= 3; ++i) {
    ASSERT_EQ(RMW_RET_OK, rmw_send_request(c1, &req, &a));
    EXPECT_EQ(i, a);
  }
  ASSERT_EQ(RMW_RET_OK, rmw_send_request(c2, &req, &b));
  EXPECT_EQ(1, b);
}
