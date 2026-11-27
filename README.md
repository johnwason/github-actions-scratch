# Robot Raconteur for Lua

> **An AI-generated experiment.** Nearly all of the code, tests, scripts and documentation in
> this repository were written by an AI coding agent (Claude, through Claude Code), working
> from a written plan, with a human owner directing scope and reviewing and pushing commits.
> It has not been used in production. Treat it as a research prototype. See
> [How this was generated](#how-this-was-generated).

A [Robot Raconteur](https://github.com/robotraconteur/robotraconteur) (RR) client and service
binding for Lua 5.4 and LuaJIT 2.1. It is built for **embedded Lua hosts**, such as simulators
and game engines, where Lua runs on the host's own thread and the host calls into the script once
per frame. It is a C++ module on top of RR core, the same library the Python, MATLAB and C#
bindings use.

## Status

Every milestone up to the host-specific ones is implemented, and passes its tests on both
runtimes:

- **Clients:** connections, properties, functions, objrefs, events, callbacks, wires, pipes,
  generators, memories, async calls with `rr.await`, subscriptions, discovery and
  authentication.
- **Services:** properties, functions, events, objrefs, wire broadcasters and receivers, pipes,
  generators, memories and password authentication.

The work is tracked in [`PROGRESS.md`](PROGRESS.md). Known gaps and limits:

- **Not implemented yet:** pod and namedarray memories, object locks, service attributes, a
  service calling a client's callbacks, and error subnames and parameters.
- **Transports:** only TCP is tested.
- **Building:** only through the project's container scripts on Linux x86_64. There are no
  prebuilt packages.
- **Sanitizer CI fails.** Memory and thread checks report findings that trace to RR core
  itself (not this binding): leaks when nodes shut down with live connections or pending
  timers, and a lock-order inversion. They are recorded in `PROGRESS.md` and
  [`docs/upstream-proposals/`](docs/upstream-proposals/), and are being fixed in RR core.

## A short example

```lua
local rr = require("robotraconteur")

local c = rr.connectService("rr+tcp://localhost:29200?service=reynard")
c:addEventListener("new_message", function(msg) print("New message: " .. msg) end)

c:callFunction("teleport", 0.1, -0.2)
c:setProperty("color", { 1, 0, 0 })
print(c:getProperty("color"))

local state = c:peekWireInValue("state")
print(state.robot_position[1], state.robot_position[2])

rr.sleep(1) -- waits while still delivering events and callbacks
c:close()
```

In a host, the script calls `rr.step()` once per frame. Lua only ever runs on that thread;
Robot Raconteur's own threads queue work for it. More examples, ported from RR core's Python
examples (Reynard the Robot, a standard robot, a standard camera), are in
[`examples/`](examples/).

## Building and testing

You need Linux x86_64 and podman. Third-party code is built and run only inside the container.

```sh
scripts/bootstrap.sh   # builds the container image, Lua, LuaJIT and RR core (pinned in scripts/pins.sh)
scripts/verify.sh      # builds the module for both runtimes and runs the tests (no network)
```

The module is built at `build/lua54/lua_modules/robotraconteur.so` (and likewise for LuaJIT).
The tests run against:

- RR core's C++ test server;
- RR core's Python client;
- the binding talking to itself;
- a fake embedding host (`tests/host_sim`) that closes and recreates Lua states while
  connections, callbacks and services are still live.

Memory and thread sanitizer builds run in CI.

## How this was generated

- **Agent and model.** Claude Code, running Claude Opus 5.5 in a terminal session with tool
  access, over about two weeks (November 2026). The commits carry a `Co-Authored-By: Claude`
  trailer.
- **The plan.** [`CLAUDE.md`](CLAUDE.md) defines the goals, the design decisions with their
  reasons, hard rules for the C++ code, the security rules, and a milestone plan with acceptance
  criteria for each milestone. The agent worked through the milestones in order. When its working
  context filled up it was summarized, and the work carried on from those summaries plus
  [`PROGRESS.md`](PROGRESS.md), which records status, decisions with their evidence and date,
  measurements, and open findings.
- **The human's role.** The project owner set and changed the scope during the work:
  - dropped a planned numerics milestone;
  - moved all CI to a separate scratch repository;
  - limited transport testing to TCP;
  - asked for the example ports, `newStructure` and `getConstants`.

  The owner fixed RR core bugs the agent found, in a separate session, and is the only one who
  pushes to this repository. The agent committed locally and never pushed here; it ran CI by
  pushing snapshots to a scratch repository with a scoped token.
- **Verification.** The agent was required to test every feature against independent peers
  rather than only against itself:
  - RR core's C++ test server, with expected values from RR's shared test-vector file. The
    server checks the values the client sends, and the tests confirm it rejects wrong ones.
  - RR core's Python binding, both as a client of Lua services and as a source of example
    services.
  - Ports of RR core's MATLAB tests.
  - A review of the TypeScript and Racket implementations' test suites, done by a sub-agent.
    The relevant cases were ported and the rest listed as gaps.
- **What it found.** Building the binding turned up several RR core issues, written up in
  `docs/upstream-proposals/`:
  - thread-pool handlers and timers that keep shut-down nodes alive;
  - a race in which every client drops a wire broadcaster's initial value;
  - lock-order and leak reports from the sanitizers.
- **Size.** 56 commits. About 9,200 lines of C++ in `rr_lua_core`, 500 of SWIG interface,
  3,700 of tests, 600 of build and CI scripts, and 460 of examples.

AI-generated code can be wrong in ways that look confident. The test suite is the best evidence
of what works; the gaps above are the known limits, not a complete list.

## License

Apache License 2.0, see [`LICENSE`](LICENSE).
