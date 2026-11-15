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

// RobotRaconteurGenGo: sibling of RobotRaconteurGen generating Go thunk
// sources. Command line compatible with "RobotRaconteurGen --thunksource
// --lang=go" (plus --go-import-prefix). ReadFile/ReadRobDefFile are copied
// from RobotRaconteurGen.cpp; see gen/upstream_merge for merging.

#include <fstream>
#include <sstream>
#include <streambuf>
#include <set>
#include <RobotRaconteur.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/join.hpp>
#include <boost/tuple/tuple.hpp>

#include "GoServiceLangGen.h"

using namespace RobotRaconteur;
using namespace RobotRaconteurGen;

bool ReadFile(std::string& file_contents, const std::string& fname)
{
    try
    {
        std::ifstream file;
        file.open(fname.c_str(), std::ios_base::in);
        if (!file)
            return false;
        int8_t bom1 = 0;
        int8_t bom2 = 0;
        int8_t bom3 = 0;
        file >> bom1 >> bom2 >> bom3;
        if (!(bom1 == -17 && bom2 == -69 && bom3 == -65))
        {
            file.seekg(0, std::ifstream::beg);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        file_contents = buffer.str();
        return true;
    }
    catch (std::exception&)
    {
        return false;
    }
}

boost::tuple<RR_SHARED_PTR<ServiceDefinition>, std::string> ReadRobDefFile(const std::string& fname,
                                                                           const std::vector<std::string>& include_dirs,
                                                                           bool try_find = false)
{
    namespace fs = boost::filesystem;
    std::string s = fname;
    fs::path s2(s);
    if (!fs::exists(s2) && s2.is_relative())
    {
        BOOST_FOREACH (const std::string& p, include_dirs)
        {
            fs::path s3 = fs::path(p) / fs::path(s);
            if (fs::exists(s3))
            {
                s = s3.string();
                break;
            }
        }
    }
    if (!fs::exists(s))
    {
        if (try_find)
            return boost::make_tuple(RR_SHARED_PTR<ServiceDefinition>(), "");
        std::cout << "RobotRaconteurGenGo: fatal error: input file not found " << s << std::endl;
        exit(1002);
    }
    try
    {
        boost::shared_ptr<ServiceDefinition> d = boost::make_shared<ServiceDefinition>();
        std::string file_contents;
        if (!ReadFile(file_contents, s))
        {
            std::cout << "RobotRaconteurGenGo: fatal error: could not open file " << s << std::endl;
            exit(1004);
        }
        ServiceDefinitionParseInfo parse_info;
        parse_info.RobDefFilePath = s;
        d->FromString(file_contents, &parse_info);
        d->CheckVersion();
        return boost::make_tuple(d, file_contents);
    }
    catch (ServiceDefinitionParseException& ee)
    {
        std::cout << s << "(" << ee.ParseInfo.LineNumber << "): error: " << ee.ShortMessage << std::endl;
        exit(1005);
    }
    catch (std::exception& ee)
    {
        std::cout << s << ": error: " << std::string(ee.what()) << std::endl;
        exit(1006);
    }
}

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    namespace fs = boost::filesystem;

    std::string lang = "go";
    std::string output_dir;
    bool thunksource = false;
    bool auto_import = false;
    std::vector<std::string> sources;
    std::vector<std::string> include_dirs;
    std::vector<std::string> imports;
    GoServiceLangGen::Options options;

    try
    {
        po::options_description generic_("Allowed options");
        generic_.add_options()("help,h", "print usage message")("version", "print program version")(
            "output-dir", po::value(&output_dir)->default_value("."), "directory for output")(
            "thunksource", po::bool_switch(&thunksource), "generate RR thunk source files")(
            "lang", po::value(&lang)->default_value("go"), "language to generate thunk code for (go)")(
            "include-path,I", po::value(&include_dirs)->composing(), "include path")(
            "import", po::value(&imports)->composing(), "input file for use in imports")(
            "auto-import", po::bool_switch(&auto_import), "automatically load imported robdef")(
            "go-import-prefix", po::value(&options.import_prefix),
            "Go import path prefix of generated service packages")(
            "go-runtime-import", po::value(&options.runtime_import)->default_value(options.runtime_import),
            "Go import path of the Robot Raconteur runtime");
        po::options_description hidden("Hidden options");
        hidden.add_options()("input-file", po::value(&sources), "input file");
        po::positional_options_description p;
        p.add("input-file", -1);
        po::options_description cmdline_options;
        cmdline_options.add(generic_).add(hidden);
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            std::cout << "RobotRaconteurGenGo version " << ROBOTRACONTEUR_VERSION_TEXT << std::endl << std::endl;
            std::cout << generic_ << std::endl;
            return 0;
        }
        if (vm.count("version"))
        {
            std::cout << "RobotRaconteurGenGo version " << ROBOTRACONTEUR_VERSION_TEXT << std::endl;
            return 0;
        }
        if (!thunksource)
        {
            std::cout << "RobotRaconteurGenGo: fatal error: no command specified (--thunksource)" << std::endl;
            return 1;
        }
        if (lang != "go")
        {
            std::cout << "RobotRaconteurGenGo: fatal error: invalid lang specified for thunksource" << std::endl;
            return 1012;
        }
        if (sources.empty())
        {
            std::cout << "RobotRaconteurGenGo: fatal error: no files specified for thunksource" << std::endl;
            return 1001;
        }

        if (char* robdef_path_c = std::getenv("ROBOTRACONTEUR_ROBDEF_PATH"))
        {
            std::string robdef_path(robdef_path_c);
            boost::trim(robdef_path);
            std::vector<std::string> env_dirs;
            if (!robdef_path.empty())
            {
                boost::split(env_dirs, robdef_path, boost::is_any_of(":"));
                BOOST_FOREACH (std::string& s, env_dirs)
                {
                    include_dirs.push_back(boost::trim_copy(s));
                }
            }
        }

        if (!fs::exists(output_dir))
        {
            std::cout << "RobotRaconteurGenGo: fatal error: output directory not found " << output_dir << std::endl;
            return 1003;
        }

        std::vector<RR_SHARED_PTR<ServiceDefinition> > sdefs, alldefs;
        std::vector<std::string> sdefs_str;
        std::set<std::string> required_imports;
        BOOST_FOREACH (std::string& e, boost::range::join(sources, imports))
        {
            RR_SHARED_PTR<ServiceDefinition> d;
            std::string file_contents;
            boost::tie(d, file_contents) = ReadRobDefFile(e, include_dirs);
            alldefs.push_back(d);
            if (boost::range::find(sources, e) != sources.end())
            {
                sdefs.push_back(d);
                sdefs_str.push_back(file_contents);
            }
            BOOST_FOREACH (const std::string& imp, d->Imports)
            {
                required_imports.insert(imp);
            }
        }

        if (auto_import)
        {
            std::set<std::string> missing, attempted;
            BOOST_FOREACH (const std::string& s, required_imports)
            {
                if (!TryFindByName(alldefs, s))
                    missing.insert(s);
            }
            while (!missing.empty())
            {
                std::string e = *missing.begin();
                missing.erase(e);
                RR_SHARED_PTR<ServiceDefinition> d;
                std::string file_contents;
                boost::tie(d, file_contents) = ReadRobDefFile(e + ".robdef", include_dirs, true);
                if (d)
                {
                    alldefs.push_back(d);
                    BOOST_FOREACH (const std::string& imp, d->Imports)
                    {
                        if (!TryFindByName(alldefs, imp) && attempted.count(imp) == 0)
                            missing.insert(imp);
                    }
                }
                else
                {
                    attempted.insert(e);
                }
            }
        }

        try
        {
            std::vector<ServiceDefinitionParseException> warnings;
            VerifyServiceDefinitions(alldefs, warnings);
            BOOST_FOREACH (ServiceDefinitionParseException w, warnings)
            {
                std::cout << w.ParseInfo.RobDefFilePath << "(" << w.ParseInfo.LineNumber
                          << "): warning: " << w.ShortMessage << std::endl;
            }
        }
        catch (ServiceDefinitionParseException& ee)
        {
            std::cout << ee.ParseInfo.RobDefFilePath << "(" << ee.ParseInfo.LineNumber
                      << "): error: " << ee.ShortMessage << std::endl;
            return 1007;
        }
        catch (ServiceDefinitionVerifyException& ee)
        {
            std::cout << ee.ParseInfo.RobDefFilePath << "(" << ee.ParseInfo.LineNumber
                      << "): error: " << ee.ShortMessage << std::endl;
            return 1008;
        }

        for (size_t i = 0; i < sdefs.size(); i++)
        {
            GoServiceLangGen::GenerateFiles(sdefs[i], sdefs_str[i], alldefs, options, output_dir);
        }
        return 0;
    }
    catch (std::exception& e)
    {
        std::cout << "RobotRaconteurGenGo: fatal error: " << e.what() << std::endl;
        return 1009;
    }
}
