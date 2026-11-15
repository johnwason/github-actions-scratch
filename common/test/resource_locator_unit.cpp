#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>
#include <tesseract/common/types.h>
#include <tesseract/common/unit_test_utils.h>
#include <tesseract/common/cereal_serialization.h>
#include <tesseract/common/yaml_utils.h>
#ifdef TESSERACT_COMMON_HAS_CONDA_ARCHIVES
#include <tesseract/common/conda_package.h>
#include "conda_test_fixture.h"
#endif

std::size_t findSeparator(const std::string& str)
{
  const size_t pos_slash = str.find('/');
  const size_t pos_backslash = str.find('\\');

  if (pos_slash != std::string::npos && pos_backslash != std::string::npos)
    return std::min(pos_slash, pos_backslash);

  if (pos_slash != std::string::npos)
    return pos_slash;

  if (pos_backslash != std::string::npos)
    return pos_backslash;

  return std::string::npos;
}

/** @brief Resource locator implementation using a provided function to locate file resources */
class TestResourceLocator : public tesseract::common::ResourceLocator
{
public:
  using Ptr = std::shared_ptr<TestResourceLocator>;
  using ConstPtr = std::shared_ptr<const TestResourceLocator>;

  ~TestResourceLocator() override = default;

  tesseract::common::Resource::Ptr locateResource(const std::string& url) const override final
  {
    std::string mod_url = url;
    if (url.find("package://tesseract") == 0)
    {
      mod_url.erase(0, strlen("package://tesseract"));
      const size_t pos = findSeparator(mod_url);
      if (pos == std::string::npos)
        return nullptr;

      mod_url.erase(0, pos);

      std::filesystem::path file_path(__FILE__);
      std::string package_path = file_path.parent_path().parent_path().parent_path().string();

      if (package_path.empty())
        return nullptr;

      mod_url = package_path + mod_url;
    }

    if (!std::filesystem::path(mod_url).is_absolute())
      return nullptr;

    return std::make_shared<tesseract::common::SimpleLocatedResource>(
        url, mod_url, std::make_shared<TestResourceLocator>(*this));
  }
};

TEST(ResourceLocatorUnit, SimpleResourceLocatorUnit)  // NOLINT
{
  using namespace tesseract::common;
  std::filesystem::path file_path(__FILE__);
  std::filesystem::path package_path = file_path.parent_path().parent_path().parent_path();

  ResourceLocator::Ptr locator = std::make_shared<TestResourceLocator>();

  Resource::Ptr resource = locator->locateResource("package://tesseract/package.xml");
  EXPECT_TRUE(resource != nullptr);
  EXPECT_TRUE(resource->isFile());
  EXPECT_EQ(resource->getUrl(), "package://tesseract/package.xml");
  EXPECT_EQ(std::filesystem::path(resource->getFilePath()), (package_path / "package.xml"));
  EXPECT_FALSE(resource->getResourceContents().empty());
  EXPECT_TRUE(resource->getResourceContentStream() != nullptr);

  const std::string separator(1, std::filesystem::path::preferred_separator);
  Resource::Ptr sub_resource = resource->locateResource("colcon.pkg");
  EXPECT_TRUE(sub_resource != nullptr);
  EXPECT_TRUE(sub_resource->isFile());
  EXPECT_EQ(sub_resource->getUrl(), "package://tesseract" + separator + "colcon.pkg");
  EXPECT_EQ(std::filesystem::path(sub_resource->getFilePath()), (package_path / "colcon.pkg"));
  EXPECT_FALSE(sub_resource->getResourceContents().empty());
  EXPECT_TRUE(sub_resource->getResourceContentStream() != nullptr);

  tesseract::common::Resource::Ptr sub_resource_empty = sub_resource->locateResource("");
  EXPECT_TRUE(sub_resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_empty = locator->locateResource("");
  EXPECT_TRUE(resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_does_not_exist = locator->locateResource("package://tesseract/"
                                                                                     "does_not_exist.txt");
  EXPECT_TRUE(resource_does_not_exist != nullptr);
  EXPECT_TRUE(resource_does_not_exist->getResourceContents().empty());
  EXPECT_TRUE(resource_does_not_exist->getResourceContentStream() == nullptr);
}

TEST(ResourceLocatorUnit, GeneralResourceLocatorUnit1)  // NOLINT
{
  using namespace tesseract::common;
  std::filesystem::path package_path(TESSERACT_INSTALL_PREFIX);
  package_path.append("share/tesseract");

  ResourceLocator::Ptr locator = std::make_shared<GeneralResourceLocator>();

  Resource::Ptr resource = locator->locateResource("package://tesseract/package.xml");
  EXPECT_TRUE(resource != nullptr);
  EXPECT_TRUE(resource->isFile());
  EXPECT_EQ(resource->getUrl(), "package://tesseract/package.xml");
  EXPECT_EQ(std::filesystem::path(resource->getFilePath()), (package_path / "package.xml"));
  EXPECT_FALSE(resource->getResourceContents().empty());
  EXPECT_TRUE(resource->getResourceContentStream() != nullptr);

  const std::string separator(1, std::filesystem::path::preferred_separator);
  Resource::Ptr sub_resource = resource->locateResource("package.dsv");
  EXPECT_TRUE(sub_resource != nullptr);
  EXPECT_TRUE(sub_resource->isFile());
  EXPECT_EQ(sub_resource->getUrl(), "package://tesseract" + separator + "package.dsv");
  EXPECT_EQ(std::filesystem::path(sub_resource->getFilePath()), (package_path / "package.dsv"));
  EXPECT_FALSE(sub_resource->getResourceContents().empty());
  EXPECT_TRUE(sub_resource->getResourceContentStream() != nullptr);

  tesseract::common::Resource::Ptr sub_resource_empty = sub_resource->locateResource("");
  EXPECT_TRUE(sub_resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_empty = locator->locateResource("");
  EXPECT_TRUE(resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_does_not_exist = locator->locateResource("package://tesseract/"
                                                                                     "does_not_exist.txt");
  EXPECT_TRUE(resource_does_not_exist != nullptr);
  EXPECT_TRUE(resource_does_not_exist->getResourceContents().empty());
  EXPECT_TRUE(resource_does_not_exist->getResourceContentStream() == nullptr);
}

TEST(ResourceLocatorUnit, GeneralResourceLocatorUnit2)  // NOLINT
{
  using namespace tesseract::common;
  std::filesystem::path package_path(TESSERACT_INSTALL_PREFIX);
  package_path.append("share/tesseract");

  ResourceLocator::Ptr locator = std::make_shared<GeneralResourceLocator>();

  Resource::Ptr resource = locator->locateResource("package://tesseract/package.xml");
  EXPECT_TRUE(resource != nullptr);
  EXPECT_TRUE(resource->isFile());
  EXPECT_EQ(resource->getUrl(), "package://tesseract/package.xml");
  EXPECT_EQ(std::filesystem::path(resource->getFilePath()), (package_path / "package.xml"));
  EXPECT_FALSE(resource->getResourceContents().empty());
  EXPECT_TRUE(resource->getResourceContentStream() != nullptr);

  const std::string separator(1, std::filesystem::path::preferred_separator);
  Resource::Ptr sub_resource = resource->locateResource("package.dsv");
  EXPECT_TRUE(sub_resource != nullptr);
  EXPECT_TRUE(sub_resource->isFile());
  EXPECT_EQ(sub_resource->getUrl(), "package://tesseract" + separator + "package.dsv");
  EXPECT_EQ(std::filesystem::path(sub_resource->getFilePath()), (package_path / "package.dsv"));
  EXPECT_FALSE(sub_resource->getResourceContents().empty());
  EXPECT_TRUE(sub_resource->getResourceContentStream() != nullptr);

  tesseract::common::Resource::Ptr sub_resource_empty = sub_resource->locateResource("");
  EXPECT_TRUE(sub_resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_empty = locator->locateResource("");
  EXPECT_TRUE(resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_does_not_exist = locator->locateResource("package://tesseract/"
                                                                                     "does_not_exist.txt");
  EXPECT_TRUE(resource_does_not_exist != nullptr);
  EXPECT_TRUE(resource_does_not_exist->getResourceContents().empty());
  EXPECT_TRUE(resource_does_not_exist->getResourceContentStream() == nullptr);
}

TEST(ResourceLocatorUnit, ByteResourceUnit)  // NOLINT
{
  using namespace tesseract::common;
  std::filesystem::path file_path(__FILE__);
  std::filesystem::path package_path = file_path.parent_path().parent_path().parent_path();

  ResourceLocator::Ptr locator = std::make_shared<TestResourceLocator>();
  Resource::Ptr resource = locator->locateResource("package://tesseract/package.xml");

  auto byte_resource = std::make_shared<BytesResource>(
      "package://tesseract/package.xml", std::vector<uint8_t>({ 1, 2, 3, 4 }), resource);
  EXPECT_TRUE(byte_resource != nullptr);
  EXPECT_FALSE(byte_resource->isFile());
  EXPECT_EQ(byte_resource->getUrl(), "package://tesseract/package.xml");
  EXPECT_TRUE(byte_resource->getFilePath().empty());
  EXPECT_FALSE(byte_resource->getResourceContents().empty());
  EXPECT_TRUE(byte_resource->getResourceContentStream() != nullptr);

  const std::string separator(1, std::filesystem::path::preferred_separator);
  Resource::Ptr sub_resource = byte_resource->locateResource("colcon.pkg");
  EXPECT_TRUE(sub_resource != nullptr);
  EXPECT_TRUE(sub_resource->isFile());
  EXPECT_EQ(sub_resource->getUrl(), "package://tesseract" + separator + "colcon.pkg");
  EXPECT_EQ(std::filesystem::path(sub_resource->getFilePath()), (package_path / "colcon.pkg"));
  EXPECT_FALSE(sub_resource->getResourceContents().empty());
  EXPECT_TRUE(sub_resource->getResourceContentStream() != nullptr);

  tesseract::common::Resource::Ptr resource_empty = byte_resource->locateResource("");
  EXPECT_TRUE(resource_empty == nullptr);

  tesseract::common::Resource::Ptr resource_does_not_exist = byte_resource->locateResource("package://tesseract/"
                                                                                           "does_not_exist.txt");
  EXPECT_TRUE(resource_does_not_exist != nullptr);
  EXPECT_TRUE(resource_does_not_exist->getResourceContents().empty());
  EXPECT_TRUE(resource_does_not_exist->getResourceContentStream() == nullptr);
}

TEST(ResourceLocatorUnit, SimpleLocatedResourceSerializUnit)  // NOLINT
{
  using namespace tesseract::common;
  SimpleLocatedResource resource("url", "file_path");
  tesseract::common::testSerialization<SimpleLocatedResource>(resource, "SimpleLocatedResource");
}

TEST(ResourceLocatorUnit, BytesResourceSerializUnit)  // NOLINT
{
  using namespace tesseract::common;
  BytesResource resource("url", { 1, 2, 3, 4, 5 });
  tesseract::common::testSerialization<BytesResource>(resource, "BytesResource");
}

TEST(ResourceLocatorUnit, ResolveRelativeResourceUrl)  // NOLINT
{
  using namespace tesseract::common;
  const std::string sep(1, std::filesystem::path::preferred_separator);
  EXPECT_EQ(resolveRelativeResourceUrl("package://foo/meshes/a.stl", "b.stl"), "package://foo/meshes" + sep + "b.stl");
  EXPECT_EQ(resolveRelativeResourceUrl("C:\\x\\a.stl", "b.stl"), "C:\\x" + sep + "b.stl");
  EXPECT_EQ(resolveRelativeResourceUrl("a.stl", "b.stl"), "");
}

#ifdef TESSERACT_COMMON_HAS_CONDA_ARCHIVES
namespace
{
using tesseract::common::test::CondaFixtureFile;
using tesseract::common::test::CondaFixtureSpec;

std::string boxStl()
{
  std::filesystem::path file_path(__FILE__);
  return tesseract::common::test::readFixtureFile(file_path.parent_path().parent_path().parent_path() / "support" /
                                                  "meshes" / "box_2m.stl");
}

std::vector<CondaFixtureFile> baseFiles()
{
  using tesseract::common::test::fixturePackageXml;
  return { { "share/foo/package.xml", fixturePackageXml("foo"), "" },
           { "share/foo/meshes/box.stl", boxStl(), "" },
           { "share/foo/meshes/box_link.stl", "", "box.stl" },
           { "share/foo/sub/bar.yaml", "value: 42\nnested: !include inc.yaml\n", "" },
           { "share/foo/sub/inc.yaml", "x: 1\n", "" },
           { "share/baz/package.xml", fixturePackageXml("baz"), "" },
           { "share/baz/nested/inner/package.xml", fixturePackageXml("inner"), "" },
           { "lib/libdummy.so", std::string(1000, 'x'), "" } };
}

const std::string kIndexObject =
    R"({"version": 1, "packages": {"foo": "share/foo", "baz": "share/baz/", "ghost": "share/ghost"}, )"
    R"("robots": ["r1"]})";

const std::string kIndexDataFile = "tesseract_resource_index:\n  version: 1\n  packages:\n    foo: share/foo\n    baz: "
                                   "share/baz\n    ghost: share/ghost\n  robots: [r1]\n";

std::string escapeJsonString(const std::string& s)
{
  std::string out;
  for (char c : s)
  {
    if (c == '"' || c == '\\')
      out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

struct CondaFixtures
{
  std::filesystem::path dir;
  std::filesystem::path t, t_indexed, t_indexed_str, t_dataonly, t_conflict, t_badabout;
  std::vector<uint8_t> t_bytes;

  CondaFixtures()
  {
    using namespace tesseract::common::test;
    dir = makeFixtureTempDir("tesseract_conda_resource_unit");

    CondaFixtureSpec spec;
    spec.name = "t";
    spec.files = baseFiles();
    spec.about_json = R"({"summary": "test package"})";
    t = dir / "t.conda";
    writeCondaPackage(t, spec);
    t_bytes = buildCondaPackage(spec);

    CondaFixtureSpec indexed = spec;
    indexed.name = "t_indexed";
    indexed.about_json = R"({"summary": "x", "extra": {"tesseract_resource_index": )" + kIndexObject + "}}";
    indexed.files.push_back({ "share/tesseract/resource_index/t_indexed.yaml", kIndexDataFile, "" });
    t_indexed = dir / "t_indexed.conda";
    writeCondaPackage(t_indexed, indexed);

    CondaFixtureSpec indexed_str = indexed;
    indexed_str.name = "t_indexed_str";
    indexed_str.about_json = R"({"extra": {"tesseract_resource_index": ")" + escapeJsonString(kIndexObject) + R"("}})";
    t_indexed_str = dir / "t_indexed_str.conda";
    writeCondaPackage(t_indexed_str, indexed_str);

    CondaFixtureSpec dataonly = spec;
    dataonly.name = "t_dataonly";
    dataonly.files.push_back({ "share/tesseract/resource_index/t_dataonly.yaml", kIndexDataFile, "" });
    t_dataonly = dir / "t_dataonly.conda";
    writeCondaPackage(t_dataonly, dataonly);

    CondaFixtureSpec conflict = spec;
    conflict.name = "t_conflict";
    conflict.about_json =
        R"({"extra": {"tesseract_resource_index": {"version": 1, "packages": {"foo": "share/foo"}}}})";
    conflict.files.push_back({ "share/tesseract/resource_index/t_conflict.yaml",
                               "tesseract_resource_index:\n  version: 1\n  packages:\n    foo: share/baz\n",
                               "" });
    t_conflict = dir / "t_conflict.conda";
    writeCondaPackage(t_conflict, conflict);

    CondaFixtureSpec badabout = spec;
    badabout.name = "t_badabout";
    badabout.about_json = R"({"extra": {"tesseract_resource_index": {"packages": {"foo": "share/foo"}}}})";
    badabout.files.push_back({ "share/tesseract/resource_index/t_badabout.yaml", kIndexDataFile, "" });
    t_badabout = dir / "t_badabout.conda";
    writeCondaPackage(t_badabout, badabout);
  }

  ~CondaFixtures()
  {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  CondaFixtures(const CondaFixtures&) = delete;
  CondaFixtures& operator=(const CondaFixtures&) = delete;
  CondaFixtures(CondaFixtures&&) = delete;
  CondaFixtures& operator=(CondaFixtures&&) = delete;
};

const CondaFixtures& fixtures()
{
  static const CondaFixtures f;
  return f;
}

std::string generic(const std::filesystem::path& p) { return p.generic_string(); }

std::string asString(const std::vector<uint8_t>& v) { return { v.begin(), v.end() }; }

/** @brief Restores the process-wide read settings */
struct ReadLimitGuard
{
  std::size_t full = tesseract::common::CondaPackageArchive::fullLoadLimit();
  std::size_t cache = tesseract::common::CondaPackageArchive::readCacheLimit();
  ReadLimitGuard() = default;
  ~ReadLimitGuard()
  {
    tesseract::common::CondaPackageArchive::setFullLoadLimit(full);
    tesseract::common::CondaPackageArchive::setReadCacheLimit(cache);
  }
  ReadLimitGuard(const ReadLimitGuard&) = delete;
  ReadLimitGuard& operator=(const ReadLimitGuard&) = delete;
  ReadLimitGuard(ReadLimitGuard&&) = delete;
  ReadLimitGuard& operator=(ReadLimitGuard&&) = delete;
};
}  // namespace

TEST(TesseractCommonResourceLocatorUnit, CondaPackagePathParse)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  // No ".conda": rejected without touching the filesystem
  EXPECT_FALSE(parseCondaPackagePath("/usr/share/foo/package.xml").has_value());
  EXPECT_FALSE(isCondaPackagePath(""));

  // Archive with an inner path
  auto cp = parseCondaPackagePath(generic(f.t / "share" / "foo" / "meshes" / "box.stl"));
  ASSERT_TRUE(cp.has_value());
  EXPECT_EQ(cp->archive_path, generic(f.t));
  EXPECT_EQ(cp->inner_path, "share/foo/meshes/box.stl");

  // Trailing .conda, no inner path
  cp = parseCondaPackagePath(generic(f.t));
  ASSERT_TRUE(cp.has_value());
  EXPECT_EQ(cp->archive_path, generic(f.t));
  EXPECT_TRUE(cp->inner_path.empty());

  // Trailing slash and dot segments are normalized
  cp = parseCondaPackagePath(generic(f.t) + "/share/./foo/../baz/");
  ASSERT_TRUE(cp.has_value());
  EXPECT_EQ(cp->inner_path, "share/baz");

  // Case-insensitive extension check
  EXPECT_TRUE(parseCondaPackagePath(generic(f.dir / "T.CONDA")) == std::nullopt);  // no such file

  // A directory named *.conda is not an archive
  std::filesystem::create_directories(f.dir / "dir.conda" / "share");
  EXPECT_FALSE(parseCondaPackagePath(generic(f.dir / "dir.conda" / "share")).has_value());

  // A missing archive
  EXPECT_FALSE(parseCondaPackagePath(generic(f.dir / "missing.conda" / "share")).has_value());

  // Backslashes are converted on every platform (Windows-style input)
  std::string backslashed = generic(f.t / "share" / "foo" / "meshes" / "box.stl");
  std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
  cp = parseCondaPackagePath(backslashed);
  ASSERT_TRUE(cp.has_value());
  EXPECT_EQ(cp->archive_path, generic(f.t));
  EXPECT_EQ(cp->inner_path, "share/foo/meshes/box.stl");

  // Inverse
  EXPECT_EQ(makeCondaPackagePath("/x/a.conda", "share/foo"), "/x/a.conda/share/foo");
  EXPECT_EQ(makeCondaPackagePath("/x/a.conda/", "/share//foo/"), "/x/a.conda/share/foo");
  EXPECT_EQ(makeCondaPackagePath("/x/a.conda", ""), "/x/a.conda");
  EXPECT_EQ(normalizeCondaInnerPath("./a\\b/../c/"), "a/c");
}

TEST(TesseractCommonResourceLocatorUnit, CondaPackageArchiveOpen)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  auto from_file = CondaPackageArchive::openFile(f.t);
  ASSERT_TRUE(from_file != nullptr);
  auto from_bytes =
      CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(f.t_bytes), "/virtual/t.conda");
  ASSERT_TRUE(from_bytes != nullptr);

  EXPECT_FALSE(from_file->isInMemory());
  EXPECT_TRUE(from_bytes->isInMemory());
  EXPECT_EQ(from_bytes->sourceName(), "/virtual/t.conda");
  EXPECT_EQ(from_file->files(), from_bytes->files());
  EXPECT_EQ(from_file->files().size(), baseFiles().size());
  EXPECT_TRUE(std::is_sorted(from_file->files().begin(), from_file->files().end()));

  EXPECT_EQ(from_file->name(), "t");
  EXPECT_EQ(from_file->version(), "1.0");
  EXPECT_EQ(from_file->build(), "h0_0");

  EXPECT_TRUE(from_file->hasFile("share/foo/package.xml"));
  EXPECT_TRUE(from_file->hasFile("/share/foo/package.xml"));
  EXPECT_FALSE(from_file->hasFile("share/foo"));
  EXPECT_TRUE(from_file->hasDirectory("share/foo"));
  EXPECT_TRUE(from_file->hasDirectory("share/foo/"));
  EXPECT_TRUE(from_file->hasDirectory(""));
  EXPECT_FALSE(from_file->hasDirectory("share/foo/package.xml"));
  EXPECT_FALSE(from_file->hasDirectory("share/missing"));
  EXPECT_EQ(from_file->listFiles("share/foo/meshes/").size(), 2);
  EXPECT_TRUE(from_file->listFiles("nothing/").empty());

  ASSERT_TRUE(from_file->infoFile("about.json").has_value());
  EXPECT_EQ(*from_file->infoFile("about.json"), R"({"summary": "test package"})");
  EXPECT_TRUE(from_file->infoFile("index.json").has_value());
  EXPECT_FALSE(from_file->infoFile("missing.json").has_value());
  ASSERT_TRUE(from_file->contentSize().has_value());

  for (const auto& path : { "share/foo/meshes/box.stl", "share/foo/sub/bar.yaml", "lib/libdummy.so" })
  {
    EXPECT_EQ(from_file->readFile(path), from_bytes->readFile(path)) << path;
  }
  EXPECT_EQ(asString(from_file->readFile("share/foo/meshes/box.stl")), boxStl());
  EXPECT_EQ(asString(from_file->readFile("share/foo/meshes/box_link.stl")), boxStl());
  EXPECT_TRUE(from_file->readFile("share/foo/missing.txt").empty());

  // Not an archive
  EXPECT_TRUE(CondaPackageArchive::openFile(f.dir / "missing.conda") == nullptr);
  EXPECT_TRUE(CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{ 1, 2 }),
                                             "bad.conda") == nullptr);
  EXPECT_TRUE(CondaPackageArchive::openBytes(nullptr, "null.conda") == nullptr);
}

TEST(TesseractCommonResourceLocatorUnit, CondaPackageArchiveManifestFallback)  // NOLINT
{
  using namespace tesseract::common;
  CondaFixtureSpec spec;
  spec.files = baseFiles();
  spec.write_files_manifest = false;
  auto archive = CondaPackageArchive::openBytes(
      std::make_shared<const std::vector<uint8_t>>(tesseract::common::test::buildCondaPackage(spec)), "/v/p.conda");
  ASSERT_TRUE(archive != nullptr);
  EXPECT_EQ(archive->files().size(), baseFiles().size());
  EXPECT_TRUE(archive->hasFile("share/foo/meshes/box.stl"));

  // Without paths.json the content size is unknown, so the archive is streamed
  spec.write_files_manifest = true;
  spec.write_paths_json = false;
  spec.write_index_json = false;
  archive = CondaPackageArchive::openBytes(
      std::make_shared<const std::vector<uint8_t>>(tesseract::common::test::buildCondaPackage(spec)), "/v/q.conda");
  ASSERT_TRUE(archive != nullptr);
  EXPECT_FALSE(archive->contentSize().has_value());
  EXPECT_TRUE(archive->name().empty());
  EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());
  EXPECT_FALSE(archive->isFullyLoaded());

  // Neither manifest
  spec.write_files_manifest = false;
  EXPECT_TRUE(CondaPackageArchive::openBytes(
                  std::make_shared<const std::vector<uint8_t>>(tesseract::common::test::buildCondaPackage(spec)),
                  "/v/r.conda") == nullptr);
}

TEST(TesseractCommonResourceLocatorUnit, CondaPackageArchiveReadStrategy)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();
  ReadLimitGuard guard;
  auto bytes = std::make_shared<const std::vector<uint8_t>>(f.t_bytes);

  // Default limit: the first read loads everything, later reads don't decompress again
  {
    auto archive = CondaPackageArchive::openBytes(bytes, "/v/t.conda");
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(archive->contentPassCount(), 0);
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());
    EXPECT_EQ(archive->contentPassCount(), 1);
    EXPECT_TRUE(archive->isFullyLoaded());
    EXPECT_FALSE(archive->readFile("share/foo/sub/bar.yaml").empty());
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box_link.stl")), boxStl());
    EXPECT_EQ(archive->contentPassCount(), 1);

    archive->releaseContents();
    EXPECT_FALSE(archive->isFullyLoaded());
    EXPECT_FALSE(archive->readFile("lib/libdummy.so").empty());
    EXPECT_EQ(archive->contentPassCount(), 2);
    EXPECT_TRUE(archive->isFullyLoaded());
  }

  // Streaming
  CondaPackageArchive::setFullLoadLimit(0);
  {
    auto archive = CondaPackageArchive::openBytes(bytes, "/v/t.conda");
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());
    EXPECT_FALSE(archive->isFullyLoaded());
    EXPECT_EQ(archive->contentPassCount(), 1);
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());  // LRU hit
    EXPECT_EQ(archive->contentPassCount(), 1);
    EXPECT_EQ(asString(archive->readFile("share/foo/sub/inc.yaml")), "x: 1\n");
    EXPECT_EQ(archive->contentPassCount(), 2);
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box_link.stl")), boxStl());
    EXPECT_TRUE(archive->readFile("share/foo/missing.txt").empty());

    // preload() ignores the limit
    archive->preload();
    EXPECT_TRUE(archive->isFullyLoaded());
    const std::size_t passes = archive->contentPassCount();
    EXPECT_EQ(asString(archive->readFile("lib/libdummy.so")), std::string(1000, 'x'));
    EXPECT_EQ(archive->contentPassCount(), passes);
    archive->releaseContents();
    EXPECT_FALSE(archive->isFullyLoaded());
    EXPECT_EQ(asString(archive->readFile("lib/libdummy.so")), std::string(1000, 'x'));
    EXPECT_FALSE(archive->isFullyLoaded());
  }

  // readFiles: one pass for several files
  {
    auto archive = CondaPackageArchive::openBytes(bytes, "/v/t.conda");
    ASSERT_TRUE(archive != nullptr);
    auto files = archive->readFiles(
        { "share/foo/meshes/box.stl", "share/foo/sub/bar.yaml", "/share/baz/package.xml", "share/foo/missing.txt" });
    EXPECT_EQ(archive->contentPassCount(), 1);
    EXPECT_EQ(files.size(), 3);
    EXPECT_EQ(asString(files["share/foo/meshes/box.stl"]), boxStl());
    EXPECT_EQ(files.count("/share/baz/package.xml"), 1);
    EXPECT_EQ(files.count("share/foo/missing.txt"), 0);
  }

  // A tiny LRU cache still returns correct data
  CondaPackageArchive::setReadCacheLimit(10);
  {
    auto archive = CondaPackageArchive::openBytes(bytes, "/v/t.conda");
    ASSERT_TRUE(archive != nullptr);
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());
    EXPECT_EQ(asString(archive->readFile("share/foo/meshes/box.stl")), boxStl());
    EXPECT_EQ(archive->contentPassCount(), 2);
  }
}

TEST(TesseractCommonResourceLocatorUnit, CondaPackageIdentity)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  // Reference digests (FIPS 180-2 test vectors)
  const std::string abc = "abc";
  EXPECT_EQ(computeSha256Hex(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()),  // NOLINT
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(computeSha256Hex(nullptr, 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  EXPECT_EQ(computeSha256Hex(reinterpret_cast<const uint8_t*>(two_blocks.data()), two_blocks.size()),  // NOLINT
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

  auto archive = CondaPackageArchive::openFile(f.t);
  ASSERT_TRUE(archive != nullptr);
  const auto id = archive->identity();
  EXPECT_EQ(id.name, "t");
  EXPECT_EQ(id.version, "1.0");
  EXPECT_EQ(id.build, "h0_0");
  EXPECT_EQ(id.sha256, computeSha256Hex(f.t_bytes.data(), f.t_bytes.size()));
  EXPECT_EQ(id.sha256.size(), 64);

  auto mem = CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(f.t_bytes), "/v/t.conda");
  EXPECT_EQ(mem->identity(), id);
}

TEST(TesseractCommonResourceLocatorUnit, CondaPackageArchiveCache)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();
  CondaPackageArchiveCache cache;

  auto a = cache.get(generic(f.t));
  ASSERT_TRUE(a != nullptr);
  EXPECT_EQ(cache.get(generic(f.t)), a);          // shared while alive
  EXPECT_EQ(cache.get(generic(f.t) + "/./"), a);  // same file, other spelling
  EXPECT_TRUE(cache.get(generic(f.dir / "missing.conda")) == nullptr);

  // Lookup by identity
  auto id = a->identity();
  EXPECT_EQ(cache.findByIdentity(id), a);
  CondaPackageIdentity no_hash = id;
  no_hash.sha256.clear();
  EXPECT_EQ(cache.findByIdentity(no_hash), a);
  CondaPackageIdentity wrong_hash = id;
  wrong_hash.sha256 = std::string(64, '0');
  EXPECT_TRUE(cache.findByIdentity(wrong_hash) == nullptr);
  CondaPackageIdentity name_only;
  name_only.name = "t";
  EXPECT_TRUE(cache.findByIdentity(name_only) == nullptr);

  // In-memory archives under a virtual path
  auto mem = CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(f.t_bytes), "/remote/t.conda");
  EXPECT_FALSE(cache.hasKey("/remote/t.conda"));
  EXPECT_FALSE(parseCondaPackagePath("/remote/t.conda/share/foo", cache).has_value());
  cache.add("/remote/t.conda", mem);
  EXPECT_TRUE(cache.hasKey("/remote/t.conda"));
  EXPECT_EQ(cache.get("/remote/t.conda"), mem);
  auto cp = parseCondaPackagePath("/remote/t.conda/share/foo/package.xml", cache);
  ASSERT_TRUE(cp.has_value());
  EXPECT_EQ(cp->archive_path, "/remote/t.conda");
  EXPECT_EQ(cp->inner_path, "share/foo/package.xml");

  auto res = locateCondaResource(
      "file:///remote/t.conda/share/foo/package.xml", "/remote/t.conda/share/foo/package.xml", nullptr, cache);
  ASSERT_TRUE(res != nullptr);
  EXPECT_FALSE(res->getResourceContents().empty());

  cache.remove("/remote/t.conda");
  EXPECT_FALSE(cache.hasKey("/remote/t.conda"));

  // Resolver
  int calls = 0;
  cache.setResolver([&](const CondaPackageIdentity& requested) -> CondaPackageArchive::ConstPtr {
    ++calls;
    return (requested.name == "t") ? mem : nullptr;
  });
  cache.clear();
  a.reset();
  CondaPackageIdentity other = id;
  other.name = "other";
  EXPECT_TRUE(cache.resolve(other) == nullptr);
  EXPECT_EQ(cache.resolve(id), mem);
  EXPECT_EQ(calls, 2);
  cache.setResolver(nullptr);
  EXPECT_TRUE(cache.resolve(id) == nullptr);
}

TEST(TesseractCommonResourceLocatorUnit, CondaFindRosPackages)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();
  auto archive = CondaPackageArchive::openFile(f.t);
  ASSERT_TRUE(archive != nullptr);

  auto packages = findRosPackagesInCondaArchive(*archive, "share");
  EXPECT_EQ(packages.size(), 2);  // nested/inner is under baz and is skipped
  EXPECT_EQ(packages["foo"], "share/foo");
  EXPECT_EQ(packages["baz"], "share/baz");

  packages = findRosPackagesInCondaArchive(*archive, "share/foo/");
  EXPECT_EQ(packages.size(), 1);
  EXPECT_EQ(packages["foo"], "share/foo");

  packages = findRosPackagesInCondaArchive(*archive, "");
  EXPECT_EQ(packages.size(), 2);

  EXPECT_TRUE(findRosPackagesInCondaArchive(*archive, "lib").empty());
}

TEST(TesseractCommonResourceLocatorUnit, CondaResourceIndexParse)  // NOLINT
{
  using namespace tesseract::common;

  // Valid object with unknown keys kept in raw
  auto index = parseTesseractResourceIndex(YAML::Load(
      R"({"version": 1, "packages": {"foo": "share/foo/", "root": ".", "w": "share\\w"}, "robots": ["r1"]})"));
  EXPECT_EQ(index.version, 1);
  EXPECT_EQ(index.packages.size(), 3);
  EXPECT_EQ(index.packages["foo"], "share/foo");
  EXPECT_EQ(index.packages["root"], "");
  EXPECT_EQ(index.packages["w"], "share/w");
  ASSERT_TRUE(index.raw["robots"].IsSequence());
  EXPECT_EQ(index.raw["robots"][0].as<std::string>(), "r1");

  // conda-build writes recipe extra values as strings
  EXPECT_EQ(parseTesseractResourceIndex(YAML::Load(R"({"version": "1", "packages": {}})")).version, 1);

  // Rejected
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"packages": {"foo": "share/foo"}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 2, "packages": {}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": "one", "packages": {}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": {"foo": "/share/foo"}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": {"foo": "C:/share"}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": {"foo": "../foo"}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": {"foo": "a/../../b"}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": ["share/foo"]})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1, "packages": {"foo": ["x"]}})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"({"version": 1})")));
  EXPECT_ANY_THROW(parseTesseractResourceIndex(YAML::Load(R"(["x"])")));

  // Data-side file
  auto from_file = parseTesseractResourceIndexFile(kIndexDataFile);
  EXPECT_EQ(from_file.source, TesseractResourceIndexSource::DataFile);
  EXPECT_EQ(from_file.packages.size(), 3);
  EXPECT_ANY_THROW(parseTesseractResourceIndexFile("version: 1\npackages: {}\n"));
  EXPECT_ANY_THROW(parseTesseractResourceIndexFile("tesseract_resource_index: [\n"));

  // about.json: object and string forms give the same packages as the data-side file
  auto from_object =
      parseTesseractResourceIndexFromAboutJson(R"({"extra": {"tesseract_resource_index": )" + kIndexObject + "}}");
  ASSERT_TRUE(from_object.has_value());
  EXPECT_EQ(from_object->source, TesseractResourceIndexSource::AboutJson);
  EXPECT_EQ(from_object->packages, from_file.packages);
  auto from_string = parseTesseractResourceIndexFromAboutJson(R"({"extra": {"tesseract_resource_index": ")" +
                                                              escapeJsonString(kIndexObject) + R"("}})");
  ASSERT_TRUE(from_string.has_value());
  EXPECT_EQ(from_string->packages, from_file.packages);

  EXPECT_FALSE(parseTesseractResourceIndexFromAboutJson(R"({"summary": "x"})").has_value());
  EXPECT_FALSE(parseTesseractResourceIndexFromAboutJson(R"({"extra": {"other": 1}})").has_value());
  EXPECT_ANY_THROW(parseTesseractResourceIndexFromAboutJson(R"({"extra": {"tesseract_resource_index": "{not json"}})"));
  EXPECT_ANY_THROW(parseTesseractResourceIndexFromAboutJson(R"({"extra": {"tesseract_resource_index": "plain"}})"));
  EXPECT_ANY_THROW(parseTesseractResourceIndexFromAboutJson("{\"extra\": [1, 2"));
}

TEST(TesseractCommonResourceLocatorUnit, CondaResourceIndexRead)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  // No index of either kind: nothing is decompressed
  {
    auto archive = CondaPackageArchive::openFile(f.t);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_FALSE(readTesseractResourceIndex(*archive).has_value());
    EXPECT_EQ(archive->contentPassCount(), 0);
  }

  // about.json object and string forms; the missing directory is dropped
  for (const auto& path : { f.t_indexed, f.t_indexed_str })
  {
    auto archive = CondaPackageArchive::openFile(path);
    ASSERT_TRUE(archive != nullptr);
    auto index = readTesseractResourceIndex(*archive);
    ASSERT_TRUE(index.has_value()) << path;
    EXPECT_EQ(index->source, TesseractResourceIndexSource::AboutJson);
    EXPECT_EQ(index->packages.size(), 2);
    EXPECT_EQ(index->packages["foo"], "share/foo");
    EXPECT_EQ(index->packages["baz"], "share/baz");
    EXPECT_EQ(archive->contentPassCount(), 0);
    EXPECT_EQ(index->raw["robots"][0].as<std::string>(), "r1");

    // The data-side copy agrees
    auto data_side = readTesseractResourceIndexFromDataFiles(*archive);
    ASSERT_TRUE(data_side.has_value());
    EXPECT_EQ(data_side->packages, index->packages);
  }

  // Data-side file only
  {
    auto archive = CondaPackageArchive::openFile(f.t_dataonly);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_FALSE(readTesseractResourceIndexFromAboutJson(*archive).has_value());
    auto index = readTesseractResourceIndex(*archive);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->source, TesseractResourceIndexSource::DataFile);
    ASSERT_EQ(index->source_files.size(), 1);
    EXPECT_EQ(index->source_files[0], "share/tesseract/resource_index/t_dataonly.yaml");
    EXPECT_EQ(index->packages.size(), 2);
  }

  // about.json wins and the in-package file is never read
  {
    auto archive = CondaPackageArchive::openFile(f.t_conflict);
    ASSERT_TRUE(archive != nullptr);
    auto index = readTesseractResourceIndex(*archive);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(index->packages.size(), 1);
    EXPECT_EQ(index->packages["foo"], "share/foo");
    EXPECT_EQ(archive->contentPassCount(), 0);
  }

  // A broken about.json entry is an error, with no fallback to the valid data-side file
  {
    auto archive = CondaPackageArchive::openFile(f.t_badabout);
    ASSERT_TRUE(archive != nullptr);
    EXPECT_FALSE(readTesseractResourceIndex(*archive).has_value());
    EXPECT_EQ(archive->contentPassCount(), 0);
    EXPECT_ANY_THROW(readTesseractResourceIndexFromAboutJson(*archive));
    EXPECT_TRUE(readTesseractResourceIndexFromDataFiles(*archive).has_value());
  }
}

TEST(TesseractCommonResourceLocatorUnit, CondaArchive)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  // Archive + inner directory: scans the manifest for package.xml
  {
    GeneralResourceLocator locator(std::vector<std::string>{});
    EXPECT_TRUE(locator.addPath(f.t / "share"));
    EXPECT_TRUE(locator.locateResource("package://foo/package.xml") != nullptr);
    EXPECT_TRUE(locator.locateResource("package://baz/package.xml") != nullptr);
    EXPECT_TRUE(locator.locateResource("package://inner/package.xml") == nullptr);
  }

  // Bare archive without an index fails; a missing inner directory fails
  {
    GeneralResourceLocator locator(std::vector<std::string>{});
    EXPECT_FALSE(locator.addPath(f.t));
    EXPECT_FALSE(locator.addPath(f.t / "share" / "missing"));
    EXPECT_FALSE(locator.addPath(f.t / "share" / "foo" / "package.xml"));
    EXPECT_TRUE(locator.locateResource("package://foo/package.xml") == nullptr);
  }

  // Bare archives with an index
  for (const auto& path : { f.t_indexed, f.t_indexed_str, f.t_dataonly })
  {
    GeneralResourceLocator locator(std::vector<std::string>{});
    EXPECT_TRUE(locator.addPath(path)) << path;
    EXPECT_TRUE(locator.locateResource("package://foo/package.xml") != nullptr) << path;
    EXPECT_TRUE(locator.locateResource("package://baz/package.xml") != nullptr) << path;
    EXPECT_TRUE(locator.locateResource("package://ghost/package.xml") == nullptr) << path;
  }
  {
    GeneralResourceLocator locator(std::vector<std::string>{});
    EXPECT_TRUE(locator.addPath(f.t_conflict));
    auto res = locator.locateResource("package://foo/package.xml");
    ASSERT_TRUE(res != nullptr);
    EXPECT_NE(asString(res->getResourceContents()).find("<name>foo</name>"), std::string::npos);
  }
  {
    GeneralResourceLocator locator(std::vector<std::string>{});
    EXPECT_FALSE(locator.addPath(f.t_badabout));
  }

  GeneralResourceLocator::Ptr locator = std::make_shared<GeneralResourceLocator>(std::vector<std::string>{});
  ASSERT_TRUE(locator->addPath(f.t / "share"));

  // package:// URL
  auto res = locator->locateResource("package://foo/meshes/box.stl");
  ASSERT_TRUE(res != nullptr);
  auto conda_res = std::dynamic_pointer_cast<CondaArchiveResource>(res);
  ASSERT_TRUE(conda_res != nullptr);
  EXPECT_FALSE(res->isFile());
  EXPECT_EQ(res->getUrl(), "package://foo/meshes/box.stl");
  EXPECT_EQ(res->getFilePath(), generic(f.t) + "/share/foo/meshes/box.stl");
  EXPECT_EQ(conda_res->getInnerPath(), "share/foo/meshes/box.stl");
  EXPECT_EQ(conda_res->getArchivePath(), generic(f.t));
  EXPECT_TRUE(conda_res->getArchive() != nullptr);
  EXPECT_EQ(asString(res->getResourceContents()), boxStl());
  auto stream = res->getResourceContentStream();
  ASSERT_TRUE(stream != nullptr);
  std::stringstream ss;
  ss << stream->rdbuf();
  EXPECT_EQ(ss.str(), boxStl());

  // The file path can be handed back to the locator
  auto again = locator->locateResource(res->getFilePath());
  ASSERT_TRUE(again != nullptr);
  EXPECT_EQ(asString(again->getResourceContents()), boxStl());

  // file:/// URL
  res = locator->locateResource("file://" + std::string(generic(f.t).front() == '/' ? "" : "/") + generic(f.t) +
                                "/share/foo/meshes/box.stl");
  ASSERT_TRUE(res != nullptr);
  EXPECT_FALSE(res->isFile());
  EXPECT_EQ(asString(res->getResourceContents()), boxStl());
  EXPECT_TRUE(locator->locateResource("file://" + std::string(generic(f.t).front() == '/' ? "" : "/") + generic(f.t) +
                                      "/share/foo/meshes/missing.stl") == nullptr);
  EXPECT_TRUE(locator->locateResource("package://foo/meshes/missing.stl") == nullptr);

  // Relative lookups resolve inside the archive
  auto rel = res->locateResource("box_link.stl");
  ASSERT_TRUE(rel != nullptr);
  EXPECT_EQ(asString(rel->getResourceContents()), boxStl());
  auto pkg_xml = locator->locateResource("package://foo/sub/bar.yaml");
  ASSERT_TRUE(pkg_xml != nullptr);
  auto inc = pkg_xml->locateResource("inc.yaml");
  ASSERT_TRUE(inc != nullptr);
  EXPECT_EQ(asString(inc->getResourceContents()), "x: 1\n");
  EXPECT_EQ(inc->getFilePath(), generic(f.t) + "/share/foo/sub/inc.yaml");
  EXPECT_TRUE(pkg_xml->locateResource("missing.yaml") == nullptr);
  EXPECT_TRUE(pkg_xml->locateResource("") == nullptr);

  // Absolute and package:// lookups go through the parent locator
  auto other = pkg_xml->locateResource("package://baz/package.xml");
  ASSERT_TRUE(other != nullptr);
  EXPECT_NE(asString(other->getResourceContents()).find("<name>baz</name>"), std::string::npos);

  // Windows-style input, including backslash relative lookups
  std::string backslashed = generic(f.t / "share" / "foo" / "meshes" / "box.stl");
  std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
  res = locator->locateResource(backslashed);
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(asString(res->getResourceContents()), boxStl());
  rel = res->locateResource("box_link.stl");
  ASSERT_TRUE(rel != nullptr);
  EXPECT_EQ(asString(rel->getResourceContents()), boxStl());
  rel = locator->locateResource("package://foo/sub/bar.yaml")->locateResource("..\\sub\\inc.yaml");
  ASSERT_TRUE(rel != nullptr);

  // YAML with a relative !include
  YAML::Node yaml = loadYamlFile("package://foo/sub/bar.yaml", *locator);
  EXPECT_EQ(yaml["value"].as<int>(), 42);
  EXPECT_EQ(yaml["nested"]["x"].as<int>(), 1);
  EXPECT_ANY_THROW(loadYamlFile("package://foo/sub/missing.yaml", *locator));  // NOLINT

  // Environment variable with an archive token
  {
    const std::string value = generic(f.t / "share");
#ifndef _WIN32
    setenv("TESSERACT_CONDA_UNIT_PATH", value.c_str(), 1);
#else
    _putenv_s("TESSERACT_CONDA_UNIT_PATH", value.c_str());
#endif
    GeneralResourceLocator env_locator(std::vector<std::string>{ "TESSERACT_CONDA_UNIT_PATH" });
    EXPECT_TRUE(env_locator.locateResource("package://foo/meshes/box.stl") != nullptr);
  }
}

TEST(TesseractCommonResourceLocatorUnit, CondaArchiveResourceDirect)  // NOLINT
{
  using namespace tesseract::common;
  const auto& f = fixtures();

  // Built directly from an in-memory archive, with no locator
  auto archive = CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(f.t_bytes), "mem/t.conda");
  ASSERT_TRUE(archive != nullptr);
  auto res =
      std::make_shared<CondaArchiveResource>("package://foo/meshes/box.stl", archive, "share/foo/meshes/box.stl");
  EXPECT_FALSE(res->isFile());
  EXPECT_EQ(res->getFilePath(), "mem/t.conda/share/foo/meshes/box.stl");
  EXPECT_EQ(asString(res->getResourceContents()), boxStl());
  auto rel = res->locateResource("box_link.stl");
  ASSERT_TRUE(rel != nullptr);
  EXPECT_EQ(rel->getUrl().substr(0, 20), "package://foo/meshes");
  EXPECT_EQ(asString(rel->getResourceContents()), boxStl());
  EXPECT_TRUE(res->locateResource("missing.stl") == nullptr);
  EXPECT_TRUE(res->locateResource("package://foo/package.xml") == nullptr);  // no locator

  // A missing file inside an available archive
  auto missing = std::make_shared<CondaArchiveResource>("x", archive, "share/missing.txt");
  EXPECT_TRUE(missing->getResourceContents().empty());
  EXPECT_TRUE(missing->getResourceContentStream() == nullptr);

  // By path, opened lazily through the global cache
  CondaPackagePath cp{ generic(f.t), "share/foo/sub/inc.yaml" };
  auto lazy = std::make_shared<CondaArchiveResource>("package://foo/sub/inc.yaml", cp);
  EXPECT_EQ(asString(lazy->getResourceContents()), "x: 1\n");
  EXPECT_EQ(lazy->getFilePath(), generic(f.t) + "/share/foo/sub/inc.yaml");

  // Equality uses identity: same name, different bytes are not equal
  CondaFixtureSpec spec;
  spec.files = baseFiles();
  spec.files.back().contents = "different";
  auto other_bytes = tesseract::common::test::buildCondaPackage(spec);
  auto other = CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(other_bytes), "mem/t.conda");
  ASSERT_TRUE(other != nullptr);
  CondaArchiveResource r1("u", archive, "share/foo/package.xml");
  CondaArchiveResource r2("u", other, "share/foo/package.xml");
  CondaArchiveResource r3("u", archive, "share/foo/package.xml");
  CondaArchiveResource r4("u", archive, "share/baz/package.xml");
  EXPECT_TRUE(r1 != r2);
  EXPECT_TRUE(r1 == r3);
  EXPECT_TRUE(r1 != r4);

  // Unavailable archive
  CondaPackagePath gone{ generic(f.dir / "gone.conda"), "share/foo/package.xml" };
  CondaArchiveResource unavailable("u", gone);
  EXPECT_TRUE(unavailable.getArchive() == nullptr);
  EXPECT_TRUE(unavailable.getResourceContents().empty());
  EXPECT_TRUE(unavailable.getResourceContentStream() == nullptr);
}
#endif  // TESSERACT_COMMON_HAS_CONDA_ARCHIVES

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
