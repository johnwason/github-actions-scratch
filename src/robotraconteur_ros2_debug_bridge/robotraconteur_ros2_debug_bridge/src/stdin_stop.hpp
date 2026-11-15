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

#ifndef STDIN_STOP_HPP_
#define STDIN_STOP_HPP_

// --stop-on-stdin-close: stop (rclcpp::shutdown()) when standard input is
// closed, the same as Ctrl+C. For supervisors and tests that cannot send
// SIGINT to one process (Windows; docs/windows.md). Opt in only: a process
// started with no stdin would otherwise stop at once.

#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"

namespace robotraconteur_ros2_debug_bridge
{

inline constexpr const char * kStopOnStdinCloseFlag = "--stop-on-stdin-close";

/// Removes every `flag` from argv (before rclcpp::init); true if it was there.
inline bool take_flag(int & argc, char ** argv, const char * flag)
{
  bool found = false;
  int out = 1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      found = true;
    } else {
      argv[out++] = argv[i];
    }
  }
  argc = out;
  argv[argc] = nullptr;
  return found;
}

/// After rclcpp::init: a thread that reads stdin until it closes, then calls
/// rclcpp::shutdown(). Detached: it may still be blocked in the read when the
/// process stops another way.
inline void shutdown_when_stdin_closes()
{
  std::thread(
    []() {
      std::string line;
      while (std::getline(std::cin, line)) {
      }
      try {
        RCLCPP_INFO(rclcpp::get_logger("stdin_stop"), "stdin closed; shutting down");
        rclcpp::shutdown();
      } catch (const std::exception &) {
      }
    }).detach();
}

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // STDIN_STOP_HPP_
