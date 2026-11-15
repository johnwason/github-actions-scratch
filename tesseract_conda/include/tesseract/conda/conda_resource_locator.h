/**
 * @file conda_resource_locator.h
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
#ifndef TESSERACT_CONDA_CONDA_RESOURCE_LOCATOR_H
#define TESSERACT_CONDA_CONDA_RESOURCE_LOCATOR_H

#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>

namespace tesseract::conda
{
class CondaResourceLocator;

template <class Archive>
void serialize(Archive& ar, CondaResourceLocator& obj);

/**
 * @brief Locates ROS packages in directories and inside .conda packages, without extracting the archives
 * @details A drop-in alternative to tesseract::common::GeneralResourceLocator. Paths come from the constructor,
 * addPath() and environment variables (':' separated, ';' on Windows). Each path is one of:
 *  - A directory (only when use_filesystem is true): searched recursively for ROS packages (directories containing
 *    package.xml), exactly like GeneralResourceLocator. The directory name is the package name.
 *  - A .conda package plus a directory inside it, e.g. `/x/ros-humble-foo-1.0-h123_0.conda/share`: the archive's
 *    manifest is searched under that directory for package.xml files.
 *  - A bare .conda package, e.g. `/x/foo.conda`: the packages come from the archive's tesseract resource index
 *    (`extra.tesseract_resource_index` in info/about.json, else share/tesseract/resource_index/\*.yaml in the package
 *    contents). Without an index this is an error.
 *
 * Directories are never searched for .conda files. All packages share one name map; the first registration of a
 * name wins, whether it is on disk or in an archive.
 *
 * `package://` and `file:///` URLs and absolute paths are resolved. Files on disk are returned as
 * SimpleLocatedResource, files inside archives as CondaArchiveResource (isFile() is false; getFilePath() returns the
 * path inside the archive, which this locator accepts again). Every returned resource has this locator as its
 * parent, so `package://` lookups from a file on disk can reach packages in archives and the other way round.
 *
 * Archives are opened through CondaPackageArchiveCache::global().
 *
 * Meshes inside archives are loaded from bytes, so external textures and .mtl files can't be resolved. Use
 * self-contained meshes (.glb or .stl) in conda packages.
 */
class CondaResourceLocator : public tesseract::common::ResourceLocator
{
public:
  using Ptr = std::shared_ptr<CondaResourceLocator>;
  using ConstPtr = std::shared_ptr<const CondaResourceLocator>;

  /** @brief The environment variables read by default: TESSERACT_RESOURCE_PATH, ROS_PACKAGE_PATH, AMENT_PREFIX_PATH */
  static std::vector<std::string> defaultEnvironmentVariables();

  /**
   * @brief Construct the locator from environment variables
   * @param environment_variables The environment variables to read paths from
   * @param use_filesystem If false, only .conda packages are used: directory paths are ignored, and file:/// URLs or
   * absolute paths outside archives are not resolved
   */
  explicit CondaResourceLocator(const std::vector<std::string>& environment_variables = defaultEnvironmentVariables(),
                                bool use_filesystem = true);

  /**
   * @brief Construct the locator from paths, then environment variables
   * @param paths Paths to add (directories or .conda packages, see the class description)
   * @param environment_variables The environment variables to read paths from
   * @param use_filesystem See CondaResourceLocator(const std::vector<std::string>&, bool)
   */
  CondaResourceLocator(const std::vector<std::filesystem::path>& paths,
                       const std::vector<std::string>& environment_variables = defaultEnvironmentVariables(),
                       bool use_filesystem = true);

  ~CondaResourceLocator() override = default;
  CondaResourceLocator(const CondaResourceLocator&) = default;
  CondaResourceLocator& operator=(const CondaResourceLocator&) = default;
  CondaResourceLocator(CondaResourceLocator&&) = default;
  CondaResourceLocator& operator=(CondaResourceLocator&&) = default;

  std::shared_ptr<tesseract::common::Resource> locateResource(const std::string& url) const override;

  /**
   * @brief Add a path: a directory, a .conda package, or a directory inside a .conda package
   * @return false if the path does not exist, can't be used, or is a directory while use_filesystem is false
   */
  bool addPath(const std::filesystem::path& path);

  /**
   * @brief Add the paths listed in an environment variable
   * @return false if the environment variable is not set
   */
  bool loadEnvironmentVariable(const std::string& environment_variable);

  /** @brief True if directories and files on disk are used */
  bool usesFilesystem() const;

  /** @brief Package name -> package directory (on disk, or a path inside an archive) */
  const std::unordered_map<std::string, std::string>& getPackagePaths() const;

  bool operator==(const CondaResourceLocator& rhs) const;
  bool operator!=(const CondaResourceLocator& rhs) const;

private:
  bool use_filesystem_{ true };
  std::unordered_map<std::string, std::string> package_paths_;

  bool processToken(const std::string& token);
  bool processCondaToken(const std::string& token);
  bool processDirectoryToken(const std::string& token);

  template <class Archive>
  friend void ::tesseract::conda::serialize(Archive& ar, CondaResourceLocator& obj);
};

}  // namespace tesseract::conda

#endif  // TESSERACT_CONDA_CONDA_RESOURCE_LOCATOR_H
