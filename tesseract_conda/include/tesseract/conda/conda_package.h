/**
 * @file conda_package.h
 * @brief Read resources directly from conda (.conda) packages without extracting them
 *
 * The code is split into layers that can be used on their own:
 *
 *  1. Path helpers (parseCondaPackagePath, makeCondaPackagePath). A path such as
 *     `/x/ros-humble-foo-1.0-h123_0.conda/share/foo/meshes/link.stl` names the file
 *     `share/foo/meshes/link.stl` inside the archive `/x/ros-humble-foo-1.0-h123_0.conda`.
 *  2. CondaPackageArchive: reads one archive (from a file or from memory). It knows nothing about resources.
 *  3. CondaPackageArchiveCache: shares open archives by path or package identity, and lets in-memory
 *     archives be registered under a virtual path.
 *  3b. The tesseract resource index, metadata embedded in a package that maps ROS package names to
 *     directories inside the archive.
 *  4. Helpers that combine the layers (findRosPackagesInCondaArchive, locateCondaResource) and
 *     CondaArchiveResource, the Resource type for a file inside an archive.
 *
 * CondaResourceLocator (conda_resource_locator.h) puts these together into a ResourceLocator.
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
#ifndef TESSERACT_CONDA_CONDA_PACKAGE_H
#define TESSERACT_CONDA_CONDA_PACKAGE_H

#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/resource_locator.h>

namespace tesseract::conda
{
class CondaPackageArchiveCache;
class CondaArchiveResource;
struct CondaPackageIdentity;

template <class Archive>
void serialize(Archive& ar, CondaArchiveResource& obj);

using tesseract::common::Resource;
using tesseract::common::ResourceLocator;

/* ----------------------------------------------------------------------------------------------
 * Layer 1: path helpers
 * -------------------------------------------------------------------------------------------- */

/** @brief A path split into the archive and the path inside it */
struct CondaPackagePath
{
  /** @brief Path of the .conda file (or the key of an in-memory archive), in generic '/' form */
  std::string archive_path;
  /** @brief Path inside the archive, relative with no leading '/'. Empty for the archive root. */
  std::string inner_path;
};

/**
 * @brief Split a path into a .conda archive and the path inside it
 * @details Strings without ".conda" (case-insensitive) are rejected without touching the filesystem.
 * The path is converted to generic '/' form (backslashes become '/') and normalized. The first prefix
 * whose last component ends in ".conda" and that is either a regular file or a key registered in the
 * cache is the archive.
 * @param path The path to split
 * @param cache The cache whose registered in-memory archives are also recognized
 * @return The split path, or nullopt if the path does not name a .conda archive
 */
std::optional<CondaPackagePath> parseCondaPackagePath(const std::string& path, const CondaPackageArchiveCache& cache);

/** @copydoc parseCondaPackagePath(const std::string&, const CondaPackageArchiveCache&) (uses the global cache) */
std::optional<CondaPackagePath> parseCondaPackagePath(const std::string& path);

/** @brief Check if a path names a file or directory inside a .conda archive (uses the global cache) */
bool isCondaPackagePath(const std::string& path);

/**
 * @brief Build the path of a file inside an archive, the inverse of parseCondaPackagePath
 * @param archive_path The archive path
 * @param inner_path The path inside the archive. May be empty.
 */
std::string makeCondaPackagePath(const std::string& archive_path, const std::string& inner_path);

/** @brief Normalize a path inside an archive: generic '/' form, no leading, trailing or repeated '/', no '.' */
std::string normalizeCondaInnerPath(const std::string& inner_path);

/** @brief Check that the libarchive in use can read .conda packages (zstd support). Logs once if not. */
bool isCondaArchiveSupportAvailable();

/* ----------------------------------------------------------------------------------------------
 * Layer 2: reading one archive
 * -------------------------------------------------------------------------------------------- */

/** @brief Identifies a conda package independently of where it is stored */
struct CondaPackageIdentity
{
  std::string name;
  std::string version;
  std::string build;
  /** @brief Lowercase hex sha256 of the whole .conda file. May be empty when unknown. */
  std::string sha256;

  /** @brief A short form for messages, e.g. "foo-1.0-h123_0 (0123abcd)" */
  std::string toString() const;

  bool operator==(const CondaPackageIdentity& rhs) const;
  bool operator!=(const CondaPackageIdentity& rhs) const;
};

/** @brief Compute the lowercase hex SHA-256 digest of a buffer */
std::string computeSha256Hex(const uint8_t* data, std::size_t size);

/**
 * @brief Read-only access to a .conda package
 * @details On open, the outer ZIP container is read and the small info-*.tar.zst member is decompressed to get the
 * file manifest (info/files, or info/paths.json as fallback) and the package metadata. File contents come from the
 * pkg-*.tar.zst member, which is either loaded whole on first use (when its uncompressed size is at most
 * fullLoadLimit()) or streamed per request with a byte-capped LRU cache.
 *
 * All methods are thread-safe. Errors are logged and reported by empty results; nothing throws.
 */
class CondaPackageArchive
{
public:
  using Ptr = std::shared_ptr<CondaPackageArchive>;
  using ConstPtr = std::shared_ptr<const CondaPackageArchive>;

  /**
   * @brief Open a .conda file
   * @return The archive, or nullptr (with an error logged) if it can't be read
   */
  static ConstPtr openFile(const std::filesystem::path& path);

  /**
   * @brief Open a .conda package held in memory, e.g. one received over the network
   * @param data The bytes of the .conda file. They are shared, not copied.
   * @param name The name reported by sourceName(). To make it reachable by path, register it in a
   * CondaPackageArchiveCache under the same name, ideally an absolute-looking path ending in ".conda".
   * @return The archive, or nullptr (with an error logged) if it can't be read
   */
  static ConstPtr openBytes(std::shared_ptr<const std::vector<uint8_t>> data, std::string name);

  ~CondaPackageArchive();
  CondaPackageArchive(const CondaPackageArchive&) = delete;
  CondaPackageArchive& operator=(const CondaPackageArchive&) = delete;
  CondaPackageArchive(CondaPackageArchive&&) = delete;
  CondaPackageArchive& operator=(CondaPackageArchive&&) = delete;

  /** @brief The file path, or the name given for an in-memory archive */
  const std::string& sourceName() const;

  /** @brief True if this archive was opened from memory */
  bool isInMemory() const;

  /** @brief Package name from info/index.json (empty if unknown) */
  const std::string& name() const;
  /** @brief Package version from info/index.json (empty if unknown) */
  const std::string& version() const;
  /** @brief Package build string from info/index.json (empty if unknown) */
  const std::string& build() const;

  /** @brief Name, version, build and sha256. The sha256 is computed on the first call and cached. */
  CondaPackageIdentity identity() const;

  /** @brief Every file path in the package (sorted), from the manifest */
  const std::vector<std::string>& files() const;
  /** @brief Check if the manifest lists this file */
  bool hasFile(const std::string& inner_path) const;
  /** @brief Check if this is a directory of a file in the manifest. The empty path (archive root) is a directory. */
  bool hasDirectory(const std::string& inner_path) const;
  /** @brief Every manifest file whose path starts with prefix (plain string prefix match), sorted */
  std::vector<std::string> listFiles(const std::string& prefix) const;

  /**
   * @brief Get the raw contents of a top-level file in info/, e.g. "about.json", "index.json", "files", "paths.json"
   * @return The contents, or nullopt if the package has no such file
   */
  std::optional<std::string> infoFile(const std::string& name) const;

  /**
   * @brief Read one file of the package. Symbolic links inside the archive are followed (depth limit 8).
   * @return The contents. Empty (with an error logged) if missing or unreadable.
   */
  std::vector<uint8_t> readFile(const std::string& inner_path) const;

  /**
   * @brief Read several files in one pass through the content stream
   * @return The files that were found, by requested path
   */
  std::map<std::string, std::vector<uint8_t>> readFiles(const std::vector<std::string>& inner_paths) const;

  /** @brief Load every file into memory now, regardless of fullLoadLimit() */
  void preload() const;
  /** @brief Drop file contents held in memory (full load and LRU cache). Later reads decompress again. */
  void releaseContents() const;
  /** @brief True if every file is currently held in memory */
  bool isFullyLoaded() const;

  /** @brief Uncompressed size of the package contents from info/paths.json, or nullopt if unknown */
  std::optional<std::size_t> contentSize() const;

  /**
   * @brief The number of times the pkg-*.tar.zst content stream was decompressed (diagnostics and tests)
   */
  std::size_t contentPassCount() const;

  /** @brief Archives whose content is at most this many bytes are loaded whole on first read. Default 256 MB. */
  static void setFullLoadLimit(std::size_t bytes);
  static std::size_t fullLoadLimit();

  /** @brief Byte cap of the per-archive LRU cache used for large archives. Default 64 MB. */
  static void setReadCacheLimit(std::size_t bytes);
  static std::size_t readCacheLimit();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit CondaPackageArchive(std::unique_ptr<Impl> impl);
  static ConstPtr open(std::unique_ptr<Impl> impl);
};

/* ----------------------------------------------------------------------------------------------
 * Layer 3: sharing open archives
 * -------------------------------------------------------------------------------------------- */

/**
 * @brief Shares open archives by path and by package identity
 * @details Archives opened from files are held weakly and reopened if the file's size or modification time
 * changes. Archives registered with add() are held strongly until remove(). All methods are thread-safe.
 */
class CondaPackageArchiveCache
{
public:
  /** @brief Called to fetch a package that is not open yet. Return nullptr if it isn't available. */
  using Resolver = std::function<CondaPackageArchive::ConstPtr(const CondaPackageIdentity&)>;

  CondaPackageArchiveCache() = default;
  ~CondaPackageArchiveCache() = default;
  CondaPackageArchiveCache(const CondaPackageArchiveCache&) = delete;
  CondaPackageArchiveCache& operator=(const CondaPackageArchiveCache&) = delete;
  CondaPackageArchiveCache(CondaPackageArchiveCache&&) = delete;
  CondaPackageArchiveCache& operator=(CondaPackageArchiveCache&&) = delete;

  /** @brief The default instance shared across the process */
  static CondaPackageArchiveCache& global();

  /**
   * @brief Get an archive by path: a registered key, or a .conda file that is opened (or reused) as needed
   * @return The archive, or nullptr if it can't be opened
   */
  CondaPackageArchive::ConstPtr get(const std::string& archive_path);

  /** @brief Register an archive (typically in-memory) under a virtual path, e.g. "/remote/foo-1.0-h0_0.conda" */
  void add(const std::string& key, CondaPackageArchive::ConstPtr archive);

  /** @brief Remove an archive registered with add() */
  void remove(const std::string& key);

  /** @brief Check if a key was registered with add() */
  bool hasKey(const std::string& key) const;

  /**
   * @brief Find an archive that is already open with this identity
   * @details When the identity has a sha256 it must match; otherwise name, version and build must match.
   */
  CondaPackageArchive::ConstPtr findByIdentity(const CondaPackageIdentity& identity) const;

  /** @brief findByIdentity, then the resolver if one is set */
  CondaPackageArchive::ConstPtr resolve(const CondaPackageIdentity& identity) const;

  /** @brief Set a function used to fetch packages that are not open yet. Pass nullptr to clear it. */
  void setResolver(Resolver resolver);

  /** @brief Forget every archive, including registered ones. The resolver is kept. */
  void clear();

  /** @brief Check if an archive matches an identity, with the rules of findByIdentity */
  static bool matchesIdentity(const CondaPackageArchive& archive, const CondaPackageIdentity& identity);

private:
  struct FileEntry
  {
    std::weak_ptr<const CondaPackageArchive> archive;
    std::uintmax_t size{ 0 };
    std::filesystem::file_time_type mtime;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, FileEntry> files_;
  std::unordered_map<std::string, CondaPackageArchive::ConstPtr> registered_;
  std::shared_ptr<Resolver> resolver_;
};

/* ----------------------------------------------------------------------------------------------
 * Layer 3b: tesseract resource index
 * -------------------------------------------------------------------------------------------- */

/** @brief The key of the index, in info/about.json "extra" and at the top of the data-side file */
static constexpr const char* kTesseractResourceIndexKey = "tesseract_resource_index";
/** @brief Directory, inside the package contents, of the data-side index files (<conda-package-name>.yaml) */
static constexpr const char* kTesseractResourceIndexDataDir = "share/tesseract/resource_index";
/** @brief Highest index format version this code understands */
static constexpr int kTesseractResourceIndexVersion = 1;

/** @brief Where a resource index was read from */
enum class TesseractResourceIndexSource
{
  /** @brief Parsed from text given directly to a parser */
  Unknown,
  /** @brief info/about.json, extra.tesseract_resource_index */
  AboutJson,
  /** @brief share/tesseract/resource_index/\*.yaml in the package contents */
  DataFile
};

/**
 * @brief Metadata embedded in a conda package that tells tesseract which ROS packages it contains
 * @details Format version 1:
 * @code{.yaml}
 * tesseract_resource_index:
 *   version: 1
 *   packages:            # package name -> directory, relative to the archive root
 *     ur_description: share/ur_description
 * @endcode
 * Other keys are reserved; readers ignore keys they don't know, and they stay available in raw.
 */
struct TesseractResourceIndex
{
  int version{ 0 };
  /** @brief Package name -> normalized directory inside the archive */
  std::map<std::string, std::string> packages;
  /** @brief The whole index object, including keys this version doesn't use */
  YAML::Node raw;
  /** @brief Where the index came from */
  TesseractResourceIndexSource source{ TesseractResourceIndexSource::Unknown };
  /** @brief For DataFile: the files that were read, in the order they were merged */
  std::vector<std::string> source_files;
};

/**
 * @brief Check and convert an index object ({version, packages, ...})
 * @throws std::runtime_error if the version is missing or newer than kTesseractResourceIndexVersion, if packages is
 * not a map of strings, or if a package path is absolute or leaves the archive root
 */
TesseractResourceIndex parseTesseractResourceIndex(const YAML::Node& index_object);

/**
 * @brief Parse a data-side index file, which wraps the index object in the top-level tesseract_resource_index key
 * @throws std::runtime_error if the text is invalid, the key is missing or the index is invalid
 */
TesseractResourceIndex parseTesseractResourceIndexFile(const std::string& yaml_text);

/**
 * @brief Parse info/about.json and read extra.tesseract_resource_index, given as an object or as a JSON string
 * @return The index, or nullopt if about.json has no index entry
 * @throws std::runtime_error if about.json or the entry is invalid
 */
std::optional<TesseractResourceIndex> parseTesseractResourceIndexFromAboutJson(const std::string& about_json_text);

/**
 * @brief Read the index from the archive's info/about.json only
 * @return nullopt if there is no entry
 * @throws std::runtime_error if the entry is invalid
 */
std::optional<TesseractResourceIndex> readTesseractResourceIndexFromAboutJson(const CondaPackageArchive& archive);

/**
 * @brief Read and merge the data-side index files of an archive only. Files that fail to parse are logged and skipped.
 * @return nullopt if there are no usable files
 */
std::optional<TesseractResourceIndex> readTesseractResourceIndexFromDataFiles(const CondaPackageArchive& archive);

/**
 * @brief Read an archive's resource index: info/about.json first, the data-side files only if about.json has none
 * @details A broken about.json entry is logged and nullopt is returned without falling back, so the error stays
 * visible. Package directories that are not in the archive are logged and dropped. Never throws.
 */
std::optional<TesseractResourceIndex> readTesseractResourceIndex(const CondaPackageArchive& archive);

/* ----------------------------------------------------------------------------------------------
 * Layer 4: helpers that combine the layers
 * -------------------------------------------------------------------------------------------- */

/**
 * @brief Find ROS packages (directories with package.xml) under a directory of an archive
 * @details If inner_root/package.xml exists, the root itself is the package. Otherwise every inner_root/.../package.xml
 * is collected, shortest path first, skipping anything under a package already found.
 * @return Package name (the directory name) -> package directory inside the archive
 */
std::map<std::string, std::string> findRosPackagesInCondaArchive(const CondaPackageArchive& archive,
                                                                 const std::string& inner_root);

/**
 * @brief Create the resource for a path inside a .conda archive
 * @param url The URL the resource was requested with
 * @param resolved_path The file path the URL resolved to, e.g. /x/foo.conda/share/foo/meshes/a.stl
 * @param parent The locator used for lookups that leave the archive
 * @param cache The cache used to open the archive
 * @return The resource, or nullptr if resolved_path is not a conda path or the file is not in the archive
 */
std::shared_ptr<Resource> locateCondaResource(const std::string& url,
                                              const std::string& resolved_path,
                                              ResourceLocator::ConstPtr parent,
                                              CondaPackageArchiveCache& cache = CondaPackageArchiveCache::global());

/**
 * @brief A file inside a .conda package
 * @details The resource serves bytes: isFile() is false, and getFilePath() returns the archive path of the file
 * (e.g. /x/foo.conda/share/foo/urdf/a.urdf) so it can be handed back to a locator. Serialization records the package
 * identity (name, version, build, sha256), not just the local path; on load the archive is found through the global
 * cache (findByIdentity, then its resolver), then through the recorded path if its identity matches.
 */
class CondaArchiveResource : public Resource
{
public:
  using Ptr = std::shared_ptr<CondaArchiveResource>;
  using ConstPtr = std::shared_ptr<const CondaArchiveResource>;

  /** @brief This is for serialization do not use directly */
  CondaArchiveResource() = default;

  /**
   * @brief A file of an archive given by path. The archive is opened lazily through the global cache.
   * @param url The url used to locate the resource
   * @param path The archive and the file inside it
   * @param parent The locator used for lookups that leave the archive (may be nullptr)
   */
  CondaArchiveResource(std::string url, CondaPackagePath path, ResourceLocator::ConstPtr parent = nullptr);

  /**
   * @brief A file of an archive that is already open, for use without a locator or file path
   * @param url The url of the resource
   * @param archive The archive
   * @param inner_path The file inside the archive
   * @param parent The locator used for lookups that leave the archive (may be nullptr)
   * @param archive_path The path the archive is known by. Defaults to archive->sourceName().
   */
  CondaArchiveResource(std::string url,
                       CondaPackageArchive::ConstPtr archive,
                       std::string inner_path,
                       ResourceLocator::ConstPtr parent = nullptr,
                       std::string archive_path = "");

  ~CondaArchiveResource() override = default;
  CondaArchiveResource(const CondaArchiveResource&) = delete;
  CondaArchiveResource& operator=(const CondaArchiveResource&) = delete;
  CondaArchiveResource(CondaArchiveResource&&) = delete;
  CondaArchiveResource& operator=(CondaArchiveResource&&) = delete;

  bool isFile() const override final;
  std::string getUrl() const override final;
  std::string getFilePath() const override final;
  std::vector<uint8_t> getResourceContents() const override final;
  std::shared_ptr<std::istream> getResourceContentStream() const override final;
  Resource::Ptr locateResource(const std::string& url) const override final;

  /** @brief The archive, resolved on first use. nullptr if it is not available. */
  CondaPackageArchive::ConstPtr getArchive() const;
  /** @brief The file inside the archive */
  const std::string& getInnerPath() const;
  /** @brief The path the archive is known by */
  std::string getArchivePath() const;
  /** @brief The package identity (from the archive if available, otherwise as deserialized) */
  CondaPackageIdentity getIdentity() const;

  /** @brief Compares url, inner path and archive identity */
  bool operator==(const CondaArchiveResource& rhs) const;
  bool operator!=(const CondaArchiveResource& rhs) const;

private:
  std::string url_;
  std::string inner_path_;
  ResourceLocator::ConstPtr parent_;

  mutable std::mutex mutex_;
  mutable std::string archive_path_;
  mutable CondaPackageArchive::ConstPtr archive_;
  mutable CondaPackageIdentity identity_;
  mutable bool unavailable_logged_{ false };

  template <class Archive>
  friend void ::tesseract::conda::serialize(Archive& ar, CondaArchiveResource& obj);
};

}  // namespace tesseract::conda

#endif  // TESSERACT_CONDA_CONDA_PACKAGE_H
