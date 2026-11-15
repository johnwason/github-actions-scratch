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

// The C++ Robot Raconteur test safety helper: refuses every flag set that could
// reach the network, and a safe node has LocalTransport/IntraTransport only.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "RobotRaconteur.h"
#include "rr_test_safety.hpp"

namespace RR = RobotRaconteur;

TEST(RrTestSafety, RejectsNetworkFlags)
{
  const std::vector<uint32_t> unsafe = {
    RR::RobotRaconteurNodeSetupFlags_CLIENT_DEFAULT,
    RR::RobotRaconteurNodeSetupFlags_SERVER_DEFAULT,
    RR::RobotRaconteurNodeSetupFlags_SECURE_SERVER_DEFAULT,
    rr_test_safety::client_flags() | RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE,
    rr_test_safety::client_flags() |
    RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_DISCOVERY_LISTENING,
    rr_test_safety::client_flags() | RR::RobotRaconteurNodeSetupFlags_ENABLE_HARDWARE_TRANSPORT,
    rr_test_safety::server_flags() | RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_SERVER_PUBLIC,
    rr_test_safety::client_flags() | RR::RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT,
  };
  for (uint32_t flags : unsafe) {
    EXPECT_THROW(rr_test_safety::check_flags(flags), rr_test_safety::UnsafeSetupError) << flags;
  }
  const uint32_t tcp_server = RR::RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT |
    RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER;
  EXPECT_THROW(
    rr_test_safety::check_flags(tcp_server, true), rr_test_safety::UnsafeSetupError);
  EXPECT_NO_THROW(
    rr_test_safety::check_flags(
      tcp_server | RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST, true));
  EXPECT_NO_THROW(rr_test_safety::check_flags(rr_test_safety::server_flags()));
  EXPECT_NO_THROW(rr_test_safety::check_flags(rr_test_safety::client_flags()));
}

TEST(RrTestSafety, SafeServerHasNoTcpTransport)
{
  auto node = RR_MAKE_SHARED<RR::RobotRaconteurNode>();
  node->Init();
  {
    auto setup = rr_test_safety::make_node_setup(
      node, {}, "rr_test_safety_gtest", rr_test_safety::server_flags());
    EXPECT_FALSE(setup->GetTcpTransport());
    EXPECT_TRUE(setup->GetLocalTransport());
    EXPECT_TRUE(setup->GetIntraTransport());
  }
  node->Shutdown();
  EXPECT_THROW(
    rr_test_safety::make_node_setup(
      node, {}, "x", RR::RobotRaconteurNodeSetupFlags_SERVER_DEFAULT),
    rr_test_safety::UnsafeSetupError);
}
