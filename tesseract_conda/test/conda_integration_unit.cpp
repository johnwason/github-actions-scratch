#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>
#include <tesseract/conda/conda_package.h>
#include <tesseract/conda/conda_resource_locator.h>
#include <tesseract/collision/discrete_contact_manager.h>
#include <tesseract/environment/environment.h>
#include <tesseract/geometry/impl/mesh.h>
#include <tesseract/geometry/mesh_parser.h>
#include <tesseract/scene_graph/graph.h>
#include <tesseract/scene_graph/joint.h>
#include <tesseract/scene_graph/link.h>
#include <tesseract/srdf/srdf_model.h>
#include <tesseract/urdf/urdf_parser.h>
#include "conda_test_fixture.h"

using namespace tesseract::conda;
using tesseract::common::JointId;
using tesseract::common::LinkId;
namespace fx = tesseract::conda::test;

namespace
{
struct RobotArchive
{
  std::filesystem::path dir;
  std::filesystem::path archive;

  explicit RobotArchive(const std::string& prefix)
  {
    dir = fx::makeFixtureTempDir(prefix);
    archive = dir / "robot.conda";
    fx::writeCondaPackage(archive, fx::condaRobotFixture(fx::fixtureBoxStl(), fx::fixtureContactManagersYaml()));
  }
  ~RobotArchive()
  {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  RobotArchive(const RobotArchive&) = delete;
  RobotArchive& operator=(const RobotArchive&) = delete;
  RobotArchive(RobotArchive&&) = delete;
  RobotArchive& operator=(RobotArchive&&) = delete;

  std::string inner(const std::string& path) const { return (archive / "share" / "foo" / path).generic_string(); }
};

tesseract::scene_graph::SceneGraph robotSceneGraph()
{
  tesseract::scene_graph::SceneGraph g;
  g.addLink(tesseract::scene_graph::Link("base_link"));
  g.addLink(tesseract::scene_graph::Link("tool0"));
  tesseract::scene_graph::Joint j("joint_1");
  j.type = tesseract::scene_graph::JointType::REVOLUTE;
  j.parent_link_id = LinkId("base_link");
  j.child_link_id = LinkId("tool0");
  j.axis = Eigen::Vector3d::UnitZ();
  j.limits = std::make_shared<tesseract::scene_graph::JointLimits>(-1, 1, 0, 1, 1, 1);
  g.addJoint(j);
  return g;
}
}  // namespace

TEST(TesseractCondaIntegrationUnit, CreateMeshFromCondaResource)  // NOLINT
{
  fx::CondaFixtureSpec spec;
  spec.files = { { "share/foo/package.xml", fx::fixturePackageXml("foo"), "" },
                 { "share/foo/meshes/box.stl", fx::fixtureBoxStl(), "" } };
  auto archive = CondaPackageArchive::openBytes(
      std::make_shared<const std::vector<uint8_t>>(fx::buildCondaPackage(spec)), "/mem/geometry.conda");
  ASSERT_TRUE(archive != nullptr);
  auto resource =
      std::make_shared<CondaArchiveResource>("package://foo/meshes/box.stl", archive, "share/foo/meshes/box.stl");
  auto from_archive = tesseract::geometry::createMeshFromResource<tesseract::geometry::Mesh>(resource);

  const auto dir = fx::makeFixtureTempDir("tesseract_conda_geometry_unit");
  {
    std::ofstream(dir / "box.stl", std::ios::binary) << fx::fixtureBoxStl();
  }
  auto from_file = tesseract::geometry::createMeshFromPath<tesseract::geometry::Mesh>((dir / "box.stl").string());
  ASSERT_EQ(from_archive.size(), 1);
  ASSERT_EQ(from_file.size(), 1);
  EXPECT_EQ(from_archive[0]->getVertexCount(), from_file[0]->getVertexCount());
  EXPECT_EQ(from_archive[0]->getFaceCount(), from_file[0]->getFaceCount());
  EXPECT_GT(from_archive[0]->getVertexCount(), 0);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

TEST(TesseractCondaIntegrationUnit, ParseURDFFromArchive)  // NOLINT
{
  RobotArchive f("tesseract_conda_urdf_unit");
  CondaResourceLocator locator(std::vector<std::string>{});
  ASSERT_TRUE(locator.addPath(f.archive / "share"));

  auto check = [](const tesseract::scene_graph::SceneGraph& sg) {
    EXPECT_EQ(sg.getLinks().size(), 2);
    auto base = sg.getLink(LinkId("base_link"));
    ASSERT_TRUE(base != nullptr);
    ASSERT_EQ(base->visual.size(), 1);
    ASSERT_EQ(base->collision.size(), 1);
    auto mesh = std::dynamic_pointer_cast<const tesseract::geometry::Mesh>(base->visual[0]->geometry);
    ASSERT_TRUE(mesh != nullptr);
    EXPECT_GT(mesh->getVertexCount(), 0);
    EXPECT_FALSE(mesh->getResource()->isFile());
  };

  // By path inside the archive
  auto sg = tesseract::urdf::parseURDFFile(f.inner("urdf/robot.urdf"), locator);
  ASSERT_TRUE(sg != nullptr);
  check(*sg);

  // By the file path of the located resource
  auto res = locator.locateResource("package://foo/urdf/robot.urdf");
  ASSERT_TRUE(res != nullptr);
  sg = tesseract::urdf::parseURDFFile(res->getFilePath(), locator);
  ASSERT_TRUE(sg != nullptr);
  check(*sg);

  EXPECT_ANY_THROW(tesseract::urdf::parseURDFFile(f.inner("urdf/missing.urdf"), locator));  // NOLINT
}

TEST(TesseractCondaIntegrationUnit, ParseSRDFFromArchive)  // NOLINT
{
  RobotArchive f("tesseract_conda_srdf_unit");
  CondaResourceLocator locator(std::vector<std::string>{});
  ASSERT_TRUE(locator.addPath(f.archive / "share"));
  const auto g = robotSceneGraph();

  tesseract::srdf::SRDFModel srdf;
  srdf.initFile(g, f.inner("urdf/robot.srdf"), locator);
  EXPECT_EQ(srdf.name, "conda_robot");
  EXPECT_EQ(srdf.kinematics_information.chain_groups.size(), 1);

  // The calibration config inside the archive uses a relative !include
  ASSERT_EQ(srdf.calibration_info.joints.size(), 1);
  const auto& cal = srdf.calibration_info.joints.at(JointId("joint_1"));
  EXPECT_NEAR(cal.translation().x(), 1, 1e-9);
  EXPECT_NEAR(cal.translation().z(), 3, 1e-9);

  tesseract::srdf::SRDFModel srdf2;
  srdf2.initFile(g, "package://foo/urdf/robot.srdf", locator);
  EXPECT_EQ(srdf2.calibration_info.joints.size(), 1);

  tesseract::srdf::SRDFModel srdf3;
  EXPECT_ANY_THROW(srdf3.initFile(g, f.inner("urdf/missing.srdf"), locator));  // NOLINT
}

TEST(TesseractCondaIntegrationUnit, EnvironmentInitFromArchive)  // NOLINT
{
  RobotArchive f("tesseract_conda_environment_unit");
  auto locator = std::make_shared<CondaResourceLocator>(std::vector<std::string>{});
  ASSERT_TRUE(locator->addPath(f.archive / "share"));

  // The common pattern: hand getFilePath() of located resources to Environment::init
  auto urdf_res = locator->locateResource("package://foo/urdf/robot.urdf");
  auto srdf_res = locator->locateResource("package://foo/urdf/robot.srdf");
  ASSERT_TRUE(urdf_res != nullptr);
  ASSERT_TRUE(srdf_res != nullptr);
  EXPECT_FALSE(urdf_res->isFile());

  tesseract::environment::Environment env;
  EXPECT_TRUE(env.init(
      std::filesystem::path(urdf_res->getFilePath()), std::filesystem::path(srdf_res->getFilePath()), locator));
  EXPECT_TRUE(env.isInitialized());
  auto sg = env.getSceneGraph();
  ASSERT_TRUE(sg != nullptr);
  EXPECT_EQ(sg->getLinks().size(), 2);
  auto base = sg->getLink(LinkId("base_link"));
  ASSERT_TRUE(base != nullptr);
  ASSERT_EQ(base->collision.size(), 1);
  EXPECT_TRUE(env.getDiscreteContactManager() != nullptr);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
