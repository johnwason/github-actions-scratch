# Progress

Read `CLAUDE.md` first. Keep this file current: it is how the next session, possibly on another
machine, knows where things stand. Confirm what it says against `git log` and `scripts/verify.sh`.

## Status

**Last verified state (2026-11-27):** `verify.sh` passes locally on Lua 5.4.9 and LuaJIT 2.1, 17
tests each, against RR core at robotraconteur PR 503's head `22c20c1` (shutdown leak fixes, on
master `caf757b`, which includes the merged PR 502; a trial pin). Scratch CI run 46080688952 at
that pin is **fully green**: secret scan, `verify`, TSan and ASan.

| Task | Status | Notes |
|---|---|---|
| M0.1 Repo skeleton, license | done | Apache-2.0 text from RR core with the appendix placeholder; CMake configures once M0.2 provides Lua |
| M0.2 `bootstrap.sh` | done | All steps ran in the container (in two passes after fixes); stamps make reruns no-ops |
| M0.3 `verify.sh` | done | `--network none`; static checks (singleton, absolute paths), then both runtimes |
| M0.4 Trivial module, error guard | done | `tests/m0`; numbers under Measurements |
| M0.5 Sanitizer builds and teardown stress | done | ASan+UBSan+LSan and TSan clean on GitHub (scratch run 44697703442), 3 tests each; run with `scripts/remote-ci.sh` |
| M0.6 GitHub Actions CI | done | CI runs in the scratch repository; full run 44702668545 green (secret scan, verify, ASan, TSan) |
| M1 Numerics | removed | Dropped from scope 2026-11-11; see "Decisions and plan changes" |
| M2.1 Dispatch queue, `rr.step` | done | Nodes, default node, borrowed-node C++ API, per-state queue, `rr.step(budget)`; teardown with live threads tested in `host_sim` |
| M2.2 `host_sim` harness | done | Scenarios `states`, `borrowed` and `connection` (states closed with live connections) pass. ASan and TSan clean with PR 503 pinned (run 46080688952) |
| M2.3 `rr.array`, marshalling | done | Every type, including multidim arrays of pods and namedarrays, round trips through the real wire format and against the C++ test server (TS1, TS3, TS5) |
| M2.4 Connect, properties, functions, objrefs | done | TS1, TS3 and TS5 pass on both runtimes; full CI green, sanitizers included (run 46080688952) |
| M3 Client streaming and async | done | Events, callbacks, wires, pipes, async ops, `rr.await`, nested-wait detection; `host_sim streaming` and the large-array check pass on both runtimes |
| M4 Server core | done | Services with properties, functions, events, objref registry, wire broadcasters and receivers, pipes; Python client and `host_sim service` pass on both runtimes |
| M5 Remaining parity | done | Generators, numeric memories, subscriptions, discovery, authentication; MATLAB coverage ported; TypeScript/Racket review done with gaps listed |
| M6 CoppeliaSim slice | human gate | Install and licensing needed |
| M7 Tesseract integration | human gate | Confirm demand first |

Status values: `not started`, `in progress`, `done` (acceptance criteria verified), `blocked`,
`human gate`.

## Next

1. Trial pin on robotraconteur PR 503's head (`22c20c1`). Full CI run 46080688952 is green in
   every job, the first fully green full run since the RR core shutdown leaks appeared: the ASan
   leaks (`host_sim connection`, `streaming`, `service`, ts3's test server, `server.lua_client`)
   are gone and TSan stays clean (one documented suppression). Move the pin to master once PR 503
   merges.
2. The wire initial-value proposal is still open (rechecked at the PR 503 pin).
3. One `luajit.host_sim.streaming` run under heavy host load (another project's build) lost the
   in-process test server ("Transport Closed", then connect timeouts) and hung until ctest's
   300 s timeout; it passed 6 of 6 alone and in the next full `verify.sh`. Watch for it in CI.
4. M6 and M7 are human gates. Remaining gaps are listed under the M5 notes in "In progress".

## In progress

Sanitizer findings with an instrumented test server (2026-11-27). The sanitizer builds now
build RR core's test server and test server library with the sanitizer flags (each links its
own static RR core). Run 45924055978, RR core `b746db8`:
- The earlier TSan races in `TcpTransportConnection::~TcpTransportConnection()`,
  `sync_async_handler<unsigned int>` and `MessageEntry::~MessageEntry()` were false positives:
  all were inside the uninstrumented library, whose `shared_ptr` reference counts TSan cannot
  see. With it instrumented, `host_sim connection` and `streaming` pass under TSan.
- Real, RR core: `ClientContext::AsyncProcessRequest` registers the request (handler and
  timeout timer) before `AsyncSendMessage`, which throws when the connection is gone; the catch
  rethrows without erasing it, so the handler still runs later (timeout, connection loss, or
  `AsyncClose1`'s sweep). In `AsyncClose` this invokes the close handler twice. TSan caught the
  second invocation in ts1 writing the binding's close result after `Client::close` had read it.
  Fixed in the binding: `rl::OpRef` and `rl::Completion` act on the first invocation only
  (before, a late completion of a request whose start threw would `luaL_unref` a registry slot
  Lua may have reused).
- Real, RR core: `ClientContext::GetConnected()` reads `m_Connected` without `m_Connected_lock`
  (TSan, `host_sim service`, against `AsyncClose1`'s locked write, Client.cpp:1428 and 2350).
  This also settles the earlier 0.14 s `host_sim service` pass: it now runs, 0.63 s.
- ts3 and ts5 could not connect to the TSan test server after ts1 failed. Run 45928662207, with
  the server's output captured, showed why: "bind: Address already in use". RR core sets
  SO_REUSEADDR only with `ROBOTRACONTEUR_TCP_SERVER_REUSEADDR=1`, so a server that exits with
  connections open leaves the port in TIME_WAIT. The test helper now sets it for the server.
  That server then exited, after its failed start, with RR threads still running; TSan reported a
  heap-use-after-free in Boost.Regex's traits cache during exit (the test server program's
  shutdown, not the binding).
- With the one-shot completions, ts1's client-side race is gone (run 45928662207). The TSan test
  server now reports a lock-order inversion, real and in RR core: `ServerContext::GetObjectSkel`
  calls the service's `GetSubObj` (user code) while holding `skels_lock`, and
  `ReleaseServicePath1` takes `skels_lock`. The test service's `get_o6` takes `o6_lock`, and
  `o6_op` calls `ReleaseServicePath` while holding it: two concurrent clients can deadlock. The
  binding is not exposed (its `GetSubObj` takes only its own object mutex, briefly, and it never
  calls `ReleaseServicePath`).
- ASan, run 45928662207: with its output captured, the ts3 test server (TestService3) leaks
  136,691 bytes at exit, all indirect (a reference cycle): its `ServerContext`, skels, wire and
  pipe servers and parsed service definitions. Same class as the other RR core shutdown leaks; it
  was hidden while the server's output was discarded.
- The C++ test server's output includes gtest "Failure" lines from ts1's deliberate wrong-value
  checks; only sanitizer reports fail the test.
- The discovery lock-order inversion (`server.lua_client`) is not a reachable deadlock and is
  now suppressed (see "Decisions", 2026-11-26). The ASan shutdown leaks are unchanged.

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

First sanitizer CI on RR core 1.3.0, run 45908992877 (at `da57a6b`), 2026-11-26: secret scan
and `verify` green. The old lock-order inversion is gone from ts1, ts3, ts5 and `host_sim service`,
which now pass under TSan. Remaining, all inside RR core:
- TSan, `host_sim connection` and `streaming`: a data race in
  `TcpTransportConnection::~TcpTransportConnection()` against `Close()` (a mutex destroyed while
  another thread locks it), inside `librobotraconteur_test_server_lib.so`'s own (not
  instrumented) RR copy.
- TSan, `server.lua_client`: a lock-order inversion between a `boost::recursive_mutex` and a
  `boost::mutex` in `Discovery_updatediscoverednodes::UpdateDiscoveredNodes`, reached from
  `findServiceByType` (the "no match" check, which also runs under `--network none`).
- ASan: the shutdown leaks are unchanged by the pin (`host_sim connection` 1,334,490 bytes,
  `streaming` 241,424, `service` 83,061, `server.lua_client` 898 from the discovery timer).
- Unconfirmed: `host_sim service` passed under TSan in 0.14 s, against 1.6 s natively and 4.2 s
  under ASan; ctest shows no output for passing tests, so check that it really ran.
Not suppressed: the reports are in RR core code, not the binding.

Examples (2026-11-20, at the project owner's request): Lua ports of RR core's Reynard client,
standard robot clients (jog_freespace, position_command, trajectory) and standard camera
clients (capture_frame, capture_frame_compressed, image, image_preview) in `examples/`, with a
README of the differences (camera frames are saved as PPM/PGM or PNG/JPEG instead of shown with
OpenCV; the endless loops take an optional run time). `tests/examples/test_examples.lua` runs
each one as a separate `lua` process against a Reynard mock (`tests/examples/reynard_mock.py`)
and the loopback robot and camera services of the pinned Racket client checkout, and checks what
the services saw (Reynard calls, robot joint positions after commands and trajectories, image
pixels and PNG bytes); it passes on both runtimes. Small API additions the examples needed:
`rr.connectService(url[, options])` (default node, as in CLAUDE.md's target API),
`c:getEnum(name)` (an enum's values; the binding has no general constants API yet),
`arr:toBytes()` (uint8/int8 arrays as a Lua string) and `rr.time()` (monotonic seconds).
Then, at the project owner's request: `c:newStructure(type)` (a struct, pod or namedarray with
every field at its default, as Python's `NewStructure`: 0, false, "", zero-filled or empty
`rr.array`s, `{}` for lists and maps, default pods and namedarrays (lists with `dims` for
multidim), nil for nested structs and varvalue; a short name is relative to the object's service)
and `c:getConstants([service])` (as Python's `RRN.GetConstants`: service constants including
hex, arrays and struct constants, a table per object or struct with constants, every enum as
`{name = value}`). Tested on TS3's constants and `testpod1`/`teststruct3` defaults (which
marshal), and TS1's `teststruct1`; the robot and camera examples now use them.

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
gaps above. Full CI run 44818417615 (at `dfb1389`): secret scan and `verify` green; sanitizers
show the known RR core findings (the connection and streaming leaks unchanged, `host_sim service`
82,463 bytes with the same signature, TSan's lock-order inversion) plus one new RR core leak:
- ASan, `server.lua_client`, 898 bytes, all indirect: a `findServiceByType` that finds nothing
  completes before its timeout timer fires; `Discovery_findservicebytype` keeps that `WallTimer`
  (whose handler holds the find object and the binding's completion) and the node shuts down
  before the timer is released. Same family as the core's late-timer leaks after shutdown
  (`docs/upstream-proposals/threadpool-shutdown-pending-handlers.md`); the binding's part is only
  the completion handler the timer holds. Reported for the core fix, not suppressed.

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
M4 acceptance: every item passes locally on both runtimes. Full CI run 44804134673 (at
`8fd171e`): secret scan and `verify` green (17 tests per runtime, including the Python client);
sanitizers show only the RR core findings:
- ASan: `host_sim connection` and `host_sim streaming` unchanged (1,334,910 and 241,494 bytes;
  the test server library's part is now suppressed), and `host_sim service` 189,093 bytes with
  the same signature: all indirect, rooted in client nodes created by the states and shut down at
  teardown with live connections (`StateContext::create_node`/`teardown`, stub factories, RR
  core `WallTimer::Start`). No frame from the service code (`LuaServiceObject`,
  `LuaServiceSkel`, server tasks). Not yet reproduced locally with a clean-close experiment as the
  streaming leak was.
- TSan: the same lock-order inversion, now also in `server.lua_client` and `host_sim service`;
  TSan halts there, so the server threading is checked once the core fix is in.

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
large-array check pass on both runtimes. Full CI run 44797718600 (at `d81abd2`): secret scan and
`verify` green; sanitizers show only the RR core findings already recorded under M2 item 4, no
new kind of report:
- ASan: `host_sim connection` 1,457,447 bytes and the new `host_sim streaming` 364,031 bytes, all
  indirect. Reproduced locally: closing the streaming state's wire, pipe and client before
  `lua_close` drops the streaming leak to 122,537 bytes, and all of that is allocated inside the
  test server library's own RR copy (its node and service objects, never freed after it stops).
  So the binding's part is the known "node closed with live connections" core leak.
  `tests/sanitizers/lsan.supp` now suppresses allocations made inside
  `librobotraconteur_test_server_lib.so` only; with it, streaming reports 241,494 bytes, the core
  leak. `host_sim streaming` skips its RSS check under ASan (quarantine holds ~360 MB).
- TSan: the same lock-order inversion, now also in `host_sim streaming`; TSan halts at the first
  report, so the M3 threading (callbacks, notifications) is only checked under TSan once the core
  fix is in. Rerun the full CI then.

M2.4 and the M2 acceptance. Done so far: dynamic stubs (`rl::DynamicStub`, `rl::StubFactory`,
installed on owned nodes), `rl::Client`, TCP/local/intra client transports on owned nodes, the test
vector reader (`rr._testVector`), and TestService1 properties (all numeric types, d5/d6 multidim,
64-bit boxes, string, struct from the server), functions, remote exceptions and objrefs against
`robotraconteur_test_server` (`tests/servicetest/test_ts1.lua`, `RUN_SERIAL`, port per runtime).
Next, in order:
1. Done: TestService1 properties, functions, objrefs, including the `o6` varobject swaps and a
   TestService2 object pulled on demand.
2. Done: TestService3 enums, complex, bools, exceptions, pods, `teststruct3` (every pod and
   namedarray shape, in fields, lists and varvalues) and namedarrays 1 to 5 (`test_ts3.lua`). The
   fill/verify helpers are ported to `tests/lua/seqgen.lua`; the server rejects a single changed
   value. Multidim pod and namedarray arrays are lists with a `dims` field. TestService5
   (`test_ts5.lua`, from `ServiceTestClient3.py`) done for properties, functions and errors; its
   generator members wait for M3. RR exception subnames and params are not exposed (Lua errors are
   strings naming the RR error); listed under Ideas.
3. Done: `host_sim connection` closes states with live connections and objrefs (3 cycles) against
   `librobotraconteur_test_server_lib`, and the service keeps working. Only TCP transport testing
   is required (see "Decisions and plan changes").
4. Full `scripts/remote-ci.sh` run 44707216414 (at `a9f1d18`): secret scan and `verify` green;
   **both sanitizer jobs fail.** The project owner is fixing the errors in RR core in a separate
   session (2026-11-20); rerun the full CI once that core fix is pinned, and recheck the leak below
   Run 44782056390 (at `7e2fe38`, with the TS3 pod and TS5 tests) shows the same two findings and
   nothing new: ASan the identical 1,433,777-byte leak in `host_sim connection` only (all servicetest
   tests clean), TSan the same inversion in ts1, ts3, ts5 and `host_sim connection`.
   for any part that is the binding's own. Findings:
   - **ASan/LSan, `host_sim connection`:** 1,433,777 bytes in 8,364 allocations leaked, all
     indirect (a cycle). The roots include service definitions parsed by `rl::StubFactory`
     (`client.cpp:69`, from `DynamicStubFactory::CreateServiceFactories` during
     `ClientContext::AsyncConnectService7`), so a node closed with live connections is not freed.
     Theory: `ClientContext` and its `DynamicStub`s hold each other, and node shutdown with
     connections still open does not break it, or a queued handler holds the context. Next step:
     reproduce deterministically like `node_release` (connect, shut the node down without closing,
     check a `weak_ptr` to the node and the stub) against `librobotraconteur_test_server_lib`.
     About 107 of the leak reports point into the test server library's own RR copy; check if they
     are separate and suppress them if they are not ours.
   - **TSan, `servicetest.ts1`, `servicetest.ts3`, `host_sim connection`:** "lock-order inversion
     (potential deadlock)" between a `boost::mutex` and a `boost::recursive_mutex`, both taken in RR
     core under `ClientContext::MessageEntryReceived` (`Client.cpp:1208`) on pool threads during
     connection setup. Get the full stacks from the job log (`scripts/ci-wait.sh` prints the run;
     the GitHub job log API gives the text), decide whether the binding's factory or stub causes the
     order (for example taking a lock in `CreateStub`) or whether it is RR core's; if it is core's,
     write an upstream proposal and add a narrow TSan suppression with the explanation here.

## Blocked / needs a human

None.

## Decisions and plan changes

Record the date, the evidence, and what changed.

- 2026-11-27: Removed the thread-pool workaround (`rl::ThreadPool`, `rl::init_node`, the
  `node_release` test); nodes use RR core's pool with plain `Init()`. At the PR 503 pin, core
  binds a weak node in `Post`/`TryPost`, the cleanup task checks shutdown, and RR objects release
  themselves from node shutdown listeners, so a drain is not needed (the project owner pointed at
  `AddShutdownListener`). Evidence: experiment branch with the workaround disabled, scratch CI run
  46084375403: every test passes under ASan and TSan; the only leak was `node_release`'s own
  handlers, which hold the node strongly by design. Rule in `CLAUDE.md`: work that holds the node
  must be released from a shutdown listener. Test count is 17 per runtime.
- 2026-11-27: Docs cleanup before the first public push. Milestones M2.2 to M5 are plain `done`
  now that sanitizer CI is green. Upstream proposals rechecked at the PR 503 pin (thread-pool:
  see the entry above; the wire initial-value bug is unchanged).
- 2026-11-27: Pin moved to robotraconteur PR 503's head `22c20c1` (shutdown leak fixes). PR 502
  was squash-merged to master as `caf757b`; PR 503 is 16 commits on it. Local verify 18 of 18 per
  runtime; full CI run 46080688952 green in all jobs, ASan included, so the RR core shutdown
  leaks are fixed by PR 503.
- 2026-11-27: Service attributes implemented, at the project owner's direction (they matter
  more than the other gaps). The M5.6 review had listed them as not applicable. Service side:
  `registerService(..., {attributes = {...}})` and `svc:setAttributes{...}`; client side:
  `c:getServiceAttributes()` and an `attributes` field in `findServiceByType` results. Values
  follow the varvalue rules (strings and `rr.array` detected, numbers through `rr.varvalue`) and
  must be strings or numeric arrays, as RR core documents; received values of other types are
  left out. Tested against TS1's attributes, a Lua service (with error cases) and the Python
  client (`RRN.GetServiceAttributes`). A service calling a client's callbacks is also not a
  priority (owner).
- 2026-11-27: The project owner says memories and locks are rarely used and no real service
  needs them. Pod and namedarray memories and object/monitor locks stay unimplemented and are
  not priorities; the numeric memories already done are kept. The same applies to exception
  subnames and parameters: not used in practice.

- 2026-11-27: Moved the trial pin from PR 502's earlier head `4886a7b` to its updated head
  `61548d2f55b94f52156a854d525d2402f388b1d5`, which adds two commits: `transports` cleared under
  `transports_lock` in `Shutdown()`, and `is_shutdown` read under `thread_pool_lock`. These are
  the two RR core races found on the earlier head.

- 2026-11-26: Added `tests/sanitizers/tsan.supp` with one entry, for the RR core discovery
  lock-order inversion reported in `server.lua_client` since 1.3.0. RR core's review classed it
  as not a bug, and the code at the pin agrees: the two locks are `Discovery_findservicebytype`'s
  `work_lock` and the `work_lock` of the `Discovery_updatediscoverednodes` object that
  `AsyncUpdateDetectedNodes` creates for each call (Discovery.cpp:1476). The caller takes the
  second lock once, at the top of `UpdateDiscoveredNodes`, while holding the first. The callbacks
  that take them in the other order are bound inside that critical section, and the caller never
  takes the second lock again, so the cycle cannot close. The RR core review also confirmed that
  the earlier `TcpTransportConnection`, `sync_async_handler` and `MessageEntry` reports were
  artifacts of the uninstrumented test-server library.

- 2026-11-26: Trial pin of RR core to robotraconteur PR 502's head,
  `4886a7b5a88e4523244efbd1c9fc37655fe03ba7` (branch `client-request-fixes`, on master `7638d31`),
  at the project owner's request. Moved from master `b746db8`. PR 502 addresses the findings
  under "Sanitizer findings with an instrumented test server": the request handler that ran after
  `AsyncProcessRequest` threw, the unlocked `GetConnected()`, the test server exiting with the node
  running after a failed setup, and the test service's `o6_op` deadlock. Not merged upstream yet:
  move the pin to master once it is.

- 2026-11-26: Moved the RR core pin from `fe165c6d` to master
  `b746db80845a7345c8fc4c7b002dbe2582e20ea4` (PRs 497-500; 498 fixes a use-after-free in
  `TcpAcceptor::CloseSocket` at transport close), at the project owner's request, to test whether
  the TSan `TcpTransportConnection` race is fixed. `bootstrap.sh` rebuilt; `verify.sh` passed on
  both runtimes (18 tests each) with no binding change.
- 2026-11-26: Moved the RR core pin from `7cd1610c519613f241b25453754fb118b270cdaf` to
  `fe165c6dcbcfb3864674b946e173a7cc1215c41e` (master, the merge of robotraconteur PR 494: a
  344-commit hardening series, version 1.3.0), at the project owner's request after a review of
  that PR. `bootstrap.sh` rebuilt RR core and the test servers; `verify.sh` passed on both
  runtimes with no binding change. The Python peer stays at the released 1.2.9, which also tests
  interoperability with it.
- 2026-11-26: Repaired this file. From commit `5772921` (2026-11-20) on, edits that inserted new
  text after "## In progress" dropped the rest of the file (an editing-script bug), losing the
  later sections and, at each milestone, the previous milestone's notes; later edits aimed at the
  lost sections did not apply. Rebuilt from git history: the sections after "In progress" from
  `26c0d8d`, the M3 to M5 and examples notes from `450fea9`, `15d0faa`, `142abd1` and `c9ca1c8`,
  and the three decision entries below that never applied, re-added with their original dates.
- 2026-11-20: Pinned two more Python test peers in `scripts/pins.sh`, installed into the venv by
  the bootstrap: `robotraconteurcompanion` 0.4.0 (standard robdefs) and
  `robotraconteur-abstract-robot` 0.2.4, needed by the loopback robot and camera services the
  example tests run. Their own dependencies (scipy, general-robotics-toolbox, lxml, ...) are
  resolved by pip at bootstrap time and not pinned.
- 2026-11-20: Client callbacks wait on an RR pool thread for the Lua thread, as in MEX. RR core's
  `ClientContext::ProcessCallbackCall` takes the reply synchronously from `ServiceStub::CallbackCall`
  and has no async variant, so this is the only way to answer from Lua. The wait is bounded by the
  node's request timeout and released at once when the state closes; CLAUDE.md's teardown rule
  names this exception.

- 2026-11-20: Only TCP transport testing is required (project owner). The `IntraTransport` two-node
  check is dropped from M4; M4 instead checks a Lua client and a Lua service in separate nodes over
  TCP. Owned nodes still register the local and intra client transports, untested.
- 2026-11-20: The RR core errors behind the M2 sanitizer findings are being fixed by the project
  owner in a separate session; this session continues the remaining M2 client work meanwhile.

- 2026-11-20: Moved the M2 acceptance check "two nodes in one process talk over `IntraTransport`" to
  M4. It needs a service on the binding's own copy of RR core in the same process: RR's
  `librobotraconteur_test_server_lib` carries its own static RR copy, so its intra transport is not
  shared with ours, and the binding cannot serve until M4. Writing a throwaway C++ service with
  generated thunks only for this check was judged not worth it.

- 2026-11-20: Settled the open type mapping rows (M2) after comparing the TypeScript value types
  guide and the Racket README. Same as TypeScript: complex as `{real, imag}`, pods and namedarrays as
  plain tables of fields (lists of tables for arrays), exact conversions, received varvalue numbers
  always arrays, strings and typed arrays detected for varvalue. Same as Racket: enum names accepted
  on input. Differs: 64-bit integers Lua cannot hold come back as a 1-element `rr.array` box instead of
  a bigint, because Lua has no bigint type and LuaJIT numbers are doubles; complex arrays are
  `rr.array` with `{real, imag}` elements rather than interleaved real arrays. "Aim for no copy" is
  not met for pods and namedarrays; recorded under Ideas.

- 2026-11-20: All CI runs in `johnwason/github-actions-scratch`, at the project owner's direction
  ("use the scratch repo for all ci and continue committing at your discretion"). Renamed
  `scripts/remote-sanitizers.sh` to `scripts/remote-ci.sh` and `.github/scratch/sanitizers.yml` to
  `.github/scratch/ci.yml`, which adds a secret scan of the snapshot tree and a `--quick` mode;
  added `scripts/ci-wait.sh`. `CLAUDE.md` gets a "CI" section; the session protocol checks the latest
  scratch run; M0.6 is accepted on a green full scratch run. `.github/workflows/ci.yml` stays for the
  human's own pushes.

- 2026-11-20: Every node the binding creates is initialized with `rl::init_node` (hard rule
  "State teardown" updated). Evidence: the first GitHub ASan run (scratch run 44694486971) of
  `teardown_stress` reported LeakSanitizer leaks: 3 of 80 nodes, 61,565 bytes in 467 allocations,
  all indirect. RR core's `ThreadPool::Shutdown` leaves queued handlers that hold the node in its
  `io_context`, a cycle; and `StartPeriodicCleanupTask` can arm a timer after `Shutdown()` cancelled
  it. `rl::ThreadPool` drains queued handlers after the base shutdown, and `rl::init_node` waits
  until the cleanup task has run. New test `node_release`: RR's pool leaks 30 of 30 nodes; the drain
  alone still leaked 0–3 of 10; drain plus init wait leaked 0 of 180 over six runs. Upstream
  proposal with patch: `docs/upstream-proposals/threadpool-shutdown-pending-handlers.md`.

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
- Error objects for RR exceptions (a table or userdata with name, message, subname and params,
  with `__tostring`) instead of strings, so scripts can read subnames and params.
- Pass arrays of pods and namedarrays without a copy, e.g. a namedarray array as an `rr.array` of its
  element type with an extra leading dimension.

## Session log

Newest first. Each entry: date, machine-independent summary, what was verified and how.

- 2026-11-20: M0 done; M2.1–M2.3 implemented, M2.4 largely implemented. Verified with `verify.sh`
  on both runtimes (12 tests each: module, arrays, marshalling round trips through the wire format,
  `host_sim` states/borrowed/connection, and TestService1 and TestService3 against RR's C++ test
  server with values from the test vector blob, the server validating what we send) and green
  quick scratch CI runs. Remaining for M2 is listed under "In progress".
- 2026-11-20: M0.5 done. Sanitizers moved to GitHub at the project owner's direction
  (`scripts/remote-sanitizers.sh`, scratch repository, orphan branches). First run (44694486971):
  `verify.sh` and TSan passed, ASan/LSan found RR core leaking shut-down nodes. Worked around with
  `rl::ThreadPool` and `rl::init_node`, added the deterministic `node_release` test (mutation-checked
  against RR's stock pool), and wrote an upstream proposal. Second run (44697703442): ASan+UBSan+LSan,
  TSan and `verify.sh` all passed, 3 sanitizer tests each; the job logs were read to confirm the
  tests ran. `verify.sh` also passes locally on both runtimes (4 tests each).

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
