#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <filesystem>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>
#include <tesseract/geometry/impl/mesh.h>
#include <tesseract/scene_graph/graph.h>
#include <tesseract/scene_graph/link.h>
#include <tesseract/urdf/urdf_parser.h>

#ifdef TESSERACT_COMMON_HAS_CONDA_ARCHIVES
#include "conda_test_fixture.h"

TEST(TesseractURDFUnit, CondaArchive)  // NOLINT
{
  using namespace tesseract::common;
  namespace fx = tesseract::common::test;
  const std::filesystem::path source_root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  const auto dir = fx::makeFixtureTempDir("tesseract_urdf_conda_unit");
  const auto archive = dir / "robot.conda";
  fx::writeCondaPackage(archive,
                        fx::condaRobotFixture(fx::readFixtureFile(source_root / "support" / "meshes" / "box_2m.stl")));

  GeneralResourceLocator locator(std::vector<std::string>{});
  ASSERT_TRUE(locator.addPath(archive / "share"));

  const std::string urdf_path = (archive / "share" / "foo" / "urdf" / "robot.urdf").generic_string();
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
  auto sg = tesseract::urdf::parseURDFFile(urdf_path, locator);
  ASSERT_TRUE(sg != nullptr);
  check(*sg);

  // By the file path of the located resource
  auto res = locator.locateResource("package://foo/urdf/robot.urdf");
  ASSERT_TRUE(res != nullptr);
  sg = tesseract::urdf::parseURDFFile(res->getFilePath(), locator);
  ASSERT_TRUE(sg != nullptr);
  check(*sg);

  EXPECT_ANY_THROW(tesseract::urdf::parseURDFFile(  // NOLINT
      (archive / "share" / "foo" / "urdf" / "missing.urdf").generic_string(),
      locator));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
#endif
