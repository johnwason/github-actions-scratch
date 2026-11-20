# Progress

Read `CLAUDE.md` first. Keep this file current: it is how the next session, possibly on another
machine, knows where things stand. Confirm what it says against `git log` and `scripts/verify.sh`.

## Status

**Last verified state (2026-11-11):** `scripts/bootstrap.sh && scripts/verify.sh` pass on x86_64:
Lua 5.4.9 and LuaJIT 2.1, 3 tests each (M0 pass-through, M0 throw memory, teardown stress).
M0 acceptance still needs a single-pass bootstrap on a clean clone and green CI after the human
pushes.

| Task | Status | Notes |
|---|---|---|
| M0.1 Repo skeleton, license | done | Apache-2.0 text from RR core with the appendix placeholder; CMake configures once M0.2 provides Lua |
| M0.2 `bootstrap.sh` | done | All steps ran in the container (in two passes after fixes); stamps make reruns no-ops |
| M0.3 `verify.sh` | done | `--network none`; static checks (singleton, absolute paths), then both runtimes |
| M0.4 Trivial module, error guard | done | `tests/m0`; numbers under Measurements |
| M0.5 Sanitizer builds and teardown stress | in progress | ASan+UBSan+LSan clean locally after the `-z nodelete` fix; TSan moved to GitHub (`scripts/remote-sanitizers.sh`) |
| M0.6 GitHub Actions CI | in progress | `ci.yml` written; YAML parses; not run until the human pushes |
| M1 Numerics | removed | Dropped from scope 2026-11-11; see "Decisions and plan changes" |
| M2.1 Dispatch queue, `rr.step` | not started | |
| M2.2 `host_sim` harness | not started | |
| M2.3 `rr.array`, marshalling | not started | |
| M2.4 Connect, properties, functions, objrefs | not started | |
| M3 Client streaming and async | not started | |
| M4 Server core | not started | |
| M5 Remaining parity | not started | |
| M6 CoppeliaSim slice | human gate | Install and licensing needed |
| M7 Tesseract integration | human gate | Confirm demand first |

Status values: `not started`, `in progress`, `done` (acceptance criteria verified), `blocked`,
`human gate`.

## Next

Finish M0.5 with `scripts/remote-sanitizers.sh` (TSan and ASan on GitHub). Then M0.6 once CI has run
on `dev`, and confirm M0 acceptance from a clean clone. Then M2.1.

## In progress

None.

## Blocked / needs a human

None.

## Decisions and plan changes

Record the date, the evidence, and what changed.

- 2026-11-20: Sanitizers run on GitHub, never locally, at the project owner's direction ("do not
  attempt tsan locally, use johnwason/github-actions-scratch on github with orphan branches"). Local
  TSan aborted at startup with `FATAL: ThreadSanitizer: unexpected memory mapping` (GCC 13 TSan vs.
  32-bit mmap ASLR entropy); `setarch -R` in the container was refused by podman's default seccomp
  profile, and relaxing that profile or using host `sudo` is off limits. Added
  `scripts/remote-sanitizers.sh` and `.github/scratch/sanitizers.yml`, the push exception in
  `CLAUDE.md`, and a `sysctl vm.mmap_rnd_bits=28` step to `ci.yml`'s sanitizer job.

- 2026-11-11: Modules are linked with `-z nodelete` (hard rule "SWIG" updated). Evidence: the M0.5
  ASan run of `teardown_stress` hung for 30 minutes with one thread spinning in
  `SWIG_PropagateClientData` inside `luaopen_rrlua_m0`. Lua `dlclose`s a C module when the state
  that loaded it closes, so "load once on one thread first" did not hold: each later `require`
  reloaded the module and reran SWIG's unlocked init concurrently, corrupting its cast list into a
  cycle. `teardown_stress` now fails if the module is unmapped after its loading state closes
  (mutation-checked: fails without the flag, passes with it), and has a 600 s timeout.

- 2026-11-11: Numerics and Eigen dropped from scope at the project owner's direction ("a mistake to
  include eigen in the instructions"). Removed Goal 3, D11, the Eigen hard rule, milestone M1, the
  Eigen pin and `libeigen3-dev`; D12 keeps only its Tesseract half; M2.3 no longer shares a
  representation with `lua_eigen`. `rr.array` is the only array type. Code written for M1.1–M1.3
  this session was reverted in `eb084db` (history kept). Decision and milestone numbers are unchanged.

- 2026-11-11: Added `libgtest-dev` to the container. Evidence: RR core's `test/` tree, which holds
  the C++ test server, fails to configure without it (`GTest::Main` target not found from
  `test/cmake/macros.cmake`). Also added build tooling to the image: `git make python3-venv binutils
  file`. `CLAUDE.md` updated. numpy, pulled in by the Python `robotraconteur` wheel, is pinned in
  `scripts/pins.sh` for reproducibility.
- 2026-11-11: Pins chosen in M0: Lua 5.4.9 (sha256 `2335b6c5…` from lua.org, checked on download),
  LuaJIT `v2.1` at `63c8f1a9b479ba5518cf6a170ff7729178bc3cfd`, lua-compat-5.3 at
  `2d3c70b95bf076816d974664d62070abe7c6e7fc`, numpy 2.5.4. All in `scripts/pins.sh`.

- 2026-11-11: Sessions no longer push. At the project owner's direction, they commit to the local
  `dev` branch to track work, and the owner reviews and pushes. The session protocol, the boundaries,
  and the M0 acceptance wording for CI were updated to match.
- 2026-11-11: The RR core pin moved from tag `v1.2.9` to `master` commit
  `7cd1610c519613f241b25453754fb118b270cdaf` at the project owner's direction, to use the shared test vector blob
  (#489: `test/data/test_vectors.bin`). The C++ test server and RR's own service tests now take their
  expected values from it, as do the Racket client's ServiceTest ports, so the M2 acceptance tests read
  it too. The other two commits after the tag change service definition parsing (keeping documentation
  when sending definitions) and node directories; `RobotRaconteurMex.cpp` is unchanged, so its line
  references still hold. The Python peer stays at release 1.2.9.
- 2026-11-11: The project owner asked that sessions also consider
  `johnwason/robotraconteur_typescript`, `johnwason/robotraconteur_typescript_node`,
  `johnwason/robotraconteur_racket`, and local `~/RobotRaconteur` checkouts when present. Added "Other
  reference implementations" to `CLAUDE.md` with pinned commits, what to use each for, and read-only
  rules; `bootstrap.sh` (M0.2) now clones them into `.deps/refs/`; M5 acceptance now includes reviewing
  their test cases. They are references only; Python stays the test peer. Local checkouts may be under
  `~/RobotRaconteur2` instead; both names are checked.
- 2026-11-08: D8 changed from one node per process to multiple independent nodes. The project owner
  confirmed the single node is only a MATLAB convenience. The core supports it directly (constructor
  plus `Init()`), and RR's own C++ tests run several nodes per process.
- 2026-11-08: D1 and D5 changed. SWIG is now used for fixed-signature wrappers only, with new `.i`
  files; `LuaBridge3` and `sol2` are rejected. Evidence is under "Measurements". Added the podman rule,
  `Containerfile`, and M0.5 sanitizer builds.
- 2026-11-08: RR data now passes through SWIG as raw stack references (`rl::Arg`, `rl::Ret`), following
  the Python binding's raw `PyObject*` approach at the project owner's direction. `%native` is limited
  to true varargs functions.
- 2026-11-08: Added a `pre-commit` hook running gitleaks because the repository is public. It runs on
  the host as approved local tooling, as an exception to the container rule.
- 2026-11-08: The project owner chose CI only on GitHub Actions; no agent runs there. Added M0.6 and
  made `LICENSE` the first task.

## Measurements

M0.4, 2026-11-11, x86_64 container, Release (`-O3`): 50,000 C++ throws with a 4 KB string argument.

| Path | Lua 5.4.9 (built as C) | LuaJIT 2.1 |
|---|---|---|
| SWIG method (`const std::string&` argument) | 4364 → 4364 KB | 4288 → 4292 KB |
| `%native` through `rl::guard` | 4364 → 4364 KB | 4292 → 4292 KB |

The first `%exception` handler called `lua_error` inside SWIG's argument block and leaked the
`std::string` temporary: 12 → 213 MB on Lua 5.4. It now pushes the message and uses `SWIG_fail`, so
SWIG closes the block before `lua_error`. This also shows the memory test can fail.

From the original feasibility study, on x86_64 Ubuntu with Eigen 3.4 and stripped `-O2` builds
(the Eigen figures are historical; numerics were later dropped from scope):

- Lua 5.4 shared library: 265 KB.
- Eigen kernels with no Lua wrappers: 39 KB for basic and spatial operations, 264 KB for
  decompositions, 285 KB combined.
- Python: `libpython3.12` 9 MB, untrimmed standard library 55 MB, NumPy 36 MB.

Binding comparison, 2026-11-08: the same small C++ class wrapped with SWIG 4.2 and `LuaBridge3` master,
on x86_64 in `ubuntu:24.04`, with Lua 5.4.7 built as C and LuaJIT 2.1. This covered one class and one
error path, not the real RR surface.

| | SWIG 4.2 | `LuaBridge3` |
|---|---|---|
| RSS after 50,000 C++ throws with a 4 KB argument, Lua 5.4 | 4.5 → 4.5 MB | 4.6 → 422 MB |
| Same, LuaJIT | 4.6 → 4.8 MB | 4.4 → 4.6 MB |
| Stripped module size, LuaJIT build | 53 KB | 81 KB |
| Stripped module size, Lua 5.4 build | not captured | 77 KB |
| Time per method call | 73–78 ns | 62–74 ns |

The `LuaBridge3` leak was the same with and without `enableExceptions`. It is about 8.3 KB per throw,
consistent with the 4 KB argument and 4 KB exception message not being destroyed (inferred, not traced).

Raw pass-through test, 2026-11-08, same environment, using the `rl::Arg` and `rl::Ret` typemaps: tables,
functions and `nil` passed through intact; multiple returns worked; wrong argument counts raised Lua
errors. RSS stayed flat over 50,000 throws (Lua 5.4: 4.3 → 4.6 MB; LuaJIT: 4.5 → 4.9 MB). 66–69 ns per
call. 54 KB stripped.

Maintenance as of 2026-11-08: `sol2` last commit March 2025; `LuaBridge3` last commit October 2026.

## Notes for later milestones

- Local checkouts found in `~/RobotRaconteur2`: `robotraconteur` at the pinned commit,
  `robotraconteur_racket` at its pin, `robotraconteur_standards` at `e202c8d7` (branch
  `docstring-convention`), `robotraconteur-docstring-fields` at `762397ca`. Only read; this session
  used `.deps/` for builds.
- Lessons from the Racket generation log: confirm verifiers can fail (mutation checks); the pipes
  test must not assume ack order; an event fired late can arrive on the next test's connection;
  Python's `TestMonitorLock` cannot fail as written, so assert the intended behavior instead.
- UseSWIG does not track `%include`d `.i` files unless `SWIG_USE_SWIG_DEPENDENCIES` is on; every SWIG
  target must set it, or edits to `rrlua_common.i` silently do not regenerate wrappers.
- Headers that SWIG-generated code includes must use `rrlua/lua_api.hpp`, not `rrlua/lua.hpp`:
  SWIG's runtime defines its own 5.1 compat macros, which clash with compat-5.3's.
- Sanitizer builds need about 3 GB per compile job; `sanitizer-steps.sh` caps jobs by memory.

## Ideas

Out of scope for now. Do not act on these without a plan change.

- Transparent `__index` proxy written in pure Lua on top of the explicit API.
- LuaJIT FFI paths for very fast array access.
- Use the TypeScript (Node.js) service and the Racket client as extra interoperability peers in CI.

## Session log

Newest first. Each entry: date, machine-independent summary, what was verified and how.

- 2026-11-11: M0.1–M0.4 done; M0.5 and M0.6 written. Verified with `scripts/bootstrap.sh` then
  `scripts/verify.sh` in the container on x86_64 (both runtimes, 3 tests each, all passing). The M0.4
  memory test failed against the first `%exception` handler (a real leak) and passed after the fix.
  numerics (M1.1–M1.3) were written, then reverted when the project owner dropped them from scope;
  `verify.sh` was rerun after the removal and passes. An empty stray file `oom`, committed by
  mistake in `b04fec6`, was removed.

- 2026-11-11: Plan updated to add the TypeScript, TypeScript Node and Racket implementations and local
  `~/RobotRaconteur` checkouts as references. Pinned commits are the heads of each repository's default
  branch on that date. Documentation only; nothing built or run.
- 2026-11-08: Plan created from a feasibility discussion. No code written, nothing run.
