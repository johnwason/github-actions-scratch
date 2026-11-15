/**
 * @file cereal_serialization.h
 * @brief Cereal serialization of the tesseract_conda types
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
#ifndef TESSERACT_CONDA_CEREAL_SERIALIZATION_H
#define TESSERACT_CONDA_CEREAL_SERIALIZATION_H

#include <tesseract/common/cereal_serialization.h>
#include <tesseract/conda/conda_package.h>
#include <tesseract/conda/conda_resource_locator.h>

#include <cereal/cereal.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/memory.hpp>

namespace tesseract::conda
{
template <class Archive>
void serialize(Archive& ar, CondaPackageIdentity& obj)
{
  ar(cereal::make_nvp("name", obj.name));
  ar(cereal::make_nvp("version", obj.version));
  ar(cereal::make_nvp("build", obj.build));
  ar(cereal::make_nvp("sha256", obj.sha256));
}

/**
 * @brief Written as url, inner path, package identity, local archive path (a hint) and parent.
 * @details On load the archive is not opened; it is resolved on first use, by identity first (see
 * CondaArchiveResource).
 */
template <class Archive>
void serialize(Archive& ar, CondaArchiveResource& obj)
{
  CondaPackageIdentity identity;
  std::string archive_path_hint;
  if constexpr (Archive::is_saving::value)
  {
    identity = obj.getIdentity();
    archive_path_hint = obj.getArchivePath();
  }
  ar(cereal::make_nvp("url", obj.url_));
  ar(cereal::make_nvp("inner_path", obj.inner_path_));
  ar(cereal::make_nvp("identity", identity));
  ar(cereal::make_nvp("archive_path_hint", archive_path_hint));
  ar(cereal::make_nvp("parent", obj.parent_));
  if constexpr (Archive::is_loading::value)
  {
    std::lock_guard<std::mutex> lock(obj.mutex_);
    obj.identity_ = identity;
    obj.archive_path_ = archive_path_hint;
    obj.archive_.reset();
    obj.unavailable_logged_ = false;
  }
}

/**
 * @brief Like GeneralResourceLocator, the package paths are not serialized: they are host paths. The filesystem
 * option is; on load the package paths are read again from this host's default environment variables.
 */
template <class Archive>
void serialize(Archive& ar, CondaResourceLocator& obj)
{
  bool use_filesystem = obj.use_filesystem_;
  ar(cereal::make_nvp("use_filesystem", use_filesystem));
  if constexpr (Archive::is_loading::value)
  {
    // Rebuild the package paths from this host's environment, with the loaded option
    obj = CondaResourceLocator(CondaResourceLocator::defaultEnvironmentVariables(), use_filesystem);
  }
}
}  // namespace tesseract::conda

// On Windows the cereal polymorphic-type registration must be in the header,
// for other platforms registration is in the cpp.
#ifdef _WIN32
#include <tesseract/conda/cereal_serialization_impl.hpp>
#else
CEREAL_FORCE_DYNAMIC_INIT(tesseract_conda_cereal)
#endif

#endif  // TESSERACT_CONDA_CEREAL_SERIALIZATION_H
