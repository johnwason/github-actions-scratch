/**
 * @file conda_package.cpp
 * @brief Read resources directly from conda (.conda) packages without extracting them
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

#include <tesseract/common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <list>
#include <sstream>
#include <stdexcept>
#include <archive.h>
#include <archive_entry.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/common/conda_package.h>
#include <tesseract/common/logging.h>

namespace tesseract::common
{
// Defined in resource_locator.cpp
bool isRelativePath(const std::string& url);

namespace
{
/* ---------------------------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------------------------- */

std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool endsWithCaseInsensitive(const std::string& s, const std::string& suffix)
{
  if (s.size() < suffix.size())
    return false;
  return toLower(s.substr(s.size() - suffix.size())) == toLower(suffix);
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

/** @brief Backslashes to '/', and on Windows strip the '/' in front of a drive letter ("/C:/x" from file:///C:/x) */
std::string toGenericSeparators(std::string s)
{
  std::replace(s.begin(), s.end(), '\\', '/');
#ifdef _WIN32
  if (s.size() >= 3 && s[0] == '/' && std::isalpha(static_cast<unsigned char>(s[1])) != 0 && s[2] == ':')
    s.erase(0, 1);
#endif
  return s;
}

/** @brief Normalize an outer (filesystem or virtual) path to a generic '/' form usable as a key */
std::string normalizeOuterPath(const std::string& path)
{
  std::string s = std::filesystem::path(toGenericSeparators(path)).lexically_normal().generic_string();
  while (s.size() > 1 && s.back() == '/')
    s.pop_back();
  return s;
}

std::string innerDirname(const std::string& inner)
{
  auto pos = inner.find_last_of('/');
  return (pos == std::string::npos) ? std::string() : inner.substr(0, pos);
}

std::string innerJoin(const std::string& a, const std::string& b)
{
  if (a.empty())
    return normalizeCondaInnerPath(b);
  return normalizeCondaInnerPath(a + "/" + b);
}

/** @brief True if a normalized inner path escapes the archive root */
bool escapesRoot(const std::string& normalized) { return normalized == ".." || startsWith(normalized, "../"); }

/* ---------------------------------------------------------------------------------------------
 * SHA-256 (FIPS 180-4). Small self-contained implementation; libarchive does not expose its digests.
 * ------------------------------------------------------------------------------------------- */

class Sha256
{
public:
  void update(const uint8_t* data, std::size_t len)
  {
    for (std::size_t i = 0; i < len; ++i)
    {
      buffer_[buffer_len_++] = data[i];  // NOLINT
      if (buffer_len_ == 64)
      {
        transform(buffer_.data());
        buffer_len_ = 0;
      }
    }
    total_len_ += len;
  }

  std::string hexDigest()
  {
    const uint64_t bit_len = total_len_ * 8;
    const uint8_t pad_start = 0x80;
    update(&pad_start, 1);
    const uint8_t zero = 0;
    while (buffer_len_ != 56)
      update(&zero, 1);
    std::array<uint8_t, 8> len_bytes{};
    for (int i = 0; i < 8; ++i)
      len_bytes[static_cast<std::size_t>(i)] = static_cast<uint8_t>(bit_len >> (56 - (8 * i)));
    update(len_bytes.data(), len_bytes.size());

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t h : state_)
    {
      for (int shift = 28; shift >= 0; shift -= 4)
        out.push_back(hex[(h >> shift) & 0xF]);  // NOLINT
    }
    return out;
  }

private:
  std::array<uint32_t, 8> state_{ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  std::array<uint8_t, 64> buffer_{};
  std::size_t buffer_len_{ 0 };
  uint64_t total_len_{ 0 };

  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void transform(const uint8_t* block)
  {
    static const std::array<uint32_t, 64> k = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    std::array<uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
    {
      w[i] = (static_cast<uint32_t>(block[(4 * i)]) << 24) |      // NOLINT
             (static_cast<uint32_t>(block[(4 * i) + 1]) << 16) |  // NOLINT
             (static_cast<uint32_t>(block[(4 * i) + 2]) << 8) |   // NOLINT
             (static_cast<uint32_t>(block[(4 * i) + 3]));         // NOLINT
    }
    for (std::size_t i = 16; i < 64; ++i)
    {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < 64; ++i)
    {
      const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = h + s1 + ch + k[i] + w[i];
      const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
};

/* ---------------------------------------------------------------------------------------------
 * libarchive helpers
 * ------------------------------------------------------------------------------------------- */

struct ArchiveReadDeleter
{
  void operator()(struct archive* a) const
  {
    if (a != nullptr)
      archive_read_free(a);
  }
};
using ArchiveReadPtr = std::unique_ptr<struct archive, ArchiveReadDeleter>;

std::string archiveError(struct archive* a)
{
  const char* msg = archive_error_string(a);
  return (msg != nullptr) ? std::string(msg) : std::string("unknown libarchive error");
}

std::string entryPath(struct archive_entry* entry)
{
  const char* p = archive_entry_pathname_utf8(entry);
  if (p == nullptr)
    p = archive_entry_pathname(entry);
  return (p != nullptr) ? std::string(p) : std::string();
}

std::string entrySymlink(struct archive_entry* entry)
{
  const char* p = archive_entry_symlink_utf8(entry);
  if (p == nullptr)
    p = archive_entry_symlink(entry);
  return (p != nullptr) ? std::string(p) : std::string();
}

std::string entryHardlink(struct archive_entry* entry)
{
  const char* p = archive_entry_hardlink_utf8(entry);
  if (p == nullptr)
    p = archive_entry_hardlink(entry);
  return (p != nullptr) ? std::string(p) : std::string();
}

/** @brief Read the data of the current entry into memory */
bool readEntryData(struct archive* a, std::vector<uint8_t>& out)
{
  out.clear();
  std::array<uint8_t, 65536> buf{};
  for (;;)
  {
    const la_ssize_t n = archive_read_data(a, buf.data(), buf.size());
    if (n < 0)
      return false;
    if (n == 0)
      return true;
    out.insert(out.end(), buf.begin(), buf.begin() + n);
  }
}

/** @brief Feeds a nested archive_read from the current entry of an outer archive */
struct NestedReadContext
{
  struct archive* outer{ nullptr };
  std::array<uint8_t, 65536> buffer{};
};

la_ssize_t nestedReadCallback(struct archive* /*a*/, void* client_data, const void** buff)
{
  auto* ctx = static_cast<NestedReadContext*>(client_data);
  *buff = ctx->buffer.data();
  return archive_read_data(ctx->outer, ctx->buffer.data(), ctx->buffer.size());
}

ArchiveReadPtr newTarZstReader()
{
  ArchiveReadPtr a(archive_read_new());
  if (!a)
    return nullptr;
  archive_read_support_filter_zstd(a.get());
  archive_read_support_format_tar(a.get());
  return a;
}

std::atomic<std::size_t> g_full_load_limit{ std::size_t(256) * 1024 * 1024 };
std::atomic<std::size_t> g_read_cache_limit{ std::size_t(64) * 1024 * 1024 };

constexpr int kSymlinkDepthLimit = 8;

}  // namespace

/* ---------------------------------------------------------------------------------------------
 * Layer 1
 * ------------------------------------------------------------------------------------------- */

std::string normalizeCondaInnerPath(const std::string& inner_path)
{
  std::vector<std::string> parts;
  std::string current;
  auto flush = [&]() {
    if (current.empty() || current == ".")
    {
    }
    else if (current == "..")
    {
      if (!parts.empty() && parts.back() != "..")
        parts.pop_back();
      else
        parts.emplace_back("..");
    }
    else
    {
      parts.push_back(current);
    }
    current.clear();
  };
  for (char c : inner_path)
  {
    if (c == '/' || c == '\\')
      flush();
    else
      current.push_back(c);
  }
  flush();

  std::string out;
  for (const auto& p : parts)
  {
    if (!out.empty())
      out.push_back('/');
    out += p;
  }
  return out;
}

std::optional<CondaPackagePath> parseCondaPackagePath(const std::string& path, const CondaPackageArchiveCache& cache)
{
  // Quick rejection, no filesystem access for ordinary paths
  if (toLower(path).find(".conda") == std::string::npos)
    return std::nullopt;

  const std::filesystem::path p = std::filesystem::path(toGenericSeparators(path)).lexically_normal();

  std::filesystem::path prefix;
  auto it = p.begin();
  for (; it != p.end(); ++it)
  {
    prefix /= *it;
    const std::string component = it->string();
    if (!endsWithCaseInsensitive(component, ".conda"))
      continue;

    const std::string key = prefix.generic_string();
    std::error_code ec;
    if (cache.hasKey(key) || std::filesystem::is_regular_file(prefix, ec))
    {
      CondaPackagePath result;
      result.archive_path = key;
      std::string inner;
      for (auto rest = std::next(it); rest != p.end(); ++rest)
      {
        if (!inner.empty())
          inner.push_back('/');
        inner += rest->generic_string();
      }
      result.inner_path = normalizeCondaInnerPath(inner);
      return result;
    }
  }
  return std::nullopt;
}

std::optional<CondaPackagePath> parseCondaPackagePath(const std::string& path)
{
  return parseCondaPackagePath(path, CondaPackageArchiveCache::global());
}

bool isCondaPackagePath(const std::string& path) { return parseCondaPackagePath(path).has_value(); }

std::string makeCondaPackagePath(const std::string& archive_path, const std::string& inner_path)
{
  const std::string inner = normalizeCondaInnerPath(inner_path);
  std::string archive = archive_path;
  while (archive.size() > 1 && (archive.back() == '/' || archive.back() == '\\'))
    archive.pop_back();
  if (inner.empty())
    return archive;
  return archive + "/" + inner;
}

bool isCondaArchiveSupportAvailable()
{
  static const bool available = []() {
    ArchiveReadPtr a(archive_read_new());
    const bool ok = a && archive_read_support_filter_zstd(a.get()) == ARCHIVE_OK;
    if (!ok)
      TESSERACT_LOG_ERROR("libarchive built without zstd support; .conda packages can't be read ({})",
                          archive_version_details());
    return ok;
  }();
  return available;
}

/* ---------------------------------------------------------------------------------------------
 * Layer 2
 * ------------------------------------------------------------------------------------------- */

std::string CondaPackageIdentity::toString() const
{
  std::string s = name + "-" + version + "-" + build;
  if (!sha256.empty())
    s += " (" + sha256.substr(0, 8) + ")";
  return s;
}

bool CondaPackageIdentity::operator==(const CondaPackageIdentity& rhs) const
{
  return name == rhs.name && version == rhs.version && build == rhs.build && sha256 == rhs.sha256;
}
bool CondaPackageIdentity::operator!=(const CondaPackageIdentity& rhs) const { return !operator==(rhs); }

std::string computeSha256Hex(const uint8_t* data, std::size_t size)
{
  Sha256 sha;
  sha.update(data, size);
  return sha.hexDigest();
}

struct CondaPackageArchive::Impl
{
  // Source
  std::string source_name;
  std::filesystem::path file_path;
  std::shared_ptr<const std::vector<uint8_t>> bytes;

  // Outer members
  std::string info_member;
  std::string pkg_member;

  // Manifest and metadata
  std::vector<std::string> files;
  std::set<std::string> directories;
  std::map<std::string, std::string> info_files;
  std::string name;
  std::string version;
  std::string build;
  std::optional<std::size_t> content_size;

  // Identity
  mutable std::mutex sha_mutex;
  mutable std::string sha256;

  // Contents
  mutable std::mutex content_mutex;
  mutable bool fully_loaded{ false };
  mutable std::unordered_map<std::string, std::vector<uint8_t>> contents;
  /** @brief Link path -> root-relative normalized target (symlinks and tar hardlinks) */
  mutable std::map<std::string, std::string> links;
  /** @brief True once a pass has seen every entry, so links is complete */
  mutable bool links_complete{ false };
  mutable std::list<std::string> lru_order;
  mutable std::unordered_map<std::string, std::pair<std::vector<uint8_t>, std::list<std::string>::iterator>> lru;
  mutable std::size_t lru_bytes{ 0 };
  mutable std::atomic<std::size_t> pass_count{ 0 };

  ArchiveReadPtr openOuter() const
  {
    if (!isCondaArchiveSupportAvailable())
      return nullptr;

    ArchiveReadPtr a(archive_read_new());
    if (!a)
      return nullptr;
    archive_read_support_format_zip(a.get());
    int r{ ARCHIVE_FATAL };
    if (bytes)
    {
      r = archive_read_open_memory(a.get(), bytes->data(), bytes->size());
    }
    else
    {
#ifdef _WIN32
      r = archive_read_open_filename_w(a.get(), file_path.c_str(), 65536);
#else
      r = archive_read_open_filename(a.get(), file_path.c_str(), 65536);
#endif
    }
    if (r != ARCHIVE_OK)
    {
      TESSERACT_LOG_ERROR("Failed to open conda package {}: {}", source_name, archiveError(a.get()));
      return nullptr;
    }
    return a;
  }

  /** @brief Advance an outer archive to a member. Returns false (and logs) if not found. */
  bool seekMember(struct archive* outer, const std::string& member) const
  {
    struct archive_entry* entry{ nullptr };
    int r{ ARCHIVE_OK };
    while ((r = archive_read_next_header(outer, &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN)
    {
      if (entryPath(entry) == member)
        return true;
    }
    if (r != ARCHIVE_EOF)
      TESSERACT_LOG_ERROR("Failed to read conda package {}: {}", source_name, archiveError(outer));
    else
      TESSERACT_LOG_ERROR("Conda package {} has no member {}", source_name, member);
    return false;
  }

  void recordLink(const std::string& path, struct archive_entry* entry) const
  {
    const std::string hardlink = entryHardlink(entry);
    std::string target;
    if (!hardlink.empty())
    {
      target = normalizeCondaInnerPath(hardlink);
    }
    else
    {
      const std::string symlink = entrySymlink(entry);
      if (symlink.empty() || symlink.front() == '/' || symlink.front() == '\\')
        return;  // absolute links point outside the archive
      target = innerJoin(innerDirname(path), symlink);
    }
    if (target.empty() || escapesRoot(target) || target == path)
      return;
    links[path] = target;
  }

  /**
   * @brief Decompress the pkg-*.tar.zst stream and visit entries
   * @param visitor Called for each regular file with its normalized path; may read the data. Return false to stop.
   * @return true if the pass ran (stopped early or reached the end), false on error
   */
  bool contentPass(const std::function<bool(const std::string&, struct archive*)>& visitor, bool& reached_end) const
  {
    reached_end = false;
    ArchiveReadPtr outer = openOuter();
    if (!outer)
      return false;
    if (!seekMember(outer.get(), pkg_member))
      return false;

    ++pass_count;
    NestedReadContext ctx;
    ctx.outer = outer.get();
    ArchiveReadPtr inner = newTarZstReader();
    if (!inner || archive_read_open(inner.get(), &ctx, nullptr, nestedReadCallback, nullptr) != ARCHIVE_OK)
    {
      TESSERACT_LOG_ERROR("Failed to open {} in conda package {}: {}",
                          pkg_member,
                          source_name,
                          inner ? archiveError(inner.get()) : std::string("out of memory"));
      return false;
    }

    struct archive_entry* entry{ nullptr };
    int r{ ARCHIVE_OK };
    while ((r = archive_read_next_header(inner.get(), &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN)
    {
      const std::string path = normalizeCondaInnerPath(entryPath(entry));
      if (path.empty())
        continue;
      const bool is_link = (archive_entry_filetype(entry) == AE_IFLNK) || !entryHardlink(entry).empty();
      if (is_link)
      {
        recordLink(path, entry);
        continue;
      }
      if (archive_entry_filetype(entry) != AE_IFREG)
        continue;
      if (!visitor(path, inner.get()))
        return true;
    }
    if (r != ARCHIVE_EOF)
    {
      TESSERACT_LOG_ERROR(
          "Failed to read {} in conda package {}: {}", pkg_member, source_name, archiveError(inner.get()));
      return false;
    }
    reached_end = true;
    links_complete = true;
    return true;
  }

  /** @brief Load every regular file into contents. content_mutex must be held. */
  bool fullLoadLocked() const
  {
    if (fully_loaded)
      return true;
    std::unordered_map<std::string, std::vector<uint8_t>> loaded;
    bool ok = true;
    bool reached_end{ false };
    const bool ran = contentPass(
        [&](const std::string& path, struct archive* a) {
          std::vector<uint8_t> data;
          if (!readEntryData(a, data))
          {
            TESSERACT_LOG_ERROR("Failed to read {} from conda package {}: {}", path, source_name, archiveError(a));
            ok = false;
            return false;
          }
          loaded[path] = std::move(data);
          return true;
        },
        reached_end);
    if (!ran || !ok || !reached_end)
      return false;
    contents = std::move(loaded);
    fully_loaded = true;
    lru.clear();
    lru_order.clear();
    lru_bytes = 0;
    return true;
  }

  /** @brief Rewrite a path through a known link (exact match or a link to a directory). nullopt if none applies. */
  std::optional<std::string> rewriteThroughLinks(const std::string& path) const
  {
    auto it = links.find(path);
    if (it != links.end())
      return it->second;

    // A link to a directory: the longest prefix that is a link
    std::string prefix = path;
    for (auto pos = prefix.find_last_of('/'); pos != std::string::npos; pos = prefix.find_last_of('/'))
    {
      prefix.resize(pos);
      auto lit = links.find(prefix);
      if (lit != links.end())
        return innerJoin(lit->second, path.substr(pos + 1));
    }
    return std::nullopt;
  }

  void lruInsert(const std::string& path, const std::vector<uint8_t>& data) const
  {
    const std::size_t limit = g_read_cache_limit.load();
    if (data.size() > limit)
      return;
    if (lru.find(path) != lru.end())
      return;
    while (!lru_order.empty() && lru_bytes + data.size() > limit)
    {
      const std::string& victim = lru_order.back();
      auto vit = lru.find(victim);
      lru_bytes -= vit->second.first.size();
      lru.erase(vit);
      lru_order.pop_back();
    }
    lru_order.push_front(path);
    lru.emplace(path, std::make_pair(data, lru_order.begin()));
    lru_bytes += data.size();
  }

  const std::vector<uint8_t>* lruFind(const std::string& path) const
  {
    auto it = lru.find(path);
    if (it == lru.end())
      return nullptr;
    lru_order.splice(lru_order.begin(), lru_order, it->second.second);
    return &it->second.first;
  }

  bool shouldFullLoad() const { return content_size.has_value() && *content_size <= g_full_load_limit.load(); }

  /** @brief Read one file. content_mutex must be held. */
  std::optional<std::vector<uint8_t>> readFileLocked(const std::string& requested) const
  {
    if (!fully_loaded && shouldFullLoad())
      fullLoadLocked();

    std::string path = requested;
    for (int depth = 0; depth <= kSymlinkDepthLimit; ++depth)
    {
      if (fully_loaded)
      {
        auto it = contents.find(path);
        if (it != contents.end())
          return it->second;
        auto next = rewriteThroughLinks(path);
        if (!next)
          return std::nullopt;
        path = *next;
        continue;
      }

      // Streaming
      if (const auto* cached = lruFind(path))
        return *cached;

      if (auto next = rewriteThroughLinks(path))
      {
        path = *next;
        continue;
      }

      std::optional<std::vector<uint8_t>> found;
      bool read_error = false;
      bool reached_end{ false };
      const bool ran = contentPass(
          [&](const std::string& entry_path, struct archive* a) {
            if (entry_path != path)
              return true;
            std::vector<uint8_t> data;
            if (!readEntryData(a, data))
            {
              TESSERACT_LOG_ERROR("Failed to read {} from conda package {}: {}", path, source_name, archiveError(a));
              read_error = true;
              return false;
            }
            found = std::move(data);
            return false;
          },
          reached_end);
      if (!ran || read_error)
        return std::nullopt;
      if (found)
      {
        lruInsert(path, *found);
        return found;
      }

      // Not a regular file. The pass recorded any link with this path (or, having reached the end, every link).
      auto next = rewriteThroughLinks(path);
      if (!next)
        return std::nullopt;
      path = *next;
    }
    TESSERACT_LOG_ERROR("Too many levels of symbolic links reading {} from conda package {}", requested, source_name);
    return std::nullopt;
  }

  bool readInfo()
  {
    ArchiveReadPtr outer = openOuter();
    if (!outer)
      return false;

    // Record the outer members, and read the info tar into memory
    std::vector<uint8_t> info_data;
    struct archive_entry* entry{ nullptr };
    int r{ ARCHIVE_OK };
    while ((r = archive_read_next_header(outer.get(), &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN)
    {
      const std::string member = entryPath(entry);
      if (startsWith(member, "info-") && endsWithCaseInsensitive(member, ".tar.zst"))
      {
        info_member = member;
        if (!readEntryData(outer.get(), info_data))
        {
          TESSERACT_LOG_ERROR(
              "Failed to read {} in conda package {}: {}", member, source_name, archiveError(outer.get()));
          return false;
        }
      }
      else if (startsWith(member, "pkg-") && endsWithCaseInsensitive(member, ".tar.zst"))
      {
        pkg_member = member;
      }
    }
    if (r != ARCHIVE_EOF)
    {
      TESSERACT_LOG_ERROR("Failed to read conda package {}: {}", source_name, archiveError(outer.get()));
      return false;
    }
    if (info_member.empty() || pkg_member.empty())
    {
      TESSERACT_LOG_ERROR("{} is not a valid conda package (missing info-*.tar.zst or pkg-*.tar.zst)", source_name);
      return false;
    }

    // Decompress the info tar and keep the top-level info/ files
    ArchiveReadPtr info = newTarZstReader();
    if (!info || archive_read_open_memory(info.get(), info_data.data(), info_data.size()) != ARCHIVE_OK)
    {
      TESSERACT_LOG_ERROR("Failed to open {} in conda package {}: {}",
                          info_member,
                          source_name,
                          info ? archiveError(info.get()) : std::string("out of memory"));
      return false;
    }
    while ((r = archive_read_next_header(info.get(), &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN)
    {
      const std::string path = normalizeCondaInnerPath(entryPath(entry));
      if (!startsWith(path, "info/") || archive_entry_filetype(entry) != AE_IFREG)
        continue;
      const std::string name = path.substr(5);
      if (name.empty() || name.find('/') != std::string::npos)
        continue;
      std::vector<uint8_t> data;
      if (!readEntryData(info.get(), data))
      {
        TESSERACT_LOG_ERROR("Failed to read {} in conda package {}: {}", path, source_name, archiveError(info.get()));
        return false;
      }
      info_files[name] = std::string(data.begin(), data.end());
    }
    if (r != ARCHIVE_EOF)
    {
      TESSERACT_LOG_ERROR(
          "Failed to read {} in conda package {}: {}", info_member, source_name, archiveError(info.get()));
      return false;
    }

    // Manifest: info/files, else info/paths.json
    std::optional<std::size_t> paths_json_count;
    std::size_t paths_json_size{ 0 };
    bool paths_json_size_known = false;
    auto paths_it = info_files.find("paths.json");
    if (paths_it != info_files.end())
    {
      try
      {
        const YAML::Node root = YAML::Load(paths_it->second);
        const YAML::Node& paths = root["paths"];
        if (paths && paths.IsSequence())
        {
          paths_json_count = paths.size();
          paths_json_size_known = true;
          std::vector<std::string> paths_files;
          for (const auto& p : paths)
          {
            if (p["_path"])
              paths_files.push_back(normalizeCondaInnerPath(p["_path"].as<std::string>()));
            const std::string type = p["path_type"] ? p["path_type"].as<std::string>() : std::string("hardlink");
            if (p["size_in_bytes"])
              paths_json_size += p["size_in_bytes"].as<std::size_t>();
            else if (type != "softlink" && type != "directory")
              paths_json_size_known = false;
          }
          if (info_files.find("files") == info_files.end())
            files = std::move(paths_files);
        }
      }
      catch (const std::exception& e)
      {
        TESSERACT_LOG_WARN("Failed to parse info/paths.json in conda package {}: {}", source_name, e.what());
        paths_json_count.reset();
        paths_json_size_known = false;
      }
    }

    auto files_it = info_files.find("files");
    if (files_it != info_files.end())
    {
      std::istringstream stream(files_it->second);
      std::string line;
      while (std::getline(stream, line))
      {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        const std::string p = normalizeCondaInnerPath(line);
        if (!p.empty())
          files.push_back(p);
      }
    }
    else if (files.empty())
    {
      TESSERACT_LOG_ERROR("Conda package {} has no manifest (info/files or info/paths.json)", source_name);
      return false;
    }

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    directories.insert("");
    for (const auto& f : files)
    {
      for (auto pos = f.find('/'); pos != std::string::npos; pos = f.find('/', pos + 1))
        directories.insert(f.substr(0, pos));
    }

    if (paths_json_size_known && paths_json_count && *paths_json_count == files.size())
      content_size = paths_json_size;

    // Identity metadata
    auto index_it = info_files.find("index.json");
    if (index_it != info_files.end())
    {
      try
      {
        const YAML::Node index = YAML::Load(index_it->second);
        if (index["name"])
          name = index["name"].as<std::string>();
        if (index["version"])
          version = index["version"].as<std::string>();
        if (index["build"])
          build = index["build"].as<std::string>();
      }
      catch (const std::exception& e)
      {
        TESSERACT_LOG_WARN("Failed to parse info/index.json in conda package {}: {}", source_name, e.what());
      }
    }
    return true;
  }
};

CondaPackageArchive::CondaPackageArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CondaPackageArchive::~CondaPackageArchive() = default;

CondaPackageArchive::ConstPtr CondaPackageArchive::open(std::unique_ptr<Impl> impl)
{
  if (!impl->readInfo())
    return nullptr;
  return std::shared_ptr<const CondaPackageArchive>(new CondaPackageArchive(std::move(impl)));  // NOLINT
}

CondaPackageArchive::ConstPtr CondaPackageArchive::openFile(const std::filesystem::path& path)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec))
  {
    TESSERACT_LOG_ERROR("Conda package does not exist: {}", path.string());
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->file_path = path;
  impl->source_name = normalizeOuterPath(path.string());
  return open(std::move(impl));
}

CondaPackageArchive::ConstPtr CondaPackageArchive::openBytes(std::shared_ptr<const std::vector<uint8_t>> data,
                                                             std::string name)
{
  if (!data || data->empty())
  {
    TESSERACT_LOG_ERROR("Conda package {} has no data", name);
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->bytes = std::move(data);
  impl->source_name = std::move(name);
  return open(std::move(impl));
}

const std::string& CondaPackageArchive::sourceName() const { return impl_->source_name; }
bool CondaPackageArchive::isInMemory() const { return impl_->bytes != nullptr; }
const std::string& CondaPackageArchive::name() const { return impl_->name; }
const std::string& CondaPackageArchive::version() const { return impl_->version; }
const std::string& CondaPackageArchive::build() const { return impl_->build; }

CondaPackageIdentity CondaPackageArchive::identity() const
{
  CondaPackageIdentity id;
  id.name = impl_->name;
  id.version = impl_->version;
  id.build = impl_->build;

  std::lock_guard<std::mutex> lock(impl_->sha_mutex);
  if (impl_->sha256.empty())
  {
    if (impl_->bytes)
    {
      impl_->sha256 = computeSha256Hex(impl_->bytes->data(), impl_->bytes->size());
    }
    else
    {
      std::ifstream ifs(impl_->file_path, std::ios::binary);
      if (!ifs)
      {
        TESSERACT_LOG_ERROR("Failed to read conda package {} to compute its sha256", impl_->source_name);
      }
      else
      {
        Sha256 sha;
        std::vector<char> buf(std::size_t(1) << 20);
        while (ifs)
        {
          ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
          const auto n = static_cast<std::size_t>(ifs.gcount());
          if (n > 0)
            sha.update(reinterpret_cast<const uint8_t*>(buf.data()), n);  // NOLINT
        }
        impl_->sha256 = sha.hexDigest();
      }
    }
  }
  id.sha256 = impl_->sha256;
  return id;
}

const std::vector<std::string>& CondaPackageArchive::files() const { return impl_->files; }

bool CondaPackageArchive::hasFile(const std::string& inner_path) const
{
  return std::binary_search(impl_->files.begin(), impl_->files.end(), normalizeCondaInnerPath(inner_path));
}

bool CondaPackageArchive::hasDirectory(const std::string& inner_path) const
{
  return impl_->directories.count(normalizeCondaInnerPath(inner_path)) > 0;
}

std::vector<std::string> CondaPackageArchive::listFiles(const std::string& prefix) const
{
  std::vector<std::string> out;
  auto it = std::lower_bound(impl_->files.begin(), impl_->files.end(), prefix);
  for (; it != impl_->files.end() && startsWith(*it, prefix); ++it)
    out.push_back(*it);
  return out;
}

std::optional<std::string> CondaPackageArchive::infoFile(const std::string& name) const
{
  auto it = impl_->info_files.find(name);
  if (it == impl_->info_files.end())
    return std::nullopt;
  return it->second;
}

std::vector<uint8_t> CondaPackageArchive::readFile(const std::string& inner_path) const
{
  const std::string path = normalizeCondaInnerPath(inner_path);
  std::lock_guard<std::mutex> lock(impl_->content_mutex);
  auto data = impl_->readFileLocked(path);
  if (!data)
  {
    TESSERACT_LOG_ERROR("File {} not found in conda package {}", path, impl_->source_name);
    return {};
  }
  return std::move(*data);
}

std::map<std::string, std::vector<uint8_t>>
CondaPackageArchive::readFiles(const std::vector<std::string>& inner_paths) const
{
  std::map<std::string, std::vector<uint8_t>> out;
  std::lock_guard<std::mutex> lock(impl_->content_mutex);
  if (!impl_->fully_loaded && impl_->shouldFullLoad())
    impl_->fullLoadLocked();

  std::map<std::string, std::vector<std::string>> wanted;  // normalized -> requested
  for (const auto& p : inner_paths)
    wanted[normalizeCondaInnerPath(p)].push_back(p);

  bool pass_reached_end = false;
  if (!impl_->fully_loaded)
  {
    // One pass for everything not cached; links are resolved afterwards
    std::set<std::string> pending;
    for (const auto& w : wanted)
    {
      if (const auto* cached = impl_->lruFind(w.first))
      {
        for (const auto& req : w.second)
          out[req] = *cached;
      }
      else
      {
        pending.insert(w.first);
      }
    }
    if (!pending.empty())
    {
      bool reached_end{ false };
      impl_->contentPass(
          [&](const std::string& entry_path, struct archive* a) {
            auto it = pending.find(entry_path);
            if (it == pending.end())
              return true;
            std::vector<uint8_t> data;
            if (readEntryData(a, data))
            {
              impl_->lruInsert(entry_path, data);
              for (const auto& req : wanted[entry_path])
                out[req] = data;
            }
            else
            {
              TESSERACT_LOG_ERROR(
                  "Failed to read {} from conda package {}: {}", entry_path, impl_->source_name, archiveError(a));
            }
            pending.erase(it);
            return !pending.empty();
          },
          reached_end);
      pass_reached_end = reached_end;
    }
  }

  // Lookups (full load), and links or leftovers (streaming)
  for (const auto& w : wanted)
  {
    if (out.find(w.second.front()) != out.end())
      continue;
    // A complete pass did not find it, and no link leads elsewhere: missing, no need for another pass
    std::optional<std::vector<uint8_t>> data;
    if (!pass_reached_end || impl_->rewriteThroughLinks(w.first).has_value())
      data = impl_->readFileLocked(w.first);
    if (!data)
    {
      TESSERACT_LOG_ERROR("File {} not found in conda package {}", w.first, impl_->source_name);
      continue;
    }
    for (const auto& req : w.second)
      out[req] = *data;
  }
  return out;
}

void CondaPackageArchive::preload() const
{
  std::lock_guard<std::mutex> lock(impl_->content_mutex);
  impl_->fullLoadLocked();
}

void CondaPackageArchive::releaseContents() const
{
  std::lock_guard<std::mutex> lock(impl_->content_mutex);
  impl_->contents.clear();
  impl_->fully_loaded = false;
  impl_->lru.clear();
  impl_->lru_order.clear();
  impl_->lru_bytes = 0;
}

bool CondaPackageArchive::isFullyLoaded() const
{
  std::lock_guard<std::mutex> lock(impl_->content_mutex);
  return impl_->fully_loaded;
}

std::optional<std::size_t> CondaPackageArchive::contentSize() const { return impl_->content_size; }

std::size_t CondaPackageArchive::contentPassCount() const { return impl_->pass_count.load(); }

void CondaPackageArchive::setFullLoadLimit(std::size_t bytes) { g_full_load_limit = bytes; }
std::size_t CondaPackageArchive::fullLoadLimit() { return g_full_load_limit.load(); }
void CondaPackageArchive::setReadCacheLimit(std::size_t bytes) { g_read_cache_limit = bytes; }
std::size_t CondaPackageArchive::readCacheLimit() { return g_read_cache_limit.load(); }

/* ---------------------------------------------------------------------------------------------
 * Layer 3
 * ------------------------------------------------------------------------------------------- */

CondaPackageArchiveCache& CondaPackageArchiveCache::global()
{
  static CondaPackageArchiveCache cache;
  return cache;
}

CondaPackageArchive::ConstPtr CondaPackageArchiveCache::get(const std::string& archive_path)
{
  const std::string key = normalizeOuterPath(archive_path);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registered_.find(key);
    if (it != registered_.end())
      return it->second;
  }

  std::error_code ec;
  const std::filesystem::path path(key);
  if (!std::filesystem::is_regular_file(path, ec))
  {
    TESSERACT_LOG_ERROR("Conda package does not exist: {}", archive_path);
    return nullptr;
  }
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (ec)
    canonical = path;
  const std::string canonical_key = canonical.generic_string();
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, ec);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(canonical_key);
    if (it != files_.end())
    {
      auto archive = it->second.archive.lock();
      if (archive && it->second.size == size && it->second.mtime == mtime)
        return archive;
    }
  }

  // Open outside the lock, so other archives stay available meanwhile
  auto archive = CondaPackageArchive::openFile(path);
  if (!archive)
    return nullptr;

  std::lock_guard<std::mutex> lock(mutex_);
  auto& entry = files_[canonical_key];
  auto existing = entry.archive.lock();
  if (existing && entry.size == size && entry.mtime == mtime)
    return existing;  // another thread won the race
  entry.archive = archive;
  entry.size = size;
  entry.mtime = mtime;
  return archive;
}

void CondaPackageArchiveCache::add(const std::string& key, CondaPackageArchive::ConstPtr archive)
{
  if (!archive)
    return;
  std::lock_guard<std::mutex> lock(mutex_);
  registered_[normalizeOuterPath(key)] = std::move(archive);
}

void CondaPackageArchiveCache::remove(const std::string& key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  registered_.erase(normalizeOuterPath(key));
}

bool CondaPackageArchiveCache::hasKey(const std::string& key) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (registered_.empty())
    return false;
  return registered_.find(normalizeOuterPath(key)) != registered_.end();
}

bool CondaPackageArchiveCache::matchesIdentity(const CondaPackageArchive& archive, const CondaPackageIdentity& identity)
{
  if (!identity.name.empty() && identity.name != archive.name())
    return false;
  if (!identity.version.empty() && identity.version != archive.version())
    return false;
  if (!identity.build.empty() && identity.build != archive.build())
    return false;
  if (!identity.sha256.empty())
    return toLower(identity.sha256) == archive.identity().sha256;
  // Without a hash, name, version and build must all be given
  return !identity.name.empty() && !identity.version.empty() && !identity.build.empty();
}

CondaPackageArchive::ConstPtr CondaPackageArchiveCache::findByIdentity(const CondaPackageIdentity& identity) const
{
  std::vector<CondaPackageArchive::ConstPtr> candidates;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& r : registered_)
      candidates.push_back(r.second);
    for (const auto& f : files_)
    {
      if (auto a = f.second.archive.lock())
        candidates.push_back(a);
    }
  }
  for (const auto& c : candidates)
  {
    if (matchesIdentity(*c, identity))
      return c;
  }
  return nullptr;
}

CondaPackageArchive::ConstPtr CondaPackageArchiveCache::resolve(const CondaPackageIdentity& identity) const
{
  if (auto a = findByIdentity(identity))
    return a;

  std::shared_ptr<Resolver> resolver;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    resolver = resolver_;
  }
  if (!resolver || !*resolver)
    return nullptr;

  auto a = (*resolver)(identity);
  if (a && !matchesIdentity(*a, identity))
  {
    TESSERACT_LOG_ERROR("Conda package resolver returned {} for {}", a->identity().toString(), identity.toString());
    return nullptr;
  }
  return a;
}

void CondaPackageArchiveCache::setResolver(Resolver resolver)
{
  std::lock_guard<std::mutex> lock(mutex_);
  resolver_ = resolver ? std::make_shared<Resolver>(std::move(resolver)) : nullptr;
}

void CondaPackageArchiveCache::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  files_.clear();
  registered_.clear();
}

/* ---------------------------------------------------------------------------------------------
 * Layer 3b
 * ------------------------------------------------------------------------------------------- */

namespace
{
std::string normalizeIndexPackagePath(const std::string& name, const std::string& value)
{
  const std::string generic = toGenericSeparators(value);
  const bool has_drive =
      generic.size() >= 2 && std::isalpha(static_cast<unsigned char>(generic[0])) != 0 && generic[1] == ':';
  if (generic.empty() || generic.front() == '/' || has_drive)
    throw std::runtime_error("tesseract resource index: path of package '" + name +
                             "' must be relative to the archive root: '" + value + "'");
  const std::string normalized = normalizeCondaInnerPath(generic);
  if (escapesRoot(normalized) || normalized.find("/../") != std::string::npos)
    throw std::runtime_error("tesseract resource index: path of package '" + name + "' leaves the archive root: '" +
                             value + "'");
  return normalized;
}

/** @brief Drop packages whose directory is not in the archive */
void filterIndexDirectories(TesseractResourceIndex& index, const CondaPackageArchive& archive)
{
  for (auto it = index.packages.begin(); it != index.packages.end();)
  {
    if (!archive.hasDirectory(it->second))
    {
      TESSERACT_LOG_ERROR("Conda package {}: resource index maps package '{}' to '{}', which is not a directory in "
                          "the archive; ignoring it",
                          archive.sourceName(),
                          it->first,
                          it->second);
      it = index.packages.erase(it);
    }
    else
    {
      ++it;
    }
  }
}
}  // namespace

TesseractResourceIndex parseTesseractResourceIndex(const YAML::Node& index_object)
{
  if (!index_object.IsDefined() || !index_object.IsMap())
    throw std::runtime_error("tesseract resource index must be a map");

  const YAML::Node& version_node = index_object["version"];
  if (!version_node.IsDefined() || version_node.IsNull())
    throw std::runtime_error("tesseract resource index: missing 'version'");
  if (!version_node.IsScalar())
    throw std::runtime_error("tesseract resource index: 'version' must be an integer");

  TesseractResourceIndex index;
  try
  {
    index.version = version_node.as<int>();  // accepts 1 and "1" (conda-build stringifies recipe extra values)
  }
  catch (const YAML::Exception&)
  {
    throw std::runtime_error("tesseract resource index: 'version' must be an integer, got '" + version_node.Scalar() +
                             "'");
  }
  if (index.version < 1)
    throw std::runtime_error("tesseract resource index: invalid version " + std::to_string(index.version));
  if (index.version > kTesseractResourceIndexVersion)
    throw std::runtime_error("tesseract resource index: version " + std::to_string(index.version) +
                             " is newer than the supported version " + std::to_string(kTesseractResourceIndexVersion));

  const YAML::Node& packages = index_object["packages"];
  if (!packages.IsDefined() || !packages.IsMap())
    throw std::runtime_error("tesseract resource index: 'packages' must be a map of package name to directory");
  for (const auto& p : packages)
  {
    if (!p.first.IsScalar() || !p.second.IsScalar())
      throw std::runtime_error("tesseract resource index: 'packages' must be a map of package name to directory");
    const std::string name = p.first.Scalar();
    if (name.empty())
      throw std::runtime_error("tesseract resource index: empty package name");
    index.packages[name] = normalizeIndexPackagePath(name, p.second.Scalar());
  }
  index.raw = YAML::Clone(index_object);
  return index;
}

TesseractResourceIndex parseTesseractResourceIndexFile(const std::string& yaml_text)
{
  YAML::Node root;
  try
  {
    root = YAML::Load(yaml_text);
  }
  catch (const YAML::Exception& e)
  {
    throw std::runtime_error(std::string("tesseract resource index file is not valid YAML: ") + e.what());
  }
  if (!root.IsMap() || !root[kTesseractResourceIndexKey])
    throw std::runtime_error(std::string("tesseract resource index file has no top-level '") +
                             kTesseractResourceIndexKey + "' key");
  auto index = parseTesseractResourceIndex(root[kTesseractResourceIndexKey]);
  index.source = TesseractResourceIndexSource::DataFile;
  return index;
}

std::optional<TesseractResourceIndex> parseTesseractResourceIndexFromAboutJson(const std::string& about_json_text)
{
  YAML::Node root;
  try
  {
    root = YAML::Load(about_json_text);
  }
  catch (const YAML::Exception& e)
  {
    throw std::runtime_error(std::string("about.json is not valid JSON: ") + e.what());
  }
  if (!root.IsMap())
    throw std::runtime_error("about.json must contain a JSON object");

  const YAML::Node& extra = root["extra"];
  if (!extra.IsDefined() || !extra.IsMap())
    return std::nullopt;
  YAML::Node entry = extra[kTesseractResourceIndexKey];
  if (!entry.IsDefined() || entry.IsNull())
    return std::nullopt;

  if (entry.IsScalar())
  {
    // The --extra-meta form: the index as a JSON-encoded string
    const std::string text = entry.Scalar();
    try
    {
      entry = YAML::Load(text);
    }
    catch (const YAML::Exception& e)
    {
      throw std::runtime_error(std::string("about.json extra.") + kTesseractResourceIndexKey +
                               " is a string that is not valid JSON: " + e.what());
    }
    if (!entry.IsMap())
      throw std::runtime_error(std::string("about.json extra.") + kTesseractResourceIndexKey +
                               " is a string that is not a JSON object");
  }

  auto index = parseTesseractResourceIndex(entry);
  index.source = TesseractResourceIndexSource::AboutJson;
  return index;
}

std::optional<TesseractResourceIndex> readTesseractResourceIndexFromAboutJson(const CondaPackageArchive& archive)
{
  auto about = archive.infoFile("about.json");
  if (!about)
    return std::nullopt;
  auto index = parseTesseractResourceIndexFromAboutJson(*about);
  if (index)
    filterIndexDirectories(*index, archive);
  return index;
}

std::optional<TesseractResourceIndex> readTesseractResourceIndexFromDataFiles(const CondaPackageArchive& archive)
{
  const std::string dir = std::string(kTesseractResourceIndexDataDir) + "/";
  std::vector<std::string> candidates;
  for (const auto& f : archive.listFiles(dir))
  {
    const std::string rest = f.substr(dir.size());
    if (rest.find('/') == std::string::npos && endsWithCaseInsensitive(rest, ".yaml"))
      candidates.push_back(f);
  }
  if (candidates.empty())
    return std::nullopt;

  auto contents = archive.readFiles(candidates);
  std::optional<TesseractResourceIndex> merged;
  for (const auto& file : candidates)
  {
    auto it = contents.find(file);
    if (it == contents.end())
    {
      TESSERACT_LOG_ERROR("Conda package {}: could not read resource index file {}", archive.sourceName(), file);
      continue;
    }
    TesseractResourceIndex index;
    try
    {
      index = parseTesseractResourceIndexFile(std::string(it->second.begin(), it->second.end()));
    }
    catch (const std::exception& e)
    {
      TESSERACT_LOG_ERROR(
          "Conda package {}: skipping resource index file {}: {}", archive.sourceName(), file, e.what());
      continue;
    }

    if (!merged)
    {
      merged = std::move(index);
      merged->source = TesseractResourceIndexSource::DataFile;
      merged->source_files.push_back(file);
      continue;
    }
    merged->source_files.push_back(file);
    for (const auto& p : index.packages)
    {
      auto existing = merged->packages.find(p.first);
      if (existing != merged->packages.end())
      {
        TESSERACT_LOG_WARN("Conda package {}: package '{}' is listed by more than one resource index file; keeping "
                           "'{}' and ignoring '{}' from {}",
                           archive.sourceName(),
                           p.first,
                           existing->second,
                           p.second,
                           file);
        continue;
      }
      merged->packages.insert(p);
    }
  }
  if (merged)
    filterIndexDirectories(*merged, archive);
  return merged;
}

std::optional<TesseractResourceIndex> readTesseractResourceIndex(const CondaPackageArchive& archive)
{
  try
  {
    auto index = readTesseractResourceIndexFromAboutJson(archive);
    if (index)
      return index;
  }
  catch (const std::exception& e)
  {
    // Don't fall back to the data-side files, so the broken entry is noticed
    TESSERACT_LOG_ERROR(
        "Conda package {}: invalid tesseract resource index in info/about.json: {}", archive.sourceName(), e.what());
    return std::nullopt;
  }

  try
  {
    return readTesseractResourceIndexFromDataFiles(archive);
  }
  catch (const std::exception& e)
  {
    TESSERACT_LOG_ERROR(
        "Conda package {}: failed to read the tesseract resource index files: {}", archive.sourceName(), e.what());
    return std::nullopt;
  }
}

/* ---------------------------------------------------------------------------------------------
 * Layer 4
 * ------------------------------------------------------------------------------------------- */

std::map<std::string, std::string> findRosPackagesInCondaArchive(const CondaPackageArchive& archive,
                                                                 const std::string& inner_root)
{
  std::map<std::string, std::string> packages;
  const std::string root = normalizeCondaInnerPath(inner_root);

  auto packageName = [&](const std::string& dir) {
    if (dir.empty())
      return archive.name();
    auto pos = dir.find_last_of('/');
    return (pos == std::string::npos) ? dir : dir.substr(pos + 1);
  };

  const std::string root_manifest = root.empty() ? std::string("package.xml") : root + "/package.xml";
  if (archive.hasFile(root_manifest))
  {
    const std::string name = packageName(root);
    if (!name.empty())
      packages[name] = root;
    return packages;
  }

  std::vector<std::string> dirs;
  for (const auto& f : archive.listFiles(root.empty() ? std::string() : root + "/"))
  {
    if (f == "package.xml" || (f.size() > 12 && f.compare(f.size() - 12, 12, "/package.xml") == 0))
      dirs.push_back(innerDirname(f));
  }
  std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) {
    const auto da = std::count(a.begin(), a.end(), '/');
    const auto db = std::count(b.begin(), b.end(), '/');
    return (da != db) ? (da < db) : (a < b);
  });

  std::vector<std::string> found_dirs;
  for (const auto& dir : dirs)
  {
    const bool nested = std::any_of(found_dirs.begin(), found_dirs.end(), [&](const std::string& parent) {
      return parent.empty() || startsWith(dir, parent + "/");
    });
    if (nested)
      continue;
    found_dirs.push_back(dir);
    const std::string name = packageName(dir);
    if (!name.empty() && packages.find(name) == packages.end())
      packages[name] = dir;
  }
  return packages;
}

std::shared_ptr<Resource> locateCondaResource(const std::string& url,
                                              const std::string& resolved_path,
                                              ResourceLocator::ConstPtr parent,
                                              CondaPackageArchiveCache& cache)
{
  auto cp = parseCondaPackagePath(resolved_path, cache);
  if (!cp)
    return nullptr;

  auto archive = cache.get(cp->archive_path);
  if (!archive)
    return nullptr;

  if (!archive->hasFile(cp->inner_path))
  {
    TESSERACT_LOG_ERROR(
        "Resource {} not found: conda package {} has no file {}", url, cp->archive_path, cp->inner_path);
    return nullptr;
  }
  return std::make_shared<CondaArchiveResource>(url, archive, cp->inner_path, std::move(parent), cp->archive_path);
}

/* ---------------------------------------------------------------------------------------------
 * CondaArchiveResource
 * ------------------------------------------------------------------------------------------- */

CondaArchiveResource::CondaArchiveResource(std::string url, CondaPackagePath path, ResourceLocator::ConstPtr parent)
  : url_(std::move(url))
  , inner_path_(normalizeCondaInnerPath(path.inner_path))
  , parent_(std::move(parent))
  , archive_path_(std::move(path.archive_path))
{
}

CondaArchiveResource::CondaArchiveResource(std::string url,
                                           CondaPackageArchive::ConstPtr archive,
                                           std::string inner_path,
                                           ResourceLocator::ConstPtr parent,
                                           std::string archive_path)
  : url_(std::move(url))
  , inner_path_(normalizeCondaInnerPath(inner_path))
  , parent_(std::move(parent))
  , archive_path_(std::move(archive_path))
  , archive_(std::move(archive))
{
  if (archive_path_.empty() && archive_)
    archive_path_ = archive_->sourceName();
}

CondaPackageArchive::ConstPtr CondaArchiveResource::getArchive() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (archive_)
    return archive_;

  auto& cache = CondaPackageArchiveCache::global();
  const bool has_identity = !identity_.name.empty() || !identity_.sha256.empty();
  if (has_identity)
  {
    // Deserialized: find the package by identity, then try the recorded path
    if (auto a = cache.resolve(identity_))
    {
      archive_ = a;
      archive_path_ = a->sourceName();
      return archive_;
    }

    std::error_code ec;
    if (!archive_path_.empty() &&
        (cache.hasKey(archive_path_) || std::filesystem::is_regular_file(std::filesystem::path(archive_path_), ec)))
    {
      auto a = cache.get(archive_path_);
      if (a && CondaPackageArchiveCache::matchesIdentity(*a, identity_))
        archive_ = a;
    }
  }
  else if (!archive_path_.empty())
  {
    archive_ = cache.get(archive_path_);
  }

  if (!archive_ && !unavailable_logged_)
  {
    unavailable_logged_ = true;
    if (has_identity)
      TESSERACT_LOG_ERROR("conda package {} not available", identity_.toString());
    else
      TESSERACT_LOG_ERROR("conda package {} not available", archive_path_);
  }
  return archive_;
}

bool CondaArchiveResource::isFile() const { return false; }

std::string CondaArchiveResource::getUrl() const { return url_; }

std::string CondaArchiveResource::getArchivePath() const
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!archive_path_.empty())
      return archive_path_;
  }
  auto a = getArchive();
  return a ? a->sourceName() : std::string();
}

std::string CondaArchiveResource::getFilePath() const { return makeCondaPackagePath(getArchivePath(), inner_path_); }

const std::string& CondaArchiveResource::getInnerPath() const { return inner_path_; }

CondaPackageIdentity CondaArchiveResource::getIdentity() const
{
  auto a = getArchive();
  if (a)
    return a->identity();
  std::lock_guard<std::mutex> lock(mutex_);
  return identity_;
}

std::vector<uint8_t> CondaArchiveResource::getResourceContents() const
{
  auto a = getArchive();
  if (!a)
    return {};
  return a->readFile(inner_path_);
}

std::shared_ptr<std::istream> CondaArchiveResource::getResourceContentStream() const
{
  auto a = getArchive();
  if (!a)
    return nullptr;
  if (!a->hasFile(inner_path_))
  {
    TESSERACT_LOG_ERROR("File {} not found in conda package {}", inner_path_, a->sourceName());
    return nullptr;
  }
  const std::vector<uint8_t> data = a->readFile(inner_path_);
  return std::make_shared<std::stringstream>(std::string(data.begin(), data.end()));
}

Resource::Ptr CondaArchiveResource::locateResource(const std::string& url) const
{
  if (url.empty())
    return nullptr;

  if (isRelativePath(url))
  {
    const std::string new_url = resolveRelativeResourceUrl(url_, url);

    // Look inside this resource's own archive first; this works without a locator
    if (auto a = getArchive())
    {
      const std::string sibling = innerJoin(innerDirname(inner_path_), std::filesystem::path(url).filename().string());
      if (a->hasFile(sibling))
      {
        const std::string archive_path = getArchivePath();
        return std::make_shared<CondaArchiveResource>(
            new_url.empty() ? makeCondaPackagePath(archive_path, sibling) : new_url, a, sibling, parent_, archive_path);
      }
    }

    if (parent_ == nullptr || new_url.empty())
      return nullptr;
    return parent_->locateResource(new_url);
  }

  if (parent_ != nullptr)
    return parent_->locateResource(url);

  // Without a locator, absolute archive paths can still be resolved
  std::string path = url;
  if (startsWith(path, "file://"))
    path.erase(0, std::strlen("file://"));
  return locateCondaResource(url, path, nullptr);
}

bool CondaArchiveResource::operator==(const CondaArchiveResource& rhs) const
{
  bool equal = true;
  equal &= Resource::operator==(rhs);
  equal &= url_ == rhs.url_;
  equal &= inner_path_ == rhs.inner_path_;
  equal &= getIdentity() == rhs.getIdentity();
  return equal;
}

bool CondaArchiveResource::operator!=(const CondaArchiveResource& rhs) const { return !operator==(rhs); }

}  // namespace tesseract::common
