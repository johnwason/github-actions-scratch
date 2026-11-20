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
| M3 Client streaming and async | done locally, CI pending | Events, callbacks, wires, pipes, async ops, `rr.await`, nested-wait detection; `host_sim streaming` and the large-array check pass on both runtimes |
| M4 Server core | not started | |
| M5 Remaining parity | not started | |
| M6 CoppeliaSim slice | human gate | Install and licensing needed |
| M7 Tesseract integration | human gate | Confirm demand first |

Status values: `not started`, `in progress`, `done` (acceptance criteria verified), `blocked`,
`human gate`.

## Next

M0.6 needs the human to push `dev`, then a green CI run there; the scratch runs already showed the
`verify` steps pass on a clean `ubuntu-24.04` runner. Then confirm M0 acceptance and start M2.1.

## In progress

M3 (client streaming and async), started 2026-11-20 while the M2 sanitizer findings wait on the
core fix (M2 item 4 below). Done:
- Blocking calls are async calls plus the pump (`rrlua/async.hpp`, D9): property, function, objref,
  connect and close wait on the state's queue and run its tasks meanwhile, so a service can call
  back into the client during a call. `rr.sleep(seconds)` waits the same way.
- Events: `obj:addEventListener(name, fn)` returns an id, `obj:removeEventListener(id)`. Listeners
  run in the pump; a listener only gets events that arrived after it was added. A listener's error
  is raised by whatever runs the pump (`rr.step`, `rr.sleep` or a blocking call), with its own
  location.
- Callbacks: `obj:setCallback(name, fn)` (nil clears). The return value, or the Lua error, goes back
  to the service; TS1 `test_callbacks` passes, and the server rejects a wrong return value.
- Lua function references are tracked per stub and released on the Lua thread once the stub is
  closed or gone (`StateContext::sweep`, run by `rr.step`, `rr.sleep` and `close`).
- Async: `obj:asyncGetProperty(handler, name)`, `asyncSetProperty(handler, name, value)`,
  `asyncCallFunction(handler, name, ...)` and `node:asyncConnectService(handler, url)` return an op
  (`op:isDone()`, `op:result()`); the handler runs as `handler(value, err)` in the pump.
  `rr.await(op)` yields a coroutine, which `rr.step` resumes when the op completes, and blocks with
  the pump elsewhere (the main thread, or code that cannot yield). `rr.await` and `op:result` are
  a few lines of Lua embedded in the module: LuaJIT has no `lua_yieldk`, so a C function cannot
  raise the op's error after the coroutine resumes. Errors in awaiting coroutines or handlers are
  raised by the pump after every waiter and the handler have run.
- Wires: `obj:connectWire(name)` returns a connection with `getInValue`, `tryGetInValue` (ok,
  value), `getOutValue`, `tryGetOutValue`, `setOutValue`, `inValueValid`, `outValueValid`, in/out
  value lifespans in seconds, `direction`, `close`, and coalesced `addValueChangedListener(fn)`
  (fn(latest value); at most one notification queued per connection). `obj:peekWireInValue`,
  `peekWireOutValue` and `pokeWireOutValue` for unconnected access. Tested against TS1 `w1..w3`
  (server echo) and TS3 `peekwire`/`pokewire`.
- Pipes: `obj:connectPipe(name[, index])` returns an endpoint with `sendPacket` (returns the
  packet number), `receivePacket`, `tryReceivePacket`, `peekNextPacket`, `available`, `index`,
  `isUnreliable`, `direction`, `get/setRequestPacketAck`, `close`, and coalesced
  `addPacketReceivedListener(fn)` (fn() then drain). Tested against TS1 `p1`, `p2` and
  `pipe_check_error`. Values stay raw `MessageElement`s in the core until Lua reads them (D10).
- `host_sim streaming`: 6,000 host frames streaming a 1,000-double wire value every frame and a
  pipe packet every 4th, with value-changed and packet-received listeners and events, stepping
  only every 5th frame: queue size never above 3, RSS +0.3 MB after warm-up, 1,500/1,500 packets.
  A state closed while the service waits on its callback closes in 0.5 s (not the request timeout)
  and the service keeps working.
- Large arrays: 8 MB of doubles through a pipe and a wire, both directions, costs 0.036 s of
  process CPU and arrives as `rr.array` (no table conversion).
- Deadlock detection: `rr.step` inside a task raises (M2.1); blocking calls nested in handlers are
  limited to depth 8 and checked before anything is sent, so a callback that triggers itself
  fails with a clear error instead of recursing until the stack or the service's threads run out.
  Every other wait is bounded by the node's request timeout. Reentrant server handlers are M4.
M3 acceptance: the event/callback/wire/pipe tests, the `host_sim` long-running loop and the
large-array check pass on both runtimes; a full sanitizer CI run is next.

M2 status:
