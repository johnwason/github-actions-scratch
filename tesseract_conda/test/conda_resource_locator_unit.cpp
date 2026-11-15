#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>
#include <tesseract/common/serialization.h>
#include <tesseract/common/unit_test_utils.h>
#include <tesseract/conda/cereal_serialization.h>
#include <tesseract/conda/conda_package.h>
#include <tesseract/conda/conda_resource_locator.h>
#include "conda_test_fixture.h"

using namespace tesseract::conda;
using tesseract::common::Resource;
using tesseract::common::SimpleLocatedResource;
namespace fx = tesseract::conda::test;

namespace
{
std::string asString(const std::vector<uint8_t>& v) { return { v.begin(), v.end() }; }

void writeText(const std::filesystem::path& path, const std::string& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

struct LocatorFixture
{
  std::filesystem::path dir;
  std::filesystem::path archive;
  std::filesystem::path disk;  // directory with ROS packages on disk

  LocatorFixture()
  {
    dir = fx::makeFixtureTempDir("tesseract_conda_locator_unit");
    archive = dir / "robot.conda";
    fx::writeCondaPackage(archive, fx::condaRobotFixture(fx::fixtureBoxStl()));

    disk = dir / "ws";
    writeText(disk / "bar" / "package.xml", fx::fixturePackageXml("bar"));
    // A file on disk that points into the archive
    writeText(disk / "bar" / "config" / "uses_foo.yaml", "mesh: package://foo/meshes/box.stl\n");
    // A package on disk with the same name as the one in the archive
    writeText(dir / "shadow" / "foo" / "package.xml", fx::fixturePackageXml("foo"));
    writeText(dir / "shadow" / "foo" / "meshes" / "box.stl", "disk copy");
  }
  ~LocatorFixture()
  {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  LocatorFixture(const LocatorFixture&) = delete;
  LocatorFixture& operator=(const LocatorFixture&) = delete;
  LocatorFixture(LocatorFixture&&) = delete;
  LocatorFixture& operator=(LocatorFixture&&) = delete;
};
}  // namespace

TEST(TesseractCondaUnit, CondaResourceLocatorFilesystemAndArchive)  // NOLINT
{
  LocatorFixture f;
  auto locator = std::make_shared<CondaResourceLocator>(
      std::vector<std::filesystem::path>{ f.disk, f.archive / "share" }, std::vector<std::string>{});
  EXPECT_TRUE(locator->usesFilesystem());
  EXPECT_EQ(locator->getPackagePaths().size(), 2);

  // A file on disk
  auto on_disk = locator->locateResource("package://bar/config/uses_foo.yaml");
  ASSERT_TRUE(on_disk != nullptr);
  EXPECT_TRUE(on_disk->isFile());
  EXPECT_TRUE(std::dynamic_pointer_cast<SimpleLocatedResource>(on_disk) != nullptr);

  // From the file on disk into the archive: the disk resource's parent is this locator
  auto mesh = on_disk->locateResource("package://foo/meshes/box.stl");
  ASSERT_TRUE(mesh != nullptr);
  EXPECT_FALSE(mesh->isFile());
  EXPECT_EQ(asString(mesh->getResourceContents()), fx::fixtureBoxStl());

  // And from the archive back to disk
  auto back = mesh->locateResource("package://bar/package.xml");
  ASSERT_TRUE(back != nullptr);
  EXPECT_TRUE(back->isFile());

  // file:/// and absolute paths on disk
  const std::string pkg_xml = (f.disk / "bar" / "package.xml").generic_string();
  EXPECT_TRUE(locator->locateResource(pkg_xml) != nullptr);
  EXPECT_TRUE(locator->locateResource("file://" + std::string(pkg_xml.front() == '/' ? "" : "/") + pkg_xml) != nullptr);

  // Missing paths and packages
  EXPECT_FALSE(locator->addPath(f.dir / "missing"));
  EXPECT_TRUE(locator->locateResource("package://missing/x.txt") == nullptr);
  EXPECT_TRUE(locator->locateResource("relative/x.txt") == nullptr);
  EXPECT_TRUE(locator->locateResource("package://foo/meshes/missing.stl") == nullptr);

  // Copies compare equal
  EXPECT_TRUE(*locator == CondaResourceLocator(*locator));
  EXPECT_FALSE(*locator != CondaResourceLocator(*locator));
}

TEST(TesseractCondaUnit, CondaResourceLocatorFirstRegistrationWins)  // NOLINT
{
  LocatorFixture f;
  {
    CondaResourceLocator locator(std::vector<std::filesystem::path>{ f.dir / "shadow", f.archive / "share" },
                                 std::vector<std::string>{});
    auto res = locator.locateResource("package://foo/meshes/box.stl");
    ASSERT_TRUE(res != nullptr);
    EXPECT_TRUE(res->isFile());
    EXPECT_EQ(asString(res->getResourceContents()), "disk copy");
  }
  {
    CondaResourceLocator locator(std::vector<std::filesystem::path>{ f.archive / "share", f.dir / "shadow" },
                                 std::vector<std::string>{});
    auto res = locator.locateResource("package://foo/meshes/box.stl");
    ASSERT_TRUE(res != nullptr);
    EXPECT_FALSE(res->isFile());
  }
}

TEST(TesseractCondaUnit, CondaResourceLocatorWithoutFilesystem)  // NOLINT
{
  LocatorFixture f;
  CondaResourceLocator locator(std::vector<std::string>{}, false);
  EXPECT_FALSE(locator.usesFilesystem());
  EXPECT_FALSE(locator.addPath(f.disk));  // directories are ignored
  EXPECT_TRUE(locator.addPath(f.archive / "share"));
  EXPECT_EQ(locator.getPackagePaths().size(), 1);

  EXPECT_TRUE(locator.locateResource("package://foo/meshes/box.stl") != nullptr);
  EXPECT_TRUE(locator.locateResource((f.disk / "bar" / "package.xml").generic_string()) == nullptr);
  EXPECT_TRUE(locator.locateResource((f.archive / "share" / "foo" / "package.xml").generic_string()) != nullptr);

  CondaResourceLocator with_fs(std::vector<std::string>{});
  EXPECT_TRUE(locator != with_fs);
}

TEST(TesseractCondaUnit, CondaResourceLocatorEnvironment)  // NOLINT
{
  LocatorFixture f;
#ifndef _WIN32
  const std::string value = f.disk.string() + ":" + (f.archive / "share").string() + "::";
  setenv("TESSERACT_CONDA_LOCATOR_UNIT_PATH", value.c_str(), 1);
#else
  const std::string value = f.disk.string() + ";" + (f.archive / "share").string() + ";;";
  _putenv_s("TESSERACT_CONDA_LOCATOR_UNIT_PATH", value.c_str());
#endif
  CondaResourceLocator locator(std::vector<std::string>{ "TESSERACT_CONDA_LOCATOR_UNIT_PATH", "TESSERACT_UNSET_VAR" });
  EXPECT_EQ(locator.getPackagePaths().size(), 2);
  EXPECT_FALSE(locator.loadEnvironmentVariable("TESSERACT_UNSET_VAR"));
  EXPECT_TRUE(locator.locateResource("package://bar/package.xml") != nullptr);
  EXPECT_TRUE(locator.locateResource("package://foo/urdf/robot.urdf") != nullptr);

  const auto defaults = CondaResourceLocator::defaultEnvironmentVariables();
  EXPECT_EQ(defaults.size(), 3);
  EXPECT_EQ(defaults.front(), "TESSERACT_RESOURCE_PATH");
}

TEST(TesseractCondaUnit, CondaResourceLocatorSerialization)  // NOLINT
{
  auto locator = std::make_shared<CondaResourceLocator>(std::vector<std::string>{}, false);
  const std::string xml =
      tesseract::common::Serialization::toArchiveStringXML<tesseract::common::ResourceLocator::Ptr>(locator);
  auto loaded = std::dynamic_pointer_cast<CondaResourceLocator>(
      tesseract::common::Serialization::fromArchiveStringXML<tesseract::common::ResourceLocator::Ptr>(xml));
  ASSERT_TRUE(loaded != nullptr);
  EXPECT_FALSE(loaded->usesFilesystem());
  EXPECT_TRUE(*loaded == *locator);
}

TEST(TesseractCondaUnit, CondaArchiveResourceSerialization)  // NOLINT
{
  using tesseract::conda::test::CondaFixtureSpec;
  const auto dir = fx::makeFixtureTempDir("tesseract_conda_serialization_unit");

  CondaFixtureSpec spec;
  spec.name = "ser";
  spec.files = { { "share/foo/package.xml", fx::fixturePackageXml("foo"), "" },
                 { "share/foo/data.txt", "hello conda", "" } };
  const auto archive_file = dir / "ser.conda";
  fx::writeCondaPackage(archive_file, spec);

  auto& cache = CondaPackageArchiveCache::global();
  auto archive = cache.get(archive_file.generic_string());
  ASSERT_TRUE(archive != nullptr);
  auto resource = std::make_shared<CondaArchiveResource>("package://foo/data.txt", archive, "share/foo/data.txt");

  // Round trip while the archive is cached, also as the polymorphic base type
  tesseract::common::testSerialization<CondaArchiveResource::Ptr>(
      resource, "CondaArchiveResource", tesseract::common::testSerializationComparePtrEqual<CondaArchiveResource::Ptr>);
  tesseract::common::testSerializationDerivedClass<Resource, CondaArchiveResource>(resource,
                                                                                   "CondaArchiveResourceBase");

  using tesseract::common::Serialization;
  const std::string xml = Serialization::toArchiveStringXML<Resource::Ptr>(resource);
  {
    auto loaded =
        std::dynamic_pointer_cast<CondaArchiveResource>(Serialization::fromArchiveStringXML<Resource::Ptr>(xml));
    ASSERT_TRUE(loaded != nullptr);
    EXPECT_EQ(loaded->getIdentity(), archive->identity());
    EXPECT_EQ(loaded->getArchive(), archive);
    EXPECT_EQ(asString(loaded->getResourceContents()), "hello conda");
    EXPECT_EQ(loaded->getFilePath(), archive_file.generic_string() + "/share/foo/data.txt");
  }

  // Not cached any more: found again through the path hint, whose identity matches
  const auto identity = archive->identity();
  archive.reset();
  resource.reset();
  cache.clear();
  {
    auto loaded = Serialization::fromArchiveStringXML<Resource::Ptr>(xml);
    EXPECT_EQ(asString(loaded->getResourceContents()), "hello conda");
  }

  // The hint path now holds a different package: not available
  cache.clear();
  spec.files[1].contents = "changed";
  fx::writeCondaPackage(archive_file, spec);
  {
    auto loaded =
        std::dynamic_pointer_cast<CondaArchiveResource>(Serialization::fromArchiveStringXML<Resource::Ptr>(xml));
    ASSERT_TRUE(loaded != nullptr);
    EXPECT_TRUE(loaded->getArchive() == nullptr);
    EXPECT_TRUE(loaded->getResourceContents().empty());
    EXPECT_TRUE(loaded->getResourceContentStream() == nullptr);
    EXPECT_EQ(loaded->getIdentity(), identity);
  }

  // A registered resolver is called and provides the package
  cache.clear();
  spec.files[1].contents = "hello conda";
  auto original_bytes = std::make_shared<const std::vector<uint8_t>>(fx::buildCondaPackage(spec));
  int calls = 0;
  cache.setResolver([&](const CondaPackageIdentity& requested) -> CondaPackageArchive::ConstPtr {
    ++calls;
    auto a = CondaPackageArchive::openBytes(original_bytes, "/resolved/ser.conda");
    return (a && a->identity() == requested) ? a : nullptr;
  });
  {
    auto loaded =
        std::dynamic_pointer_cast<CondaArchiveResource>(Serialization::fromArchiveStringXML<Resource::Ptr>(xml));
    ASSERT_TRUE(loaded != nullptr);
    EXPECT_EQ(asString(loaded->getResourceContents()), "hello conda");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(loaded->getFilePath(), "/resolved/ser.conda/share/foo/data.txt");
  }
  cache.setResolver(nullptr);
  cache.clear();

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
