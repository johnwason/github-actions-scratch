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
| M4 Server core | done locally, CI pending | Services with properties, functions, events, objref registry, wire broadcasters and receivers, pipes; Python client and `host_sim service` pass on both runtimes |
| M5 Remaining parity | not started | |
| M6 CoppeliaSim slice | human gate | Install and licensing needed |
| M7 Tesseract integration | human gate | Confirm demand first |

Status values: `not started`, `in progress`, `done` (acceptance criteria verified), `blocked`,
`human gate`.

## Next

M0.6 needs the human to push `dev`, then a green CI run there; the scratch runs already showed the
`verify` steps pass on a clean `ubuntu-24.04` runner. Then confirm M0 acceptance and start M2.1.

## In progress

M4 (server core), started 2026-11-20. Done (M4.1):
- `node:registerServiceType(text)` (returns the service name), `node:startTcpServer(port)` (0
  picks a port; returns the listening port; owned nodes only, a host starts the server transports
  of nodes it lends), `node:registerService(name, type, handlers)` with a handler table
  `{getProperty(name), setProperty(name, value), callFunction(name, ...)}`. Requests are queued to
  the state and answered after its pump runs the handler (`ServiceSkel::EndAsyncCall*`, the MEX
  model); an RR thread never waits for Lua. A Lua error reaches the client as
  `RobotRaconteur.OperationFailed` with the message. A request queued when the state closes is
  answered with an error.
- `svc:fireEvent(name, ...)`, and the objref registry: `svc:addObjRef(name, [index,] type,
  handlers)` declares a child object served by `GetSubObj` without calling Lua.
- `svc:close()`; services registered on borrowed nodes are closed when the state closes.
- `tests/server/test_server.lua`: a Lua client on a second node in the same state, over TCP,
  exercises properties, functions, structs, errors, events and objrefs on both runtimes.
- Integer results from SWIG methods are now Lua integers (a typemap; SWIG pushed floats, so a port
  printed as `22231.0`).
Done (M4.2): `svc:wireBroadcaster(name)` (`setOutValue`; the latest value is kept until a
child object's wire server exists, which is when a client first opens that objref),
`svc:wireReceiver(name)` (`getInValue`, `tryGetInValue`, `setInValueLifespan`), one mode per wire;
`svc:pipe(name)` (`sendPacket` to every connected endpoint, returns the count; `receivePacket`,
`tryReceivePacket`, `available` drain packets kept in each endpoint's own RR queue;
`endpointCount`). RR core finding: a broadcaster's initial value is sent before the connect reply
and every RR client drops it (`docs/upstream-proposals/wire-initial-value-before-connect-reply.md`);
the Lua client works around it by peeking once after connecting.
Done (M4.3): `tests/server/test_python_client.lua` serves the test service from Lua while RR
core's Python client (the pinned peer, `RRLUA_PYTHON`) checks properties, functions, errors,
events, objrefs, wire broadcaster and receiver, and pipes, on both runtimes. `host_sim service`:
a Lua service on a host-owned node and a Lua client on its own node over TCP; the service's state
is closed with the client connected (the client's next call fails cleanly) and recreated three
times (the client reconnects and calls), then the client's state is closed and another client's
node is shut down while connected; the service keeps serving new clients.
M4 acceptance: every item passes locally on both runtimes; a full sanitizer CI run is next
(expected to show only the RR core findings).

