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

// Writes the Robot Raconteur service definitions the bridge uses for ROS
// interface types, with everything they import, and verifies them.
//
//   generate_robdef [--out DIR] [--package PKG]... [TYPE]...
//
// TYPE is pkg/msg/Name (topic and message definitions), pkg/srv/Name or
// pkg/action/Name. --package adds every interface of an installed package.
// Without --out the definitions go to stdout. Exit code 1 on any error.

#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "type_mapper.hpp"

namespace rb = robotraconteur_ros2_debug_bridge;

namespace
{

int usage(std::ostream & os, int code)
{
  os << "usage: generate_robdef [--out DIR] [--package PKG]... [TYPE]...\n"
    "  TYPE: pkg/msg/Name, pkg/srv/Name or pkg/action/Name\n";
  return code;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string out_dir;
  std::vector<std::string> types;
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--help" || a == "-h") {
        return usage(std::cout, 0);
      } else if (a == "--out" && i + 1 < argc) {
        out_dir = argv[++i];
      } else if (a == "--package" && i + 1 < argc) {
        for (const auto & t : rb::package_interfaces(argv[++i])) {
          types.push_back(t);
        }
      } else if (!a.empty() && a[0] == '-') {
        return usage(std::cerr, 1);
      } else {
        types.push_back(a);
      }
    }
    if (types.empty()) {
      return usage(std::cerr, 1);
    }

    rb::TypeMapper mapper;
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const auto & t : types) {
      for (const auto & n : mapper.closure(mapper.interface(t))) {
        if (seen.insert(n).second) {
          names.push_back(n);
        }
      }
    }
    std::vector<std::string> texts;
    for (const auto & n : names) {
      texts.push_back(mapper.definition(n));
    }
    rb::verify_definitions(texts);

    for (size_t i = 0; i < names.size(); ++i) {
      if (out_dir.empty()) {
        std::cout << texts[i] << "\n";
        continue;
      }
      const std::string path = out_dir + "/" + names[i] + ".robdef";
      std::ofstream f(path, std::ios::binary);
      f << texts[i];
      if (!f) {
        std::cerr << "generate_robdef: cannot write " << path << "\n";
        return 1;
      }
    }
  } catch (const std::exception & e) {
    std::cerr << "generate_robdef: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
