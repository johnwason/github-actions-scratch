# Merging the Go bindings upstream

This directory holds what is needed to add Go to the upstream
`robotraconteur` repository. Nothing here is applied automatically.
`scripts/check_upstream_merge.sh` (also run in CI) checks the generator
patch:

1. It applies the patch to a copy of upstream `RobotRaconteurGen`.
2. It adds `GoServiceLangGen` and builds the result.
3. It checks that `--lang=go` output is byte-identical to `test/golden/`.
4. It checks that `--lang=csharp` still works.

## 1. RobotRaconteurGen `--lang=go`

Steps:

1. Copy `gen/GoServiceLangGen.h` and `gen/GoServiceLangGen.cpp` to `RobotRaconteurGen/`.
2. Apply `RobotRaconteurGen-lang-go.patch` from the repository root with `git apply`.

The patch:

- **`RobotRaconteurGen.cpp`**:
  - includes `GoServiceLangGen.h`;
  - adds the `--go-import-prefix` and `--go-runtime-import` options;
  - adds a `lang == "go"` branch, which writes `<outdir>/<package>/<package>.go` per service.
- **`CMakeLists.txt`**: adds the Go sources to `RobotRaconteurGen_src`/`_header`.
- **`cmake/RobotRaconteurGenerateThunk.cmake`**: `ROBOTRACONTEUR_GENERATE_THUNK(... LANG go)` lists the generated `.go` file as an output.

`gen/main.cpp` (`RobotRaconteurGenGo`) is a stand-alone copy of the
upstream CLI, limited to the options thunk generation uses. It exists only
so this repository can build a generator without modifying upstream, and it
is dropped after the merge.

Upstream test: add a `test_gen_go` like `test_gen_csharp`, using
`test/golden/*.go.golden` (see `cmake/check_golden.cmake`).

## 2. `RobotRaconteurGo/` and `BUILD_GO`

The C# binding layout maps onto Go as follows:

| this repository | upstream |
|-----------------|----------|
| `swig/*.i`, `swig/rrgo_support.h`, `swig/postprocess_go.py` | `RobotRaconteurGo/` (next to `RobotRaconteurNET/`) |
| `cmake/gen_go_errors.py`, `cmake/gen_cgo_flags.py`, `cmake/GoLinkFlags.cmake` | `RobotRaconteurGo/cmake/` |
| `CMakeLists.txt` (SWIG module, `RobotRaconteurGoNative`, error/constant generation) | `RobotRaconteurGo/CMakeLists.txt`, added by `option(BUILD_GO ...)` in the top-level `CMakeLists.txt` |
| `robotraconteur/` (Go runtime) | stays a separate Go module repository (`github.com/robotraconteur/robotraconteur_go`), with the SWIG output pregenerated for releases (as `USE_PREGENERATED_SOURCE`) |

When built upstream, `RobotRaconteurCore` and `RobotRaconteurWrapped.cpp`
come from the same tree. The out-of-tree core build in `scripts/build.sh` is
then no longer needed.

### Shared SWIG files

The upstream `SWIG/*.i` files are used unmodified. Go-specific behaviour is
added only by files in `swig/`:

- **`go_shared_ptr.i`**: SWIG's Go module has no `boost_shared_ptr.i`. This
  file provides `%shared_ptr` and `%rr_intrusive_ptr`.
- **`TimerGo.i`, `TimeSpecGo.i`**: ports of `TimerNET.i` and `TimeSpecNET.i`,
  without the C# modifiers.
- **`GoTypemaps.i`, `GoExceptionTypemaps.i`, `rrgo_support.h`**: typemaps,
  and the C++ exception to Go `error` plumbing (a thread-local pending error).
- **`RRGoTestSupport.i`**: error-path test hooks only. Drop it or keep it
  behind a test option.

## 3. Required upstream core changes

The bindings are built against upstream `7cd1610c` plus the patches in
`core-patches/`. They are on branch
[`go-bindings-fixes`](https://github.com/johnwason/robotraconteur/tree/go-bindings-fixes)
of the johnwason/robotraconteur fork, which CI checks out:

- **`0001`: connector self-deadlock (core bug, affects every language).**
  `RobotRaconteurNode_connector::connect2` held `active_lock` while it called
  `Transport::AsyncCreateTransportConnection`. `LocalTransport` connects synchronously. If
  `ASIOStreamBaseTransport::AsyncAttachStream` throws, the handler is invoked synchronously
  (`ASIOStreamBaseTransport.cpp:171`), and `connected_transport` → `handle_error` locks
  `active_lock` again on the same thread. Node shutdown then hangs in `ThreadPool::Shutdown`.

  It shows up when a client connects over `rr+local` to a node that is shutting down: two test
  processes discovering each other hung in about half of the runs. The patch registers the
  attempt and releases the lock before calling the transport. `scripts/debug_hang.sh`
  reproduces the bug (0 hangs in 12 runs with the patch).
- **`0002`: SWIG.**
  - Declares the `AuthenticatedUser` constructor in `Service.i`, so wrapped languages can implement
    user authenticators. Go uses it for `rr.UserAuthenticatorFunc`.
  - Declares `FileLogRecordHandler : public LogRecordHandler` in `Logging.i`, so it can be passed
    to `SetLogRecordHandler`.

- **`0003`: SWIG binding (`RobotRaconteurWrapped`).** `WrappedServiceSkelAsyncAdapter` captures the client
  endpoint on the request thread, and `RegisterGeneratorServer` gets an overload taking the adapter. Go
  service members run on their own goroutine and complete requests through the adapter, so a generator
  function can register its generator from that goroutine. This is binding code only.

- **`0004`: SWIG binding (`RobotRaconteurWrapped`).** It releases the director of a pipe endpoint or wire
  connection after its closed callback. That covers closes by the peer and by a disconnect, not only
  `Close()`. It also adds `Close()` to `WrappedWireSubscription`/`WrappedPipeSubscription` that releases their
  director. Without these the director keeps the wrapping language's object reachable forever. The C#
  and Java bindings leak those objects in the same way.

- **`0005`-`0009`: build the core with MinGW-w64 (Windows Go bindings).** cgo needs a GCC-compatible
  toolchain on Windows, so the core must build with MinGW-w64. Every change is inside
  `ROBOTRACONTEUR_WINDOWS`/`WIN32`/`MINGW` blocks or Windows-only sources:
  - `0005`: lowercase Windows SDK header names (`winsock2.h`, `windows.h`, ...). MinGW-w64 installs them
    in lowercase; MSVC is case-insensitive.
  - `0006`: `LocalTransport_win`. The unused `HandleTo*Stream` helpers build `std::fstream` from a `FILE*`,
    an MSVC-only extension; they are now compiled only with MSVC. The old MinGW workaround that redeclared
    `ConvertStringSecurityDescriptorToSecurityDescriptor` now applies only when `sddl.h` lacks it.
  - `0007`: `TlsSchannelStreamAdapter.h`. Drops `typedef typename` on non-dependent names (MSVC
    permissive mode only), and defines two member templates after the class they call is complete.
  - `0008`: `HardwareTransport_win`. **Fixes a handle leak with MSVC too**: two
    `SetupDiGetClassDevsW` results were wrapped as `RR_SHARED_PTR<void>(deviceInfo_sp, f->SetupDiDestroyDeviceInfoList)`,
    passing the empty `shared_ptr` instead of the handle. MSVC converts the function pointer to `void*` and
    picks the aliasing constructor, so the device info list was never destroyed. Also `BTH_ADDR*` for
    `PBTH_ADDR`, which MinGW-w64 lacks.
  - `0009`: CMake. With MinGW, link the system import libraries by their MinGW names (`ws2_32`, `mswsock`,
    `iphlpapi`, `bcrypt`, `crypt32`, `secur32`, plus `advapi32`, `user32`, `shell32` and `uuid`, which MSVC
    links by default). Checked against the symbols the static core needs. MSVC lists are unchanged.

  TLS uses SChannel, as with MSVC. The Linux build is unaffected: for the shared sources touched by
  `0005`, the preprocessed Linux output is identical before and after.

None of the patches change the C++ library ABI. `0001` only touches a `.cpp` file; its class is declared in
a private header and the exported symbols are identical. The Go bindings need `0002` and `0003` to build, and `0004` to release closed objects.
Windows builds need `0005`-`0009`.

Still open:

- **Native Windows build**: CI cross-builds from Linux. A native MSYS2 (MINGW64) build is expected to
  work but is not tested.
- **macOS**: not attempted.
