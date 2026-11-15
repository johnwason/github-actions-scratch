/**
 * @file collision_margins.h
 * @brief Parse config files
 *
 * @author Levi Armstrong
 * @date January 25, 2022
 *
 * @copyright Copyright (c) 2022, Southwest Research Institute
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
#include <tinyxml2.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/scene_graph/graph.h>
#include <tesseract/scene_graph/joint.h>
#include <tesseract/common/resource_locator.h>
#include <tesseract/common/utils.h>
#include <tesseract/common/yaml_utils.h>
#include <tesseract/common/yaml_extensions.h>
#include <tesseract/srdf/configs.h>
#include <tesseract/srdf/utils.h>

namespace tesseract::srdf
{
namespace
{
/**
 * @brief Resolves !include URLs of a config file: relative to the config resource first, then through the locator
 * @details The fallback keeps absolute and package:// includes working for resources that have no parent locator.
 */
class ConfigIncludeLocator : public tesseract::common::ResourceLocator
{
public:
  ConfigIncludeLocator(const tesseract::common::Resource& resource, const tesseract::common::ResourceLocator& locator)
    : resource_(resource), locator_(locator)
  {
  }

  tesseract::common::Resource::Ptr locateResource(const std::string& url) const override
  {
    if (auto r = resource_.locateResource(url))
      return r;
    return locator_.locateResource(url);
  }

private:
  const tesseract::common::Resource& resource_;
  const tesseract::common::ResourceLocator& locator_;
};
}  // namespace

std::filesystem::path parseConfigFilePath(const tesseract::common::ResourceLocator& locator,
                                          const tinyxml2::XMLElement* xml_element,
                                          const std::array<int, 3>& /*version*/)
{
  std::string filename;
  int status = tesseract::common::QueryStringAttributeRequired(xml_element, "filename", filename);
  if (status != tinyxml2::XML_SUCCESS)
    std::throw_with_nested(std::runtime_error(std::string(xml_element->Value()) + ": Missing or failed to parse "
                                                                                  "'filename' attribute."));

  tesseract::common::Resource::Ptr resource = locator.locateResource(filename);
  if (resource == nullptr)
    std::throw_with_nested(
        std::runtime_error(std::string(xml_element->Value()) + ": Failed to locate resource '" + filename + "'."));

  std::filesystem::path file_path(resource->getFilePath());
  if (!std::filesystem::exists(file_path))
    std::throw_with_nested(std::runtime_error(std::string(xml_element->Value()) +
                                              ": config file does not exist: "
                                              "'" +
                                              file_path.string() + "'."));
  return file_path;
}

tesseract::common::Resource::Ptr parseConfigResource(const tesseract::common::ResourceLocator& locator,
                                                     const tinyxml2::XMLElement* xml_element,
                                                     const std::array<int, 3>& /*version*/)
{
  std::string filename;
  int status = tesseract::common::QueryStringAttributeRequired(xml_element, "filename", filename);
  if (status != tinyxml2::XML_SUCCESS)
    std::throw_with_nested(std::runtime_error(std::string(xml_element->Value()) + ": Missing or failed to parse "
                                                                                  "'filename' attribute."));

  tesseract::common::Resource::Ptr resource = locator.locateResource(filename);
  if (resource == nullptr)
    std::throw_with_nested(
        std::runtime_error(std::string(xml_element->Value()) + ": Failed to locate resource '" + filename + "'."));

  if (resource->isFile() && !std::filesystem::exists(std::filesystem::path(resource->getFilePath())))
    std::throw_with_nested(std::runtime_error(std::string(xml_element->Value()) +
                                              ": config file does not exist: "
                                              "'" +
                                              resource->getFilePath() + "'."));
  return resource;
}

tesseract::common::CalibrationInfo parseCalibrationConfig(const tesseract::scene_graph::SceneGraph& scene_graph,
                                                          const tesseract::common::ResourceLocator& locator,
                                                          const tinyxml2::XMLElement* xml_element,
                                                          const std::array<int, 3>& version)
{
  tesseract::common::Resource::Ptr cal_config_resource = parseConfigResource(locator, xml_element, version);
  YAML::Node config;
  try
  {
    config = tesseract::common::loadYamlResource(*cal_config_resource);
    tesseract::common::processYamlIncludeDirective(config, ConfigIncludeLocator(*cal_config_resource, locator));
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("calibration_config: YAML failed to parse calibration config "
                                              "file '" +
                                              cal_config_resource->getFilePath() + "'."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cal_info = config[tesseract::common::CalibrationInfo::CONFIG_KEY];
  auto info = cal_info.as<tesseract::common::CalibrationInfo>();

  // Check to make sure calibration joints exist
  for (const auto& cal_joint : info.joints)
  {
    if (scene_graph.getJoint(cal_joint.first) == nullptr)
      std::throw_with_nested(
          std::runtime_error("calibration_config: joint '" + cal_joint.first.name() + "' does not exist!"));
  }

  return info;
}

tesseract::common::CalibrationInfo parseCalibrationConfig(const tesseract::scene_graph::SceneGraph& scene_graph,
                                                          const tesseract::common::ResourceLocator& locator,
                                                          const YAML::Node& config)
{
  YAML::Node cfg = config;
  try
  {
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("calibration_config: YAML failed to parse calibration node."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cal_info = cfg[tesseract::common::CalibrationInfo::CONFIG_KEY];
  auto info = cal_info.as<tesseract::common::CalibrationInfo>();

  // Check to make sure calibration joints exist
  for (const auto& cal_joint : info.joints)
  {
    if (scene_graph.getJoint(cal_joint.first) == nullptr)
      std::throw_with_nested(
          std::runtime_error("calibration_config: joint '" + cal_joint.first.name() + "' does not exist!"));
  }

  return info;
}

tesseract::common::CalibrationInfo parseCalibrationConfig(const tesseract::scene_graph::SceneGraph& scene_graph,
                                                          const tesseract::common::ResourceLocator& locator,
                                                          const std::string& config)
{
  YAML::Node cfg;
  try
  {
    cfg = YAML::Load(config);
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("calibration_config: YAML failed to parse calibration string."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cal_info = cfg[tesseract::common::CalibrationInfo::CONFIG_KEY];
  auto info = cal_info.as<tesseract::common::CalibrationInfo>();

  // Check to make sure calibration joints exist
  for (const auto& cal_joint : info.joints)
  {
    if (scene_graph.getJoint(cal_joint.first) == nullptr)
      std::throw_with_nested(
          std::runtime_error("calibration_config: joint '" + cal_joint.first.name() + "' does not exist!"));
  }

  return info;
}

tesseract::common::KinematicsPluginInfo parseKinematicsPluginConfig(const tesseract::common::ResourceLocator& locator,
                                                                    const tinyxml2::XMLElement* xml_element,
                                                                    const std::array<int, 3>& version)
{
  tesseract::common::Resource::Ptr kin_plugin_resource = parseConfigResource(locator, xml_element, version);
  YAML::Node config;
  try
  {
    config = tesseract::common::loadYamlResource(*kin_plugin_resource);
    tesseract::common::processYamlIncludeDirective(config, ConfigIncludeLocator(*kin_plugin_resource, locator));
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("kinematics_plugin_config: YAML failed to parse kinematics plugins "
                                              "file '" +
                                              kin_plugin_resource->getFilePath() + "'."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& kin_plugin_info = config[tesseract::common::KinematicsPluginInfo::CONFIG_KEY];

  return kin_plugin_info.as<tesseract::common::KinematicsPluginInfo>();
}

tesseract::common::KinematicsPluginInfo parseKinematicsPluginConfig(const tesseract::common::ResourceLocator& locator,
                                                                    const YAML::Node& config)
{
  YAML::Node cfg = config;
  try
  {
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("kinematics_plugin_config: YAML failed to parse kinematics plugins "
                                              "node."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& kin_plugin_info = cfg[tesseract::common::KinematicsPluginInfo::CONFIG_KEY];

  return kin_plugin_info.as<tesseract::common::KinematicsPluginInfo>();
}

tesseract::common::KinematicsPluginInfo parseKinematicsPluginConfig(const tesseract::common::ResourceLocator& locator,
                                                                    const std::string& config)
{
  YAML::Node cfg;
  try
  {
    cfg = YAML::Load(config);
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("kinematics_plugin_config: YAML failed to parse kinematics plugins "
                                              "string."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& kin_plugin_info = cfg[tesseract::common::KinematicsPluginInfo::CONFIG_KEY];

  return kin_plugin_info.as<tesseract::common::KinematicsPluginInfo>();
}

tesseract::common::ContactManagersPluginInfo
parseContactManagersPluginConfig(const tesseract::common::ResourceLocator& locator,
                                 const tinyxml2::XMLElement* xml_element,
                                 const std::array<int, 3>& version)
{
  tesseract::common::Resource::Ptr cm_plugin_resource = parseConfigResource(locator, xml_element, version);
  YAML::Node config;
  try
  {
    config = tesseract::common::loadYamlResource(*cm_plugin_resource);
    tesseract::common::processYamlIncludeDirective(config, ConfigIncludeLocator(*cm_plugin_resource, locator));
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("contact_managers_plugin_config: YAML failed to parse contact "
                                              "managers plugins "
                                              "file '" +
                                              cm_plugin_resource->getFilePath() + "'."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cm_plugin_info = config[tesseract::common::ContactManagersPluginInfo::CONFIG_KEY];
  return cm_plugin_info.as<tesseract::common::ContactManagersPluginInfo>();
}

tesseract::common::ContactManagersPluginInfo
parseContactManagersPluginConfig(const tesseract::common::ResourceLocator& locator, const YAML::Node& config)
{
  YAML::Node cfg = config;
  try
  {
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("contact_managers_plugin_config: YAML failed to parse contact "
                                              "managers plugins node."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cm_plugin_info = cfg[tesseract::common::ContactManagersPluginInfo::CONFIG_KEY];
  return cm_plugin_info.as<tesseract::common::ContactManagersPluginInfo>();
}

tesseract::common::ContactManagersPluginInfo
parseContactManagersPluginConfig(const tesseract::common::ResourceLocator& locator, const std::string& config)
{
  YAML::Node cfg;
  try
  {
    cfg = YAML::Load(config);
    tesseract::common::processYamlIncludeDirective(cfg, locator);
  }
  // LCOV_EXCL_START
  catch (...)
  {
    std::throw_with_nested(std::runtime_error("contact_managers_plugin_config: YAML failed to parse contact "
                                              "managers plugins string."));
  }
  // LCOV_EXCL_STOP

  const YAML::Node& cm_plugin_info = cfg[tesseract::common::ContactManagersPluginInfo::CONFIG_KEY];
  return cm_plugin_info.as<tesseract::common::ContactManagersPluginInfo>();
}

}  // namespace tesseract::srdf
