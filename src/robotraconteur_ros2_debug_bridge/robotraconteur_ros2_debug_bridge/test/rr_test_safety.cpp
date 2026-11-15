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

#include "rr_test_safety.hpp"

#include <memory>
#include <string>
#include <vector>

namespace rr_test_safety
{

namespace RR = RobotRaconteur;

uint32_t forbidden_flags()
{
  return RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_DISCOVERY_LISTENING |
         RR::RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE |
         RR::RobotRaconteurNodeSetupFlags_ENABLE_HARDWARE_TRANSPORT |
         RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER_PORT_SHARER |
         RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_SERVER_PUBLIC;
}

uint32_t server_flags()
{
  return RR::RobotRaconteurNodeSetupFlags_ENABLE_LOCAL_TRANSPORT |
         RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_START_SERVER |
         RR::RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT |
         RR::RobotRaconteurNodeSetupFlags_INTRA_TRANSPORT_START_SERVER;
}

uint32_t client_flags()
{
  return RR::RobotRaconteurNodeSetupFlags_ENABLE_LOCAL_TRANSPORT |
         RR::RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_START_CLIENT |
         RR::RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT;
}

void check_flags(uint32_t flags, bool allow_loopback_tcp)
{
  if ((flags & forbidden_flags()) != 0) {
    throw UnsafeSetupError(
            "Robot Raconteur test node setup requests announce, discovery, hardware transport, "
            "port sharer or a public local server (flags & forbidden = " +
            std::to_string(flags & forbidden_flags()) + ")");
  }
  const uint32_t tcp = RR::RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT |
    RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER;
  if ((flags & tcp) != 0) {
    if (!allow_loopback_tcp) {
      throw UnsafeSetupError("TCP transport in a test without allow_loopback_tcp");
    }
    if ((flags & RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER) != 0 &&
      (flags & RR::RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST) == 0)
    {
      throw UnsafeSetupError("a test TCP server must listen on localhost only");
    }
  }
}

std::unique_ptr<RR::RobotRaconteurNodeSetup> make_node_setup(
  const RR_SHARED_PTR<RR::RobotRaconteurNode> & node,
  const std::vector<RR_SHARED_PTR<RR::ServiceFactory>> & service_types,
  const std::string & node_name, uint32_t flags, bool allow_loopback_tcp)
{
  check_flags(flags, allow_loopback_tcp);
  // This constructor takes no command line, so nothing can override the
  // flags; the TCP port is ignored without TCP_TRANSPORT_START_SERVER.
  auto setup = std::make_unique<RR::RobotRaconteurNodeSetup>(
    node, service_types, node_name, 0, flags);
  if (!allow_loopback_tcp && setup->GetTcpTransport()) {
    throw UnsafeSetupError("a TCP transport exists although none was requested");
  }
  return setup;
}

}  // namespace rr_test_safety
