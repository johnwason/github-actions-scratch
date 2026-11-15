// Copyright 2011-2026 Wason Technology, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Go thunk source generator for Robot Raconteur service definitions.
// Written to be merged into RobotRaconteurGen as --lang=go (see
// gen/upstream_merge/README.md); it follows the structure of
// CSharpServiceLangGen.

#pragma once

#include <RobotRaconteur.h>
#include <ostream>

using namespace RobotRaconteur;

namespace RobotRaconteurGen
{
class GoServiceLangGen
{
  public:
    struct Options
    {
        // Go import path prefix for generated service packages. The package
        // for service "a.b.c" is imported as <prefix>/<package name>.
        std::string import_prefix;
        // Go import path of the Robot Raconteur runtime package.
        std::string runtime_import;
        Options() : runtime_import("github.com/robotraconteur/robotraconteur_go/robotraconteur") {}
    };

    // Package (directory) name for a service definition name.
    static std::string package_name(const std::string& service_name);

    // Generates <path>/<package>/<package>.go
    static void GenerateFiles(const RR_SHARED_PTR<ServiceDefinition>& d, const std::string& servicedef,
                              const std::vector<RR_SHARED_PTR<ServiceDefinition> >& alldefs, const Options& options,
                              const std::string& path = ".");

    static void GenerateFile(const RR_SHARED_PTR<ServiceDefinition>& d, const std::string& servicedef,
                             const std::vector<RR_SHARED_PTR<ServiceDefinition> >& alldefs, const Options& options,
                             std::ostream* w);
};
} // namespace RobotRaconteurGen
