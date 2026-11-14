# Windows: building, testing and known risks

This RMW and its tests are built and tested on Windows on every CI run
(`windows-2022` GitHub runner, MSVC 2022, ROS 2 Jazzy from RoboStack; see
`ci/github/workflows/rmw_intraprocess.yml`). This page explains how to do the
same on your own Windows machine, which rules keep the code portable, and what
is known to go wrong on Windows.

## Build and test with RoboStack (MSVC)

You need [pixi](https://pixi.sh) and Visual Studio 2022 (or the Build Tools)
with the "Desktop development with C++" workload. pixi installs ROS 2 Jazzy,
the compilers' activation scripts and the build tools into the workspace; no
system-wide ROS installation is used.

1. Lay out a workspace with this repository under `src/` and the CI
   environment file at the root (this is what `scripts/push-ci.sh` does for
   CI):

   ```
   C:\ws\
     pixi.toml                 (copy of ci\pixi.toml)
     src\rmw_intraprocess\     (this repository)
   ```

   Keep the workspace path short (for example `C:\ws`). rosidl generates
   Python-extension object files with long paths; with a long workspace path
   they exceed `MAX_PATH` and MSVC fails with `C1083: Cannot open compiler
   generated file`. CI builds into `C:\b` and installs into `C:\i` for this
   reason.

2. Set the build and install directories the pixi tasks use, then build and
   test (from a normal PowerShell; pixi activates the MSVC environment):

   ```powershell
   cd C:\ws
   $env:CI_BUILD_BASE = "C:/ws/build"
   $env:CI_INSTALL_BASE = "C:/ws/install"
   $env:AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS = "1"
   pixi run build
   pixi run test
   pixi run test-result
   ```

   The first `pixi run` downloads the environment (a few GB). `test-result`
   prints the summary; CI expects `0 errors, 0 failures` (a few tests skip
   themselves, see below).

3. The conda-forge `cppcheck.exe` cannot find its `std.cfg` (its build
   machine's path is baked in). If the `cppcheck` linter test errors with a
   missing configuration, copy the `cfg` directory that contains `std.cfg`
   (under `.pixi\envs\default`) next to `cppcheck.exe`, as the CI workflow
   does.

### Using the RMW in your own process

After `pixi run build`, `install\setup.bat` (or `setup.ps1`) puts the RMW on
`PATH` and the ament index. Select it with
`set RMW_IMPLEMENTATION=rmw_intraprocess_cpp`. Everything in one process
(composable nodes in one component container) then talks through it with no
network traffic at all. Only one copy of `rmw_intraprocess_cpp.dll` should be
on `PATH` (see "Two copies of the DLL" below).

## Portability rules (enforced on Linux, before Windows sees the code)

`scripts/check-portability.sh` runs in the local gate and checks every C/C++
file in this repository for:

| Rule | What it rejects |
|---|---|
| POSIX-HEADER | POSIX/Unix-only headers (`unistd.h`, `pthread.h`, `sys/*`, ...) |
| NET-HEADER, NET-API | any networking header or call (this RMW never opens a socket) |
| GCC-ONLY | GCC/Clang-only constructs (`__attribute__` without a macro, `__builtin_*`, `typeof`, `?:`) |
| VISIBILITY | libraries built without hidden default visibility; public headers without `__declspec` macros |

The rest of the checklist (CLAUDE.md, "Engineering rules"):

- C++17, standard library threading only (`std::thread`, `std::mutex`,
  `std::condition_variable`, `std::atomic`).
- Every exported symbol goes through `visibility_control.h`
  (`RMW_INTRAPROCESS_CPP_PUBLIC`). Linux builds use hidden visibility by
  default, so a missing macro fails there exactly as it would with MSVC.
  `scripts/in-container/check-symbols.sh` checks the exported set.
- No static-initialization-order dependencies across DLLs: the registry and the
  tap registry are function-local, intentionally leaked singletons; the
  identifier block is constant-initialized.
- Tests that link the static test library define `RMW_BUILDING_DLL` so the
  `rmw.h` declarations are not `dllimport` (otherwise MSVC looks for
  `__imp_rmw_*`).
- Text files use LF line endings (the linters compare bytes; CI sets
  `core.autocrlf false`).

## Known risks and Windows-specific behavior

- **Two copies of the DLL.** Windows loads two files with the same name from
  different directories as two separate modules. A client that loads
  `rmw_intraprocess_cpp.dll` by name could therefore get a second instance
  with its own registry and taps. The extension API avoids this: `find_api()`
  (`extension.hpp`) reaches the API through the identifier that the RMW in use
  returns, never by loading the library. Keep a single copy on `PATH` anyway.
- **Reference RMW on Windows.** `rmw_fastrtps_cpp` is the comparison RMW of
  the rclcpp-level tests. On Windows CI it once hung at shutdown in
  `test_composition_demo` (after an `rclcpp::shutdown()` from inside a
  callback); this RMW's instance of the same test passes. See
  `docs/unsupported.md`.
- **rclcpp/rcl defects found by the torture tests** apply to Windows as well
  (intra-process manager self-deadlock on concurrent publisher destruction,
  rcl's non-thread-safe entity init/fini per node, StaticSingleThreaded
  starvation, rclcpp_action exceptions after shutdown). They are listed with
  their workarounds in `docs/unsupported.md`. In short, for your own component
  container: create and destroy a node's entities from one thread, avoid
  intra-process comms for nodes that destroy publishers at run time, and stop
  action traffic before `rclcpp::shutdown()`.
- **Sanitizers.** ASan/UBSan and TSan run on Linux only (locally and in CI).
  Windows CI runs the release build of every test, including the torture suite.
- **Not verified by Claude.** All Windows evidence comes from the GitHub
  runner. A build on your own machine (M10 acceptance) is the remaining check.
