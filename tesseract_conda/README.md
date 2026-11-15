# tesseract_conda

Read [Tesseract](https://github.com/tesseract-robotics/tesseract) resources (URDF, SRDF, meshes, YAML configs)
directly from conda (`.conda`) packages, such as RoboStack's `ros-<distro>-*` packages, without extracting them.

```cpp
#include <tesseract/conda/conda_resource_locator.h>

// Reads TESSERACT_RESOURCE_PATH, ROS_PACKAGE_PATH and AMENT_PREFIX_PATH, like GeneralResourceLocator
auto locator = std::make_shared<tesseract::conda::CondaResourceLocator>();
locator->addPath("/x/ros-humble-my-robot-description-1.0.0-h123_0.conda/share");

auto urdf = locator->locateResource("package://my_robot_description/urdf/robot.urdf");
env.init(urdf->getFilePath(), srdf_path, locator);
```

Requires a tesseract with resource-based YAML/SRDF/URDF loading (the `feature/non-file-resources` changes),
libarchive with zstd support, and OpenSSL (libcrypto).

## Paths

`CondaResourceLocator` takes paths from its constructor, `addPath()` and environment variables (`:` separated, `;`
on Windows). Each path is one of:

| Path | Meaning |
| --- | --- |
| `/ws/src` | A directory, searched for ROS packages (directories with `package.xml`) like `GeneralResourceLocator`. Ignored when the locator is constructed with `use_filesystem = false`. |
| `/x/foo.conda/share` | A `.conda` package plus a directory inside it: the archive's manifest is searched under that directory for `package.xml`. |
| `/x/foo.conda` | A bare `.conda` package: the packages come from its **tesseract resource index** (below). Without one, this is an error. |

Directories are never searched for `.conda` files. The first registration of a package name wins, on disk or in an
archive. Resources inside archives are `CondaArchiveResource`: `isFile()` is false, and `getFilePath()` returns the
path inside the archive (`/x/foo.conda/share/foo/urdf/robot.urdf`), which the locator accepts again.

Meshes inside archives are loaded from bytes, so external textures and `.mtl` files can't be resolved. Use
self-contained meshes (`.glb` or `.stl`) in conda packages.

## In-memory packages and isolation

A package received over the network doesn't need to be written to disk. Register it in a cache under a virtual path
and add that path:

```cpp
using namespace tesseract::conda;

auto cache = std::make_shared<CondaPackageArchiveCache>();  // or CondaPackageArchiveCache::global()
const std::string key = "/remote/ros-humble-foo-1.0-h123_0.conda";
cache->add(key, CondaPackageArchive::openBytes(std::make_shared<const std::vector<uint8_t>>(std::move(bytes)), key));

auto locator = std::make_shared<CondaResourceLocator>(std::vector<std::filesystem::path>{ key + "/share" },
                                                      CondaResourceLocator::defaultEnvironmentVariables(),
                                                      true,
                                                      cache);
```

The locator, and every resource it returns, uses its cache to open archives and find registered ones. The cache
defaults to `CondaPackageArchiveCache::global()`. Several independent users in one process (for example hosted
planner instances) should each create their own cache, so their registrations, resolvers (`setResolver`) and read
options stay apart.

Deserialization can't take a parameter, so cereal-loaded locators and resources use
`CondaPackageArchiveCache::deserializationCache()`: the cache of the innermost `DeserializationScope` on the current
thread, otherwise the global cache.

```cpp
{
  CondaPackageArchiveCache::DeserializationScope scope(cache);
  auto env = tesseract::common::Serialization::fromArchiveBinaryData<tesseract::environment::Environment::Ptr>(data);
}
```

## Tesseract resource index (for packagers)

A bare archive path works when the package says which ROS packages it contains:

```yaml
tesseract_resource_index:
  version: 1
  packages:            # package name -> directory, relative to the archive root
    ur_description: share/ur_description
```

Put it in two places:

1. **The recipe's `extra:` section**: `extra: {tesseract_resource_index: {version: 1, packages: {...}}}`.
   rattler-build and conda-build both copy it to `info/about.json`, where it is read without decompressing the
   package contents. (conda-build writes `version` as the string `"1"`; that is accepted.)
   `--extra-meta tesseract_resource_index='{"version":1,"packages":{...}}'` works too.
2. **`share/tesseract/resource_index/<conda-package-name>.yaml`** in the package contents, with the YAML above. It
   survives installation into an environment and is used when `about.json` has no entry.

If both exist, `about.json` wins. A broken `about.json` entry is reported as an error, without falling back.

## Library layers

Everything is in `tesseract/conda/conda_package.h` and can be used without the locator:

- `parseCondaPackagePath` / `makeCondaPackagePath`: split `/x/foo.conda/share/a.stl` into archive and inner path.
- `CondaPackageArchive`: open a `.conda` file or in-memory bytes; manifest, metadata, `readFile`/`readFiles`.
  Archives up to `CondaArchiveOptions::full_load_limit` (256 MB) are loaded whole on first read; larger ones are
  streamed with an LRU cache of `read_cache_limit` bytes (64 MB). The options are given per archive (`openFile`,
  `openBytes`) or per cache (for the archives it opens from files). `preload()` / `releaseContents()` give explicit control. Identity is name, version, build
  and sha256.
- `CondaPackageArchiveCache`: shares open archives, registers in-memory archives under virtual paths
  (`add("/remote/foo.conda", archive)`), looks archives up by identity, and has a resolver hook to fetch packages
  that aren't available yet.
- Resource index parsers and readers.
- `CondaArchiveResource`, serialized by package identity (`tesseract/conda/cereal_serialization.h`).

## Building

```bash
cmake -S . -B build -DTESSERACT_CONDA_ENABLE_TESTING=ON -DCMAKE_PREFIX_PATH=<tesseract install>
cmake --build build && ctest --test-dir build
```

Dependencies: tesseract (common; plus geometry, scene_graph, srdf, urdf and environment for the tests), libarchive
(`libarchive-dev`, conda-forge `libarchive`, Homebrew `libarchive` with `-DLibArchive_ROOT=$(brew --prefix
libarchive)`, vcpkg `libarchive[zstd]`), OpenSSL's libcrypto for SHA-256 (`libssl-dev`, conda-forge/vcpkg
`openssl`), yaml-cpp, cereal, GTest.
