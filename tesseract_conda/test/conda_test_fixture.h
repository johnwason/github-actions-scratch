/**
 * @file conda_test_fixture.h
 * @brief Build small .conda packages at test time (shared by the tesseract unit tests)
 *
 * Header-only. Users must link libarchive (LibArchive::LibArchive).
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
#ifndef TESSERACT_CONDA_TEST_CONDA_TEST_FIXTURE_H
#define TESSERACT_CONDA_TEST_CONDA_TEST_FIXTURE_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // archive.h includes windows.h; keep std::min/std::max usable
#endif
#include <archive.h>
#include <archive_entry.h>

namespace tesseract::conda::test
{
/** @brief One entry of the package contents */
struct CondaFixtureFile
{
  std::string path;
  std::string contents;
  /** @brief If set, the entry is a symbolic link with this target and contents is ignored */
  std::string symlink_target;
};

/** @brief Description of a .conda package to build */
struct CondaFixtureSpec
{
  std::string name{ "t" };
  std::string version{ "1.0" };
  std::string build{ "h0_0" };
  std::vector<CondaFixtureFile> files;
  /** @brief Raw info/about.json. Not written if empty. */
  std::string about_json;
  /** @brief Write info/files */
  bool write_files_manifest{ true };
  /** @brief Write info/paths.json (with sizes) */
  bool write_paths_json{ true };
  /** @brief Write info/index.json */
  bool write_index_json{ true };
};

namespace detail
{
struct WriteBuffer
{
  std::vector<uint8_t> data;
};

inline la_ssize_t fixtureWriteCallback(struct archive* /*a*/, void* client, const void* buffer, size_t length)
{
  auto* out = static_cast<WriteBuffer*>(client);
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  out->data.insert(out->data.end(), bytes, bytes + length);  // NOLINT
  return static_cast<la_ssize_t>(length);
}

inline void check(int r, struct archive* a, const char* what)
{
  if (r < ARCHIVE_WARN)
    throw std::runtime_error(std::string("conda fixture: ") + what + ": " +
                             (archive_error_string(a) != nullptr ? archive_error_string(a) : "error"));
}

struct TarEntry
{
  std::string path;
  std::string contents;
  std::string symlink_target;
};

inline std::vector<uint8_t> writeTarZst(const std::vector<TarEntry>& entries)
{
  WriteBuffer out;
  struct archive* a = archive_write_new();
  check(archive_write_add_filter_zstd(a), a, "zstd filter");
  check(archive_write_set_format_pax_restricted(a), a, "tar format");
  // No padding of the compressed stream to a block size; real .conda members end with the zstd frame
  check(archive_write_set_bytes_in_last_block(a, 1), a, "block size");
  check(archive_write_open(a, &out, nullptr, fixtureWriteCallback, nullptr), a, "open tar");
  for (const auto& e : entries)
  {
    struct archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, e.path.c_str());
    if (!e.symlink_target.empty())
    {
      archive_entry_set_filetype(entry, AE_IFLNK);
      archive_entry_set_perm(entry, 0777);
      archive_entry_set_symlink(entry, e.symlink_target.c_str());
      archive_entry_set_size(entry, 0);
    }
    else
    {
      archive_entry_set_filetype(entry, AE_IFREG);
      archive_entry_set_perm(entry, 0644);
      archive_entry_set_size(entry, static_cast<la_int64_t>(e.contents.size()));
    }
    check(archive_write_header(a, entry), a, "tar header");
    if (e.symlink_target.empty() && !e.contents.empty())
    {
      if (archive_write_data(a, e.contents.data(), e.contents.size()) < 0)
        check(ARCHIVE_FATAL, a, "tar data");
    }
    archive_entry_free(entry);
  }
  check(archive_write_close(a), a, "close tar");
  archive_write_free(a);
  return out.data;
}

inline std::string jsonEscape(const std::string& s)
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
}  // namespace detail

/** @brief Build the bytes of a .conda package */
inline std::vector<uint8_t> buildCondaPackage(const CondaFixtureSpec& spec)
{
  using detail::TarEntry;
  const std::string base = spec.name + "-" + spec.version + "-" + spec.build;

  std::vector<TarEntry> pkg;
  pkg.reserve(spec.files.size());
  for (const auto& f : spec.files)
    pkg.push_back({ f.path, f.contents, f.symlink_target });

  std::vector<TarEntry> info;
  if (spec.write_files_manifest)
  {
    std::string files;
    for (const auto& f : spec.files)
      files += f.path + "\n";
    info.push_back({ "info/files", files, "" });
  }
  if (spec.write_index_json)
  {
    info.push_back({ "info/index.json",
                     "{\n  \"name\": \"" + spec.name + "\",\n  \"version\": \"" + spec.version +
                         "\",\n  \"build\": \"" + spec.build +
                         "\",\n  \"build_number\": 0,\n  \"subdir\": \"noarch\"\n}",
                     "" });
  }
  if (spec.write_paths_json)
  {
    std::ostringstream paths;
    paths << "{\n  \"paths\": [";
    for (std::size_t i = 0; i < spec.files.size(); ++i)
    {
      const auto& f = spec.files[i];
      paths << (i == 0 ? "\n" : ",\n") << "    {\"_path\": \"" << detail::jsonEscape(f.path) << "\", \"path_type\": \""
            << (f.symlink_target.empty() ? "hardlink" : "softlink")
            << "\", \"size_in_bytes\": " << (f.symlink_target.empty() ? f.contents.size() : f.symlink_target.size())
            << "}";
    }
    paths << "\n  ],\n  \"paths_version\": 1\n}";
    info.push_back({ "info/paths.json", paths.str(), "" });
  }
  if (!spec.about_json.empty())
    info.push_back({ "info/about.json", spec.about_json, "" });

  const std::vector<uint8_t> info_tar = detail::writeTarZst(info);
  const std::vector<uint8_t> pkg_tar = detail::writeTarZst(pkg);
  const std::string metadata = "{\"conda_pkg_format_version\": 2}";

  detail::WriteBuffer out;
  struct archive* a = archive_write_new();
  detail::check(archive_write_set_format_zip(a), a, "zip format");
  detail::check(archive_write_set_option(a, "zip", "compression", "store"), a, "zip store");
  detail::check(archive_write_set_bytes_in_last_block(a, 1), a, "block size");
  detail::check(archive_write_open(a, &out, nullptr, detail::fixtureWriteCallback, nullptr), a, "open zip");
  auto add = [&](const std::string& name, const void* data, std::size_t size) {
    struct archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, name.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, static_cast<la_int64_t>(size));
    detail::check(archive_write_header(a, entry), a, "zip header");
    if (size > 0 && archive_write_data(a, data, size) < 0)
      detail::check(ARCHIVE_FATAL, a, "zip data");
    archive_entry_free(entry);
  };
  add("metadata.json", metadata.data(), metadata.size());
  add("pkg-" + base + ".tar.zst", pkg_tar.data(), pkg_tar.size());
  add("info-" + base + ".tar.zst", info_tar.data(), info_tar.size());
  detail::check(archive_write_close(a), a, "close zip");
  archive_write_free(a);
  return out.data;
}

/** @brief Build a .conda package and write it to a file */
inline void writeCondaPackage(const std::filesystem::path& path, const CondaFixtureSpec& spec)
{
  const std::vector<uint8_t> bytes = buildCondaPackage(spec);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));  // NOLINT
  if (!ofs)
    throw std::runtime_error("conda fixture: failed to write " + path.string());
}

/** @brief Read a whole file as a string */
inline std::string readFixtureFile(const std::filesystem::path& path)
{
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs)
    throw std::runtime_error("conda fixture: failed to read " + path.string());
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

/** @brief An ASCII STL of a 2 m cube centered at the origin (12 triangles, 8 distinct vertices) */
inline std::string fixtureBoxStl()
{
  const double v[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
                           { -1, -1, 1 },  { 1, -1, 1 },  { 1, 1, 1 },  { -1, 1, 1 } };
  const int f[12][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
                         { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
  std::ostringstream out;
  out << "solid box\n";
  for (const auto& t : f)
  {
    out << "  facet normal 0 0 0\n    outer loop\n";
    for (int k : t)
      out << "      vertex " << v[k][0] << " " << v[k][1] << " " << v[k][2] << "\n";  // NOLINT
    out << "    endloop\n  endfacet\n";
  }
  out << "endsolid box\n";
  return out.str();
}

/** @brief Contact manager plugin config with the Bullet and FCL factories shipped with tesseract */
inline std::string fixtureContactManagersYaml()
{
  return "contact_manager_plugins:\n"
         "  search_libraries:\n"
         "    - tesseract_collision_bullet_factories\n"
         "    - tesseract_collision_fcl_factories\n"
         "  discrete_plugins:\n"
         "    default: BulletDiscreteBVHManager\n"
         "    plugins:\n"
         "      BulletDiscreteBVHManager:\n"
         "        class: BulletDiscreteBVHManagerFactory\n"
         "  continuous_plugins:\n"
         "    default: BulletCastBVHManager\n"
         "    plugins:\n"
         "      BulletCastBVHManager:\n"
         "        class: BulletCastBVHManagerFactory\n";
}

/** @brief A ROS package.xml for a package */
inline std::string fixturePackageXml(const std::string& name)
{
  return "<?xml version=\"1.0\"?>\n<package format=\"3\">\n  <name>" + name +
         "</name>\n  <version>1.0.0</version>\n  <description>test</description>\n  <maintainer "
         "email=\"test@example.com\">test</maintainer>\n  <license>Apache 2.0</license>\n</package>\n";
}

/**
 * @brief A package "conda_robot" with a ROS package foo holding a small robot:
 * share/foo/urdf/robot.urdf (meshes: package://foo/meshes/box.stl), share/foo/urdf/robot.srdf and
 * share/foo/config/calibration.yaml, which uses a relative !include of joint_1_calibration.yaml.
 * @param box_stl The bytes of an STL mesh
 * @param contact_managers_yaml If not empty, written to share/foo/config/contact_manager_plugins.yaml and referenced
 * by the SRDF
 */
inline CondaFixtureSpec condaRobotFixture(const std::string& box_stl, const std::string& contact_managers_yaml = "")
{
  const std::string urdf = R"(<?xml version="1.0"?>
<robot name="conda_robot" xmlns:tesseract="https://github.com/tesseract-robotics/tesseract" tesseract:make_convex="false">
  <link name="base_link">
    <visual><geometry><mesh filename="package://foo/meshes/box.stl"/></geometry></visual>
    <collision><geometry><mesh filename="package://foo/meshes/box.stl"/></geometry></collision>
  </link>
  <link name="tool0">
    <visual><geometry><mesh filename="package://foo/meshes/box.stl" scale="0.5 0.5 0.5"/></geometry></visual>
  </link>
  <joint name="joint_1" type="revolute">
    <parent link="base_link"/>
    <child link="tool0"/>
    <origin xyz="0 0 1"/>
    <axis xyz="0 0 1"/>
    <limit lower="-1" upper="1" effort="1" velocity="1"/>
  </joint>
</robot>
)";
  std::string srdf = R"(<?xml version="1.0"?>
<robot name="conda_robot" version="1.0.0">
  <group name="manipulator">
    <chain base_link="base_link" tip_link="tool0"/>
  </group>
  <calibration_config filename="package://foo/config/calibration.yaml"/>
)";
  if (!contact_managers_yaml.empty())
    srdf += R"(  <contact_managers_plugin_config filename="package://foo/config/contact_manager_plugins.yaml"/>
)";
  srdf += R"(  <disable_collisions link1="base_link" link2="tool0" reason="Adjacent"/>
</robot>
)";

  CondaFixtureSpec spec;
  spec.name = "conda_robot";
  spec.files = { { "share/foo/package.xml", fixturePackageXml("foo"), "" },
                 { "share/foo/meshes/box.stl", box_stl, "" },
                 { "share/foo/urdf/robot.urdf", urdf, "" },
                 { "share/foo/urdf/robot.srdf", srdf, "" },
                 { "share/foo/config/calibration.yaml",
                   "calibration:\n  joints:\n    joint_1: !include joint_1_calibration.yaml\n",
                   "" },
                 { "share/foo/config/joint_1_calibration.yaml",
                   "position:\n  x: 1\n  y: 2\n  z: 3\norientation:\n  x: 0\n  y: 0\n  z: 0\n  w: 1\n",
                   "" } };
  if (!contact_managers_yaml.empty())
    spec.files.push_back({ "share/foo/config/contact_manager_plugins.yaml", contact_managers_yaml, "" });
  return spec;
}

/** @brief A unique empty directory under the system temp directory */
inline std::filesystem::path makeFixtureTempDir(const std::string& prefix)
{
  const auto base = std::filesystem::temp_directory_path();
  for (int i = 0; i < 1000; ++i)
  {
    std::ostringstream name;
    name << prefix << "_" << std::hex << std::random_device{}();
    auto dir = base / name.str();
    if (std::filesystem::create_directory(dir))
      return dir;
  }
  throw std::runtime_error("conda fixture: failed to create a temp directory");
}

}  // namespace tesseract::conda::test

#endif  // TESSERACT_CONDA_TEST_CONDA_TEST_FIXTURE_H
