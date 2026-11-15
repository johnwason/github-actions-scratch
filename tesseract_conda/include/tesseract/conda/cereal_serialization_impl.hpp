/**
 * @file cereal_serialization_impl.hpp
 * @brief Cereal polymorphic type registration of the tesseract_conda types
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
#ifndef TESSERACT_CONDA_CEREAL_SERIALIZATION_IMPL_HPP
#define TESSERACT_CONDA_CEREAL_SERIALIZATION_IMPL_HPP

// These must be include before calling macro CEREAL_REGISTER_TYPE
#include <cereal/archives/binary.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/archives/json.hpp>

CEREAL_REGISTER_TYPE(tesseract::conda::CondaArchiveResource)
CEREAL_REGISTER_TYPE(tesseract::conda::CondaResourceLocator)

CEREAL_REGISTER_POLYMORPHIC_RELATION(tesseract::common::Resource, tesseract::conda::CondaArchiveResource)
CEREAL_REGISTER_POLYMORPHIC_RELATION(tesseract::common::ResourceLocator, tesseract::conda::CondaResourceLocator)

#endif  // TESSERACT_CONDA_CEREAL_SERIALIZATION_IMPL_HPP
