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

// B0 smoke service: registers experimental.ros2_debug_bridge_smoke as "smoke"
// on a LocalTransport server named argv[1] (through rr_test_safety, so no
// TCP, announce or discovery), prints "ready <node name>", and exits when
// stdin closes. test_python_client.py drives it.

#include <cstdint>
#include <iostream>
#include <string>

#include "RobotRaconteur.h"
#include "robotraconteur_generated.h"  // NOLINT(build/include_subdir): generated
#include "rr_test_safety.hpp"

namespace smoke = experimental::ros2_debug_bridge_smoke;

namespace
{

class SmokeImpl : public smoke::Smoke
{
public:
  std::string get_greeting() override {return "hello from C++";}
  int32_t add(int32_t a, int32_t b) override {return a + b;}
};

}  // namespace

int main(int argc, char ** argv)
{
  if (argc != 2) {
    std::cerr << "usage: rr_smoke_service <node name>\n";
    return 2;
  }
  const std::string node_name = argv[1];
  auto node = RR_MAKE_SHARED<RobotRaconteur::RobotRaconteurNode>();
  node->Init();
  {
    auto setup = rr_test_safety::make_node_setup(
      node, ROBOTRACONTEUR_SERVICE_TYPES, node_name, rr_test_safety::server_flags());
    node->RegisterService("smoke", "experimental.ros2_debug_bridge_smoke",
      RR_MAKE_SHARED<SmokeImpl>());
    std::cout << "ready " << node_name << " " << node->NodeID().ToString() << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
    }
    node->Shutdown();
  }
  return 0;
}
