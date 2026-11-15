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

#ifndef CLI_HPP_
#define CLI_HPP_

#include <ostream>

namespace rmw_intraprocess_container
{

/// Exit codes of container_host.
enum ExitCode : int
{
  kExitOk = 0,
  /// A component failed to load, or a run-time error.
  kExitLoadError = 1,
  /// Bad command line or configuration file.
  kExitUsage = 2,
};

/// The whole container_host program: parses the command line, reads the
/// configuration, selects the RMW, initializes rclcpp, loads the components
/// and spins until shutdown (Ctrl+C, rclcpp::shutdown() from a component, or
/// --shutdown-after).
int run(int argc, char ** argv, std::ostream & out, std::ostream & err);

}  // namespace rmw_intraprocess_container

#endif  // CLI_HPP_
