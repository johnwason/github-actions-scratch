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
- **`LoggingGo.i`**: `rrgo::SetFileLogRecordHandler`. A helper is needed
  because `SWIG/Logging.i` declares `FileLogRecordHandler` without its
  `LogRecordHandler` base.
- **`GoTypemaps.i`, `GoExceptionTypemaps.i`, `rrgo_support.h`**: typemaps,
  and the C++ exception to Go `error` plumbing (a thread-local pending error).
- **`RRGoTestSupport.i`**: error-path test hooks only. Drop it or keep it
  behind a test option.

## 3. Suggested upstream core changes

- **`SWIG/Logging.i`**: declare `class FileLogRecordHandler : public LogRecordHandler`. This also fixes the C# and Java bindings, and makes `LoggingGo.i` unnecessary.
- **`SWIG/ServiceSecurity.i`**: expose an `AuthenticatedUser` constructor, or a factory, to allow user authenticators written in Go (and other director languages).
- **MinGW/cgo**: Windows support requires `RobotRaconteurCore` to build with a cgo-compatible toolchain (MinGW-w64), or a C-ABI shim around an MSVC-built core.
- **Connector self-deadlock (core bug, affects every language).** `RobotRaconteurNode_connector::connect`
  (`RobotRaconteurNode_connector.cpp:420-428`) holds `active_lock` while it calls
  `Transport::AsyncCreateTransportConnection`. `LocalTransport` connects synchronously. If
  `ASIOStreamBaseTransport::AsyncAttachStream` then throws, its outer `catch`
  (`ASIOStreamBaseTransport.cpp:171`) calls the handler synchronously with
  `InvokeHandlerWithException`. The handler reaches `connected_transport` → `handle_error`, which locks
  `active_lock` again on the same thread, and the thread blocks forever. Node shutdown then hangs in
  `ThreadPool::Shutdown` joining that thread.

  It was seen when a client connects over `rr+local` to a node that is shutting down: two test
  processes discovering each other through the local transport hit it in about half of the runs.
  Either change fixes it:
  - `PostHandlerWithException` in that `catch`;
  - releasing `active_lock` before calling `AsyncCreateTransportConnection`.

  The tests in this repository run the C++ interop packages one at a time to avoid it.
