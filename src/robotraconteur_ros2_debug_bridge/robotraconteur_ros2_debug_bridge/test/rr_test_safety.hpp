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

// Test-only Robot Raconteur node setup that cannot reach the network
// (CLAUDE.md S2). Every C++ test that starts a Robot Raconteur node uses
// make_node_setup(); test_safety_helper_usage.py fails the build's tests if a
// test calls a node setup class directly. The Python twin is
// rr_test_safety.py.
//
// Rules enforced:
// - never node announce, discovery listening, hardware (USB/Bluetooth)
//   transport, the TCP port sharer, or a public LocalTransport server;
// - TCP only when the test asks for it explicitly, and then only listening
//   on localhost (TCP_TRANSPORT_LISTEN_LOCALHOST);
// - no command-line or environment overrides of these flags.

#ifndef RR_TEST_SAFETY_HPP_
#define RR_TEST_SAFETY_HPP_

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "RobotRaconteur.h"

namespace rr_test_safety
{

/// A node setup the helper refuses to create.
class UnsafeSetupError : public std::logic_error
{
public:
  using std::logic_error::logic_error;
};

/// Flags that are never allowed in tests.
uint32_t forbidden_flags();

/// LocalTransport and IntraTransport, both as servers (no TCP).
uint32_t server_flags();

/// LocalTransport and IntraTransport clients (no TCP).
uint32_t client_flags();

/// Throws UnsafeSetupError if `flags` could reach the network. TCP is
/// rejected unless `allow_loopback_tcp`; with it, a TCP server must listen on
/// localhost only.
void check_flags(uint32_t flags, bool allow_loopback_tcp = false);

/// The only way tests create a Robot Raconteur node setup: checks the flags,
/// configures `node` without command-line overrides, then verifies that no
/// TCP transport exists unless one was allowed.
std::unique_ptr<RobotRaconteur::RobotRaconteurNodeSetup> make_node_setup(
  const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> & node,
  const std::vector<RR_SHARED_PTR<RobotRaconteur::ServiceFactory>> & service_types,
  const std::string & node_name, uint32_t flags, bool allow_loopback_tcp = false);

}  // namespace rr_test_safety

#endif  // RR_TEST_SAFETY_HPP_
