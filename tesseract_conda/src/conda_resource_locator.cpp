/**
 * @file conda_resource_locator.cpp
 * @brief A resource locator for ROS packages on disk and inside .conda packages
 *
 * @copyright Copyright (c) 2026, Wason Technology, LLC
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/logging.h>
#include <tesseract/common/utils.h>
#include <tesseract/conda/conda_package.h>
#include <tesseract/conda/conda_resource_locator.h>

namespace tesseract::conda
{
namespace
{
std::vector<std::string> splitPathList(const std::string& value)
{
#ifndef _WIN32
  const char separator = ':';
#else
  const char separator = ';';
#endif
  std::vector<std::string> tokens;
  std::string current;
  for (char c : value)
  {
    if (c == separator)
    {
      if (!current.empty())
        tokens.push_back(current);
      current.clear();
    }
    else
    {
      current.push_back(c);
    }
  }
  if (!current.empty())
    tokens.push_back(current);
  return tokens;
}

std::size_t findSeparator(const std::string& str)
{
  const std::size_t pos = str.find_first_of("/\\");
  return pos;
}
}  // namespace

std::vector<std::string> CondaResourceLocator::defaultEnvironmentVariables()
{
  return { "TESSERACT_RESOURCE_PATH", "ROS_PACKAGE_PATH", "AMENT_PREFIX_PATH" };
}

CondaResourceLocator::CondaResourceLocator(const std::vector<std::string>& environment_variables, bool use_filesystem)
  : use_filesystem_(use_filesystem)
{
  for (const auto& env_variable : environment_variables)
    loadEnvironmentVariable(env_variable);
}

CondaResourceLocator::CondaResourceLocator(const std::vector<std::filesystem::path>& paths,
                                           const std::vector<std::string>& environment_variables,
                                           bool use_filesystem)
  : use_filesystem_(use_filesystem)
{
  for (const auto& path : paths)
    addPath(path);

  for (const auto& env_variable : environment_variables)
    loadEnvironmentVariable(env_variable);
}

bool CondaResourceLocator::loadEnvironmentVariable(const std::string& environment_variable)
{
  const char* value = std::getenv(environment_variable.c_str());  // NOLINT
  if (value == nullptr)
    return false;

  for (const auto& token : splitPathList(value))
    processToken(token);
  return true;
}

bool CondaResourceLocator::addPath(const std::filesystem::path& path) { return processToken(path.string()); }

bool CondaResourceLocator::usesFilesystem() const { return use_filesystem_; }

const std::unordered_map<std::string, std::string>& CondaResourceLocator::getPackagePaths() const
{
  return package_paths_;
}

bool CondaResourceLocator::processToken(const std::string& token)
{
  if (isCondaPackagePath(token))
    return processCondaToken(token);

  if (!use_filesystem_)
  {
    TESSERACT_LOG_DEBUG("Package Path ignored, not a conda package and the filesystem is not used: {}", token);
    return false;
  }
  return processDirectoryToken(token);
}

bool CondaResourceLocator::processDirectoryToken(const std::string& token)
{
  // Same rules as tesseract::common::GeneralResourceLocator
  std::filesystem::path d(token);
  std::error_code ec;
  if (!std::filesystem::is_directory(d, ec))
  {
    TESSERACT_LOG_ERROR("Package Path does not exist: {}", token);
    return false;
  }

  if (std::filesystem::exists(d / "package.xml", ec))
  {
    std::string dir_name = d.filename().string();
    if (package_paths_.find(dir_name) == package_paths_.end())
      package_paths_[dir_name] = d.string();
  }

  std::filesystem::recursive_directory_iterator dir(d, ec), end;
  while (!ec && dir != end)
  {
    if (std::filesystem::exists(dir->path() / "package.xml", ec))
    {
      std::string dir_name = dir->path().filename().string();
      if (package_paths_.find(dir_name) == package_paths_.end())
        package_paths_[dir_name] = dir->path().string();

      dir.disable_recursion_pending();  // don't recurse into this directory.
    }
    dir.increment(ec);
  }
  return true;
}

bool CondaResourceLocator::processCondaToken(const std::string& token)
{
  auto cp = parseCondaPackagePath(token);
  if (!cp)
    return false;

  auto archive = CondaPackageArchiveCache::global().get(cp->archive_path);
  if (!archive)
  {
    TESSERACT_LOG_ERROR("Package Path {}: can't open conda package {}", token, cp->archive_path);
    return false;
  }

  std::map<std::string, std::string> packages;
  if (cp->inner_path.empty())
  {
    auto index = readTesseractResourceIndex(*archive);
    if (!index)
    {
      TESSERACT_LOG_ERROR("{} has no tesseract resource index (info/about.json extra.{} or {}/*.yaml); give a path "
                          "inside the archive, e.g. {}",
                          cp->archive_path,
                          kTesseractResourceIndexKey,
                          kTesseractResourceIndexDataDir,
                          makeCondaPackagePath(cp->archive_path, "share"));
      return false;
    }
    packages = index->packages;
  }
  else
  {
    if (!archive->hasDirectory(cp->inner_path))
    {
      TESSERACT_LOG_ERROR("Package Path does not exist: {} (no directory {} in conda package {})",
                          token,
                          cp->inner_path,
                          cp->archive_path);
      return false;
    }
    packages = findRosPackagesInCondaArchive(*archive, cp->inner_path);
  }

  if (packages.empty())
    TESSERACT_LOG_WARN("Package Path {}: no packages found in conda package {}", token, cp->archive_path);

  for (const auto& p : packages)
  {
    if (package_paths_.find(p.first) == package_paths_.end())
      package_paths_[p.first] = makeCondaPackagePath(cp->archive_path, p.second);
  }
  return true;
}

std::shared_ptr<tesseract::common::Resource> CondaResourceLocator::locateResource(const std::string& url) const
{
  std::string mod_url = url;
  if (url.find("file:///") == 0)
  {
    mod_url.erase(0, std::strlen("file://"));
#ifdef _WIN32
    // file:///C:/x -> C:/x
    if (mod_url.size() >= 3 && mod_url[0] == '/' && std::isalpha(static_cast<unsigned char>(mod_url[1])) != 0 &&
        mod_url[2] == ':')
      mod_url.erase(0, 1);
#endif
    if (findSeparator(mod_url) == std::string::npos)
      return nullptr;
  }
  else if (url.find("package://") == 0)
  {
    mod_url.erase(0, std::strlen("package://"));
    const std::size_t pos = findSeparator(mod_url);
    if (pos == std::string::npos)
      return nullptr;

    const std::string package = mod_url.substr(0, pos);
    mod_url.erase(0, pos);

    auto find_package = package_paths_.find(package);
    if (find_package == package_paths_.end())
    {
      TESSERACT_LOG_ERROR("Failed to find package resource {} for {}", package, url);
      return nullptr;
    }
    mod_url = find_package->second + mod_url;
  }

  auto self = std::make_shared<CondaResourceLocator>(*this);

  // Inside a .conda package. Checked before is_absolute(), since in-memory archives may use virtual paths.
  if (isCondaPackagePath(mod_url))
    return locateCondaResource(url, mod_url, self);

  if (!use_filesystem_)
  {
    TESSERACT_LOG_WARN("Resource not handled (not in a conda package, and the filesystem is not used): {}", mod_url);
    return nullptr;
  }

  if (!std::filesystem::path(mod_url).is_absolute())
  {
    TESSERACT_LOG_WARN("Resource not handled: {}", mod_url);
    return nullptr;
  }

  return std::make_shared<tesseract::common::SimpleLocatedResource>(url, mod_url, self);
}

bool CondaResourceLocator::operator==(const CondaResourceLocator& rhs) const
{
  return use_filesystem_ == rhs.use_filesystem_ &&
         tesseract::common::isIdenticalMap<std::unordered_map<std::string, std::string>, std::string>(
             package_paths_, rhs.package_paths_);
}

bool CondaResourceLocator::operator!=(const CondaResourceLocator& rhs) const { return !operator==(rhs); }

}  // namespace tesseract::conda
