# Progress

Read `CLAUDE.md` first. Keep this file current: it is how the next session, possibly on another
machine, knows where things stand. Confirm what it says against `git log` and `scripts/verify.sh`.

## Status

**Last verified state (2026-11-20):** M0 is done: `bootstrap.sh && verify.sh` pass on clean
GitHub runners and locally, and the full scratch CI run 44702668545 is green. `verify.sh` passes
locally on Lua 5.4.9 and LuaJIT 2.1, 9 tests each.

| Task | Status | Notes |
|---|---|---|
| M0.1 Repo skeleton, license | done | Apache-2.0 text from RR core with the appendix placeholder; CMake configures once M0.2 provides Lua |
| M0.2 `bootstrap.sh` | done | All steps ran in the container (in two passes after fixes); stamps make reruns no-ops |
| M0.3 `verify.sh` | done | `--network none`; static checks (singleton, absolute paths), then both runtimes |
| M0.4 Trivial module, error guard | done | `tests/m0`; numbers under Measurements |
| M0.5 Sanitizer builds and teardown stress | done | ASan+UBSan+LSan and TSan clean on GitHub (scratch run 44697703442), 3 tests each; run with `scripts/remote-ci.sh` |
| M0.6 GitHub Actions CI | done | CI runs in the scratch repository; full run 44702668545 green (secret scan, verify, ASan, TSan) |
| M1 Numerics | removed | Dropped from scope 2026-11-11; see "Decisions and plan changes" |
| M2.1 Dispatch queue, `rr.step` | in progress | Nodes, default node, borrowed-node C++ API, per-state queue, `rr.step(budget)`; Lua tests pass on both runtimes. Teardown with live threads is tested in M2.2 `host_sim` |
| M2.2 `host_sim` harness | done, pending sanitizers | Scenarios `states`, `borrowed` and `connection` (states closed with live connections) pass. ASan leak in `connection` waits on the RR core fix |
| M2.3 `rr.array`, marshalling | done | Every type, including multidim arrays of pods and namedarrays, round trips through the real wire format and against the C++ test server (TS1, TS3, TS5) |
| M2.4 Connect, properties, functions, objrefs | done, pending sanitizers | TS1, TS3 and TS5 pass on both runtimes; full CI green except the two RR core sanitizer findings (see In progress) |
| M3 Client streaming and async | done, sanitizer CI pending core fix | Events, callbacks, wires, pipes, async ops, `rr.await`, nested-wait detection; `host_sim streaming` and the large-array check pass on both runtimes |
| M4 Server core | done, sanitizer CI pending core fix | Services with properties, functions, events, objref registry, wire broadcasters and receivers, pipes; Python client and `host_sim service` pass on both runtimes |
| M5 Remaining parity | done locally, CI pending | Generators, numeric memories, subscriptions, discovery, authentication; MATLAB coverage ported; TypeScript/Racket review done with gaps listed |
| M6 CoppeliaSim slice | human gate | Install and licensing needed |
| M7 Tesseract integration | human gate | Confirm demand first |

Status values: `not started`, `in progress`, `done` (acceptance criteria verified), `blocked`,
`human gate`.

## Next

M0.6 needs the human to push `dev`, then a green CI run there; the scratch runs already showed the
`verify` steps pass on a clean `ubuntu-24.04` runner. Then confirm M0 acceptance and start M2.1.

## In progress

Hardening (2026-11-26), from a review of the binding against the fix classes in robotraconteur
PR 494 (now the pinned core):
- Remote-driven work is bounded: once 1,024 tasks are queued, a request to a Lua service and a
  generator Next are refused with a "busy" `ServiceException`, and an incoming event is dropped
  (`DispatchQueue::kMaxRemoteTasks`, `full()`, `rr._queueRefused()` for tests). The script's own
  async completions and callback requests (bounded by their waiting RR threads) are not capped,
  so nothing the script waits for is lost. Tested with 1,500 requests and 2,000 events while the
  state does not step: exactly 1,024 answered or delivered, the rest refused or dropped, and the
  service works afterwards. The same cap RR core now applies to the MATLAB binding's queues.
- Nesting limits: packing and unpacking stop at 512 levels (RR core's message reader limit) with
  a `DataTypeError`, so a table that contains itself no longer overflows the C stack; building
  defaults (`newStructure`) stops at 64. RR core 1.3.0 now rejects recursive pods in definitions
  pulled at connect (also when the client registered the type itself), so a remote service can
  no longer reach the defaults path with one; reproduced before the pin move as a segfault.
- Received array shapes: dims and element counts from the wire, from service definitions and
  from memory reads are checked in C++ (`array_numel_checked`) before `push_array`, so a bad
  shape is a `DataTypeError` instead of signed overflow or a Lua error raised under C++ frames;
  `unpack` also rejects a missing element.

