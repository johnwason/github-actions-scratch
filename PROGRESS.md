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

M5 (remaining parity), started 2026-11-20. Done (M5.1, generators):
- Client: `obj:callGenerator(name, ...)` returns a generator with `next([v])` (raises
  `RobotRaconteur.StopIteration` at the end), `tryNext([v])` (`true, value` or `false`),
  `nextAll()`, `close()`, `abort()`; all wait with the pump. Tested against TS3 `gen_func1`,
  `gen_func2`, `gen_func4` (abort, close, server-side stop after 8) as `ServiceTestClient2.py` does.
- Server: a `callFunction` handler for a generator member returns a Lua generator: a function
  (next) or a table `{next(self, v), close(self), abort(self)}`. Each Next/Close/Abort is queued
  and answered after the pump runs it (`GeneratorServerBase::EndAsyncCallNext`); it ends with
  `error(rr.StopIteration)`. The Lua generator is released once it finishes or RR drops it. Tested
  from the Lua client and RR's Python client.
Done (M5.2, memories, numeric element types including complex and bool):
- Client: `obj:getMemory(name)`; service: `svc:memory(name, initial)` copies an `rr.array`
  (dims for a multidim memory) into a native RR memory that the core serves without Lua (D10).
  One Lua handle for both: `type()`, `length()`, `dims()`, `read(pos[, count])` and
  `read({pos...}, {count...})` (return a new `rr.array`), `write(pos, data)` (an `rr.array`, or
  a table for array memories). Positions are 1-based. Locking rule: every access, from Lua or RR
  threads, goes through RR's memory classes, which hold the memory's mutex for each read and
  write; Lua never keeps a pointer into the memory. A client memory read or write is a single
  blocking RR request (no pump; it never needs Lua on the other side).
- Tested: TS1 `m1` against the test vectors as `ServiceTestClient.py` does, `m2` (5-D) and `m3`
  (1 MB uint8) by write and read back; a Lua service's memories from the Lua and Python clients.
- Gap: pod and namedarray memories (TS3 `pod_m1/2`, `namedarray_m1/2`) raise "not supported
  yet"; RR's typed pod memory clients need generated types (MEX has its own raw clients).
Done (M5.3, subscriptions and discovery):
- `node:subscribeService(url or {urls})`, `node:subscribeServiceByType(type or {types})`;
  a subscription has `getConnectedClients()` (client handles), `getDefaultClient()`,
  `getDefaultClientWait(timeout)` (pump), `subscribeWire(name[, path])`,
  `subscribePipe(name[, path])`, `close()`. Wire subscriptions: `getInValue`, `tryGetInValue`,
  `activeWireConnectionCount`, `setInValueLifespan`, `setOutValueAll`, `close`; pipe
  subscriptions: `receivePacket`, `tryReceivePacket`, `available`, `activePipeEndpointCount`,
  `sendPacketAll`, `close`. They use RR's own subscription classes over the stubs' raw wire and
  pipe clients; values are converted on read with the member type of the delivering connection.
  `setOutValueAll`/`sendPacketAll` need a root member (the type comes from a connected client).
- `node:findServiceByType(type[, timeout])` returns `{name, nodeName, nodeID, rootObjectType,
  urls}` tables. Owned nodes listen for discovery; `startTcpServer` announces (node-, link- and
  site-local, as RR's node setup does).
- Tested: URL subscription against TS1 (default client, wire echo, pipe echo); discovery and
  `subscribeServiceByType` against a Lua service. Discovery needs a multicast interface: under
  `--network none` (verify.sh, CI) only loopback exists and the check skips itself with a message;
  it passed on both runtimes with the container's default network (2026-11-20).
Done (M5.4, authentication):
- Client: `node:connectService(url, {username=, credentials={password=...}})` and the same options
  as the third argument of `asyncConnectService` (credential values: strings or numbers).
- Service: `node:registerService(name, type, handlers, {users=<password file text>,
  policies={...}})` (RR's `PasswordFileUserAuthenticator` and `ServiceSecurityPolicy`;
  `requirevaliduser` defaults to true). `rr.passwordHash(pw)` gives the MD5 hash the password
  file format uses.
- Tested: TS1 `RobotRaconteurTestService_auth` (two users, a wrong password, no credentials) as
  `ServiceTestClient.py` does, and a Lua service with a password file.
Done (M5.5, MATLAB coverage port): reviewed `test/matlab/RobotRaconteurMatlabTest.m`,
`RobotRaconteurMatlabTest2.m`, `RobotRaconteurMatlabEmptyArrayTest.m` and the MATLAB service test
client. Already covered by TS1/TS3 and the server tests: every TS1 property type, structs,
functions, TS3 bools, complex, enums, pods, namedarrays, `teststruct3`, generators, events,
service properties/functions/structs. Ported now: TS1 varvalue properties (`var_num`, `var_str`,
`var_struct`, mutation-checked) and `nulltest`; the empty-array checks (`d2`, `i32_2`, `str1`,
`list_d1`, TS3 `testnamedarray3` marshal without a DataTypeError); TS3 bool memories `c_m5`/`c_m6`.
Not ported (gap): `pod_m1`, `pod_m2`, `namedarray_m1` (pod and namedarray memories not supported).
Done (M5.6, TypeScript and Racket test review, 2026-11-20): every client and service test case
in the pinned `robotraconteur_typescript`, `robotraconteur_typescript_node` and
`robotraconteur_racket` checkouts was classified as covered, portable or not applicable.
- Ported now: TS3 `nolock_test` (server-held lock: locked members raise `ObjectLocked`,
  `[nolock]`/`[nolockread]` members and memories behave as in the C++ `TestNoLock`); TS1
  `broadcastpipe` and `broadcastwire` streaming; `i32_huge` (10 MB property); 200 concurrent async
  calls; connection failures (refused port, unknown service, wrong node name); misuse errors
  (double wire connect, wrong member kinds, `getInValue` before a value, wrong-typed wire and pipe
  values); string objref indexes with characters that need escaping; event listener isolation
  (every listener runs, the first error is raised afterwards; this changed the binding); Lua
  service checks: read-only property set raises `ReadOnlyMember` (the binding now enforces
  property direction before Lua runs, as generated skels do), wire direction on peek and poke,
  RR's `RobotRaconteurServiceIndex` lists the Lua service, `findServiceByType` with no match,
  `next()` after the end, and closing a service with a client connected.
- Differences from the TypeScript client, recorded rather than ported: RR core's clients
  acknowledge a broadcast pipe packet on arrival, not when Lua consumes it, so a broadcaster's
  backlog does not throttle a client that never drains its endpoint (packets queue until Lua
  reads them); a second `connectWire` on a connected wire fails with RR core's bare
  `std::exception` text instead of "Already connected".
- Portable, not done yet (gaps): subscription reconnect after a server restart (needs restarting
  the test server on the same port or a TCP proxy); a server that accepts and never answers
  (timeout, not hang); the legacy `tcp://host:port/node/service` URL; an idle heartbeat
  (`rr.sleep(7)` then a call); typed array coercion of tables and other element types into
  `double[]`; pipe endpoint index rules on a Lua service; more value types on the Lua service
  (`int32[]`, varvalue, string maps, a string-indexed objref with `.` and spaces); TS5 generators
  `f4`/`f5`/`f6`/`err_func3`, whose C++ test server implementations throw `NotImplemented`.
- Not applicable: features not implemented (object and monitor locks, service attributes,
  exception subnames and params, constants, connecting by implemented type, the server calling
  client callbacks, subscription filters and service-info subscriptions, wire/pipe close
  callbacks and ack listeners, a full Lua TestService1/3 for RR core's pytest suite, which needs
  locks, attributes and server-side callbacks); transports out of scope (TLS, WebSocket, browser,
  IPv6 zones); implementation-internal unit tests (packing, messages, URL selection); external
  example interop needing packages that are not pinned.
M5 acceptance: the MATLAB coverage is ported and the TypeScript/Racket review is done, with the
gaps above; a full sanitizer CI run is next.

