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
`core-patches/`. They are applied on branch `rr-go-core-fixes` of the CI repository,
which CI checks out:

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

None of the patches change the C++ library ABI. `0001` only touches a `.cpp` file; its class is declared in
a private header and the exported symbols are identical. The Go bindings need `0002` and `0003` to build.
Drop the `[skip ci]` line, which is only there for the CI repository, from the commit messages when
upstreaming.

Still open:

- **MinGW/cgo**: Windows support requires `RobotRaconteurCore` to build with a cgo-compatible toolchain
  (MinGW-w64), or a C-ABI shim around an MSVC-built core.
