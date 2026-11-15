# Windows (B13)

**Status:** the bridge's CI has a Windows job (`windows-2022`, MSVC 2022,
RoboStack Jazzy; `ci/github/workflows/robotraconteur_ros2_debug_bridge.yml`,
environment `ci/pixi.toml`). It builds Robot Raconteur master and the
bridge, then runs every bridge test under both RMWs. The user confirms
the MSVC build and the Python client test on a real Windows machine
(PLAN.md B13). Facts not yet confirmed on Windows are marked *to confirm*.

## Building against the ROS Windows environment

Use the same environment as `rmw_intraprocess`: ROS 2 Jazzy from RoboStack
through pixi, MSVC 2022. See `../rmw_intraprocess/docs/windows.md` for
setting it up, the short-path rule (`MAX_PATH`; build in `C:\b`, install in
`C:\i`) and the cppcheck fix.

1. **Workspace.** Lay out a colcon workspace:

   ```
   C:\ws\
     pixi.toml                        (rmw_intraprocess's ci\pixi.toml plus the packages below)
     src\robotraconteur\              (github.com/robotraconteur/robotraconteur, master)
     src\rmw_intraprocess\
     src\robotraconteur_ros2_debug_bridge\
   ```

2. **Environment.** Copy this repo's `ci\pixi.toml` to `C:\ws\pixi.toml`.
   It adds Robot Raconteur's dependencies to RoboStack: `libboost-devel`
   (date_time, filesystem, regex, chrono, atomic, thread, random,
   program_options), `swig` and `numpy`. It also adds the ROS packages the
   tests use.

3. **Build and test**, as the CI does (from PowerShell; pixi activates
   MSVC):

   ```powershell
   cd C:\ws
   $env:CI_BUILD_BASE = "C:/b"; $env:CI_INSTALL_BASE = "C:/i"
   $env:UPSTREAM_BRIDGE_DIR = "C:/upstream/robotraconteur_ros2_bridge"  # a clone, for the compatibility tests
   $env:AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS = "1"
   pixi run build-robotraconteur   # Robot Raconteur, ROS mode, no tests
   pixi run build                  # rmw_intraprocess_cpp and the bridge
   pixi run test
   pixi run test-result
   ```

   Robot Raconteur's Python install drops the drive letter of the install
   base: the module lands in `\i\lib\python3.x\site-packages` of the
   current drive. Add that directory to `PYTHONPATH` before `pixi run test`,
   as the CI does.

   Robot Raconteur's ROS mode (`-DROBOTRACONTEUR_ROS=ON`, as in
   `container/Containerfile`) builds the core as a static library
   (`BUILD_SHARED_LIBS` off, its default), RobotRaconteurGen, and the
   Python 3 module the tests use.

## Boost and the C runtime

The aim is that nothing of Robot Raconteur or Boost is visible to the rest
of the process.

- **Robot Raconteur core: static, inside the component DLL.**
  `robotraconteur_ros2_debug_bridge.dll` links `RobotRaconteurCore`
  privately. A DLL exports only what is marked `__declspec(dllexport)`:
  here, `make_bridge_node()` and the class_loader factory. So another Robot
  Raconteur copy in the process (another component, a Python client
  through the RR Python module) cannot share its globals. On Linux,
  `-Wl,--exclude-libs,ALL` does the same, and `test_symbols` checks it.
- **Public headers carry no Robot Raconteur or Boost types.**
  `scripts/in-container/check-public-headers.sh` enforces this in the
  gate. The public headers (`bridge.hpp`, `visibility_control.hpp`) use
  only rclcpp and the standard library.
- **Boost: one copy, from the environment.** Robot Raconteur's CMake uses
  Boost's default link mode with `Boost_USE_STATIC_RUNTIME OFF`. With
  conda-forge's Boost that means the `boost_*.dll` of the environment
  (*to confirm* with the first build's `dumpbin /dependents`). That is
  safe as long as every Boost user in the process comes from the same
  environment, which is how RoboStack works. If another component brings
  its own Boost DLLs of a different version, link Boost statically instead
  (`-DBoost_USE_STATIC_LIBS=ON`, with `libboost-static` if available) so
  the component needs no Boost DLLs.
- **One C runtime: /MD release.** Everything is built with MSVC 2022 and
  the dynamic release CRT (`/MD`, the CMake default for Release), like
  RoboStack's packages. Don't mix Debug builds (`/MDd`) with RoboStack's
  release DLLs: the bridge passes `std::string` and `rclcpp` objects
  across DLL boundaries.
- **TLS.** On Windows, Robot Raconteur uses SChannel (the Windows TLS
  library), not OpenSSL, unless built with `ROBOTRACONTEUR_USE_OPENSSL`.
  No OpenSSL DLL is needed.

## DLLs that get deployed

From this repo (install space):

| File | What it is |
|---|---|
| `bin\robotraconteur_ros2_debug_bridge.dll` | the component (rclcpp_components) and `make_bridge_node()`; Robot Raconteur inside |
| `lib\robotraconteur_ros2_debug_bridge\debug_bridge.exe` | the standalone bridge |
| `lib\robotraconteur_ros2_debug_bridge\generate_robdef.exe` | writes definitions for ROS types |
| `lib\robotraconteur_ros2_debug_bridge\robotraconteur_ros2_debug_cli.py` | the debug CLI |
| `share\robotraconteur_ros2_debug_bridge\config\default.yaml`, `robdef\` | default configuration, root definition |
| `share\ament_index\...` | component registration |

From the environment (*to confirm* with `dumpbin /dependents`):

- the `boost_*.dll` listed above (unless Boost is linked statically);
- the ROS DLLs the component uses: `rclcpp`, `rcl`, `rcutils`, `rmw`,
  `rmw_implementation`, the introspection type supports, `yaml-cpp`,
  `class_loader` and the message packages;
- the MSVC runtime: `vcruntime140.dll`, `msvcp140.dll` and the UCRT;
- Windows system DLLs: `ws2_32`, `crypt32`, `secur32`, `iphlpapi` and
  others (Robot Raconteur's transports and SChannel).

For Python clients: the `RobotRaconteur` Python module from the same build,
or the `robotraconteur` package from PyPI or conda-forge. Clients only need
the wire protocol, so the version need not match exactly.

## Tests on Windows

- **Stopping a process.** Python cannot send SIGINT to a single process on
  Windows. `debug_bridge` and the test hosts take `--stop-on-stdin-close`
  (`src/stdin_stop.hpp`): when stdin closes they shut down as on Ctrl+C.
  On Windows, `test/bridge_host.py` stops test hosts that way. On Linux it
  keeps sending SIGINT. `test_standalone` checks both ways on Linux. On
  Windows it skips the SIGINT case (user decision, B13).
- **test_symbols** reads the export table with `dumpbin /exports` on
  Windows (`nm -D` on Linux). It checks the same thing: no Robot
  Raconteur symbols, and `make_bridge_node` exported.
- The benchmark reports CPU use from `/proc`, which it skips (NaN) on
  Windows.
- LocalTransport works on Windows. Its per-user directory is under the
  user's application data instead of `/run/user` (*to confirm*).

## Windows-specific risks

- **DDS on Windows** is unreliable, which motivates this project. With
  `rmw_intraprocess_cpp` there is no DDS. With `rmw_fastrtps_cpp`, the
  composition shutdown hang seen in `rmw_intraprocess`'s Windows CI may
  appear here too.
- **Two copies of `rmw_intraprocess_cpp.dll`.** The fast path finds the tap
  API through the RMW in use, never by loading the DLL by name
  (`../rmw_intraprocess/docs/windows.md`).
- **Robot Raconteur patch.** The Windows CI job patches its Robot Raconteur
  clone with `ci/patches/*.patch` until master has the fix (a client bug
  that only the MSVC build shows; docs/upstream_issues.md). Build Robot
  Raconteur with the same patch on your own machine until then.
- **Paths.** The test configuration files and the password file are
  resolved relative to the configuration file. Use forward slashes or
  quoted backslashes in YAML.
