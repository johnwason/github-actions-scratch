# robotraconteur_ros2_debug_bridge — project plan

## 1. Goal

An `rclcpp_components` component (plus a standalone executable), written in C++,
that runs inside a single-process ROS 2 application and exposes ROS entities over
Robot Raconteur (RR). It is:
- the **only external communication path** for applications on
  `rmw_intraprocess_cpp`, and
- the **debugging/introspection tool** that replaces the `ros2` CLI, rqt and
  rosbag-over-the-network, which can't see into an intra-process-only
  application.

**Convention source:** the existing Python bridge
[robotraconteur-contrib/robotraconteur_ros2_bridge](https://github.com/robotraconteur-contrib/robotraconteur_ros2_bridge)
(Apache-2.0). This project follows its naming and object conventions so that
client code written for that bridge works here with at most a connection URL
change. Section 2.3 summarizes the conventions as surveyed on 2026-11-13. Re-check
the upstream source (`robotraconteur_ros2_bridge.py`,
`experimental.ros2_bridge.robdef`) before B2, and record any differences in
`docs/type_mapping.md`.

### Non-goals
- A general ROS↔RR bridge for multi-process ROS systems (it may work, but
  isn't designed for that).
- Mapping to `com.robotraconteur.*` standard types. That's a possible follow-on
  layer (Q2).

### Success criteria (project level)
1. With no code generation, a Python RR client (dynamic types) can list the
   ROS graph and echo, publish, call and set any **allowed** topic, service,
   action or parameter.
2. Generated message/service definitions are compatible with the existing
   bridge's (same names, same member names, a superset only).
3. Deny-by-default exposure. Disallowed entities are invisible and
   inaccessible.
4. Works under both `rmw_intraprocess_cpp` and `rmw_fastrtps_cpp`.
5. Windows-portable code. RR/Boost don't leak through public headers.

## 2. Architecture

### 2.1 Package layout (proposed)
```
robotraconteur_ros2_debug_bridge/
  robotraconteur_ros2_debug_bridge/            # ament_cmake package
    include/robotraconteur_ros2_debug_bridge/  # public headers: no RR/Boost types
    src/
      bridge_component.*           # rclcpp component; owns RR node lifecycle
      type_mapper.*                # ROS introspection → RR service definition text
      value_converter.*            # ROS message memory ⇄ RR values (both directions)
      exposure_policy.*            # deny-by-default allow rules (YAML)
      graph_object.*               # RR object: nodes/topics/services/types/QoS
      topic_proxy.*                # subscriber (pipe + wire) / publisher objects
      service_proxy.*              # rosclient objects → async ROS clients
      action_proxy.*               # RR generators ⇄ rclcpp_action
      parameter_proxy.*
      component_loader.*           # load/unload components (class_loader)
    robdef/experimental.ros2_debug_bridge.robdef  # static root definition
    config/default.yaml            # safe defaults: no TCP, no announce/discovery
    test/                          # gtest + Python RR client tests
  tools/                           # Python RR-client debug CLI (echo/pub/call/param/graph)
  docs/  design.md  type_mapping.md  security.md  windows.md
  scripts/ build-image.sh build.sh test.sh check-portability.sh
  container/ Containerfile
```

### 2.2 Names
| Item | Existing bridge | This project |
|---|---|---|
| Root service definition | `experimental.ros2_bridge` | `experimental.ros2_debug_bridge` |
| Root object | `ROS2Bridge` | `ROS2DebugBridge` |
| RR node name / registered service name | `ros2_bridge` / `ros2_bridge` | `ros2_debug_bridge` / `ros2_debug_bridge` |
| ROS node name | `robotraconteur_ros2_bridge` | `robotraconteur_ros2_debug_bridge` |
| TCP port | 34572 (always on) | TCP **off** by default. Port configurable, default 34573 |

### 2.3 Conventions taken from the existing bridge

**Generated service definition names** (`pkg/Type` input, `__` separator):
- Messages: `rosmsg_<pkg>__<Msg>`, containing `struct <Msg>`.
- Topics: `rostopic_<pkg>__<Msg>`, containing the `subscriber` and `publisher` objects.
- Services: `rosservice_<pkg>__<Srv>`, containing `struct <Srv>Request` and `struct <Srv>Response`.
- **New in this project:** actions use `rosaction_<pkg>__<Action>`, with `<Action>Goal`,
  `<Action>Result`, `<Action>Feedback` structs (plus a status struct, designed in B8).
- Nested message fields reference `rosmsg_<pkg>__<Msg>.<Msg>` and add an `import`.

**Field types** (matching the upstream code, plus extensions marked ✚):
| ROS | RR |
|---|---|
| `int8`…`uint64` | same name |
| `float32` | `single` |
| `float64` | `double` |
| `bool` | `bool` |
| `char` | `uint8` |
| ✚ `byte`/`octet` | `uint8` (upstream raises) |
| `string` | `string` |
| ✚ `wstring`, bounded strings | `string`, with bounds checked when writing into ROS (upstream raises or ignores) |
| `T[]`, `sequence<T>` (primitive) | `T[]` |
| ✚ `T[<=N]` | `T[]`, with bounds checked when writing into ROS (keeps upstream's style) |
| `T[N]` (primitive) | `T[N]` |
| arrays of strings or messages | `{list}` |
| nested `pkg/Msg` | `rosmsg_pkg__Msg.Msg` |
| ✚ constants | RR `constant` members (upstream ignores constants) |

`builtin_interfaces/Time` and similar map generically like any other message.

**Name fixing (`fixname`):** a field name that is an RR reserved word, or that
starts with `rr` or `robotraconteur` (case-insensitive), gets a `ros` prefix. All
other names are kept. The original ROS name is still used to access ROS fields.

**Topic objects** (in `rostopic_<pkg>__<Msg>`):
```
object subscriber
    pipe rosmsg_pkg__Msg.Msg subscriberpipe [readonly]
    wire rosmsg_pkg__Msg.Msg subscriberwire [readonly]
    function void unsubscribe()
end
object publisher
    function void publish(rosmsg_pkg__Msg.Msg m)
    ✚ pipe rosmsg_pkg__Msg.Msg publisherpipe [writeonly]
    ✚ wire rosmsg_pkg__Msg.Msg publisherwire [writeonly]
end
```
**Wire vs pipe: the client chooses.** Every subscriber exposes both the pipe and
the wire, and every publisher accepts `publish()`, the pipe and the wire. The
bridge doesn't choose for the client. It feeds whichever members have connected
clients (broadcasters for subscribe, an aggregated pipe/wire receiver for
publish). Transient-local topics set the wire's InValue immediately, so late
wire connections get the latched value.

**Service objects:** `rosclient` with `call(request)` and
`wait_for_service(double timeout)`. These are RR functions, implemented
asynchronously on the bridge side. Upstream's `register_service` (an RR client
acting as a ROS service server) is supported only if enabled (capability
`serve_service`, default off).

**Root object pattern:** functions return an `int32` handle, and a matching
`objref varobject{int32}` gives access to the object, as in upstream:
`subscribe(topic, msgtype)` → `subscribers`, `publish(topic, msgtype)` →
`publishers`, `client(service, srvtype)` → `clients`, `register_service(...)` →
`services`.
Extensions (✚):
- `msgtype` may be an empty string, in which case the bridge infers it from the graph.
- `subscribe_qos` / `publish_qos` take an explicit QoS struct. RR has no
  overloads, so these are separately named functions.
- Default QoS: match the discovered publisher's QoS where possible, otherwise
  depth 10 (upstream's default).
- New root members for the graph, parameters, actions and component loading.

### 2.4 Data path
- Default: RMW-agnostic generic subscriptions and publishers (serialized CDR) →
  deserialize into ROS message memory with introspection → convert to RR.
- Optional fast path: the `rmw_intraprocess_cpp` tap API (sibling M9) supplies
  typed message memory directly. Detect it at build time, and prove it with a
  benchmark before switching it on by default.

### 2.5 Transports and security
- Defaults: `IntraTransport`/`LocalTransport` only. TCP off. Node announce and
  discovery listening off. TLS and authentication configurable (B11).
- Separate, individually enabled capabilities: `read`, `publish`,
  `call_service`, `serve_service`, `action`, `set_parameter`, `load_component`.
  Only `read` is on by default.

## 3. Milestones

Each milestone's acceptance criteria (AC) must pass in the container under
`--network=none`, including the sanitizer variants, and against both RMWs once
`rmw_intraprocess_cpp` M2 is available. Until then, use `rmw_fastrtps_cpp` only.

### B0 — Build infrastructure
- Containerfile FROM a pinned `ros:jazzy-ros-base` digest.
- **Robot Raconteur from GitHub `master`**
  (`github.com/robotraconteur/robotraconteur`): clone at image build time, build
  and install the C++ core and the Python 3 bindings. Record the resolved commit
  SHA as an image label and in `scripts/build-image.sh` output, so failures can be
  tied to an RR revision. Updating RR means rebuilding the image. Check the
  repo's own build docs for the correct CMake options; don't guess them.
- Also clone the existing `robotraconteur_ros2_bridge` (master, SHA recorded)
  into the image, used only as a test reference (B2).
- The build workspace mounts `../rmw_intraprocess` read-only, so both build together.
- Build variants: release, asan, tsan. Portability check, plus a check that
  public headers don't include RR or Boost.
- An RR test helper that fails if a node is created with announce or discovery
  enabled or with non-loopback TCP.
- **AC:** an empty package builds. A smoke test proves there is no network.
  A Python RR client connects to a C++ RR service over `LocalTransport` inside
  the container. The RR commit SHA is printed.
- Done 2026-11-14 (branch `b0-infra`). Image `localhost/robotraconteur_ros2_debug_bridge-dev:jazzy`:
  RR master `7cd1610c` (1.2.9) built with colcon in its ROS mode
  (`-DROBOTRACONTEUR_ROS=ON -DBUILD_TESTING=OFF`) into `/opt/robotraconteur`;
  reference bridge `d587d2cd` in `/opt/robotraconteur_ros2_bridge`; both SHAs
  are image labels and printed by `scripts/test.sh`. Safety helpers
  `test/rr_test_safety.{hpp,cpp,py}` plus `test_safety_helper_usage.py`
  (scans test/ and tools/). AC: `test_python_client` (C++ service from
  generated thunk code, Python dynamic client, LocalTransport) and the
  no-network check pass in release and asan; portability and public-header
  checks pass. TSan: disabled locally as in the sibling repo; it moves to
  this repo's GitHub CI (its own `push-ci.sh`, S3) by B1, whose AC needs it.

### B1 — Component skeleton and RR lifecycle
- A component that owns the RR node, loads `config/default.yaml`, registers the
  root service, and starts and stops cleanly. The standalone executable composes
  it with a test node.
- **AC:** repeated load/unload of the component (100×) under ASan/TSan with no
  leaks or races. A client reads a trivial root property.
- Done 2026-11-14 (branch `b1-component`). `DebugBridge` component with a private
  RR node, strict `config/default.yaml` (safe defaults), static root object
  `ROS2DebugBridge` (ros_node_name, ros_namespace, rmw_implementation,
  bridge_version, capabilities), public `make_bridge_node()` only, standalone
  `debug_bridge` (bridge + demo node). AC: `test_component_lifecycle` (100
  load/unload cycles through the component factory, a client reading the root
  properties each time) and `test_standalone` (Python client, LocalTransport)
  pass under both RMWs in release, asan (local) and tsan (GitHub job, run
  43651793961). Deviations/findings: the component hides the static RR core's
  symbols (`-Wl,--exclude-libs,ALL`; exported RR globals were shared with the
  test's RR copy and destroyed twice), so IntraTransport reaches only clients
  inside the component's RR copy and the tests use LocalTransport; RR client
  lock-order inversion suppressed and reported (docs/upstream_issues.md).
  Open: one TSan run saw `debug_bridge` not exit within 30 s of SIGINT under
  rmw_intraprocess_cpp (not reproduced; the shutdown steps are now logged and
  the test prints them on timeout); one asan run reported an 80 KB leak when
  the test unloaded the component library before exit (not reproduced in 1000
  cycles; no per-load growth of RR globals).

### B2 — Type mapper (ROS → RR definitions)
- Generate `rosmsg_`/`rosservice_`/`rostopic_`/`rosaction_` definitions from
  introspection, including nested types, imports, `fixname` and constants.
- **AC:** golden-file tests for `std_msgs`, `geometry_msgs`, `sensor_msgs`,
  `nav_msgs`, `turtlesim`, `example_interfaces`, `action_tutorials_interfaces`.
  Every generated definition parses and verifies in RR. **Compatibility test:**
  for each type the existing bridge supports, run its `msg`/`srv` generation mode
  in the container and check that our definitions are a member-compatible superset.
- **Done 2026-11-14** (`b2-type-mapper`): release and asan local gate 145
  tests, 0 failures; GitHub TSan run 43660675623 145 tests, 0 failures.
  307 golden files, all 281 installed interfaces verified, and upstream
  compatibility (272 upstream definitions, 940 members, identical; the test
  runs upstream's ROSTypeAdapterManager in-process, the code its msg/srv
  modes call). Design
  and extensions are in docs/type_mapping.md. RR master mis-verifies 8-bit
  constants, so they are widened (docs/upstream_issues.md, reported).
  The action and publisher pipe/wire members are defined now and served in
  B8/B4.

### B3 — Value converter
- ROS message memory ⇄ RR values, for both C++ and C introspection type support.
- **AC:** property-style round-trip tests (randomized messages) for all B2
  packages. Bounds violations are rejected cleanly on writes into ROS.
- **Done 2026-11-15** (`b3-value-converter`): release and asan local gate 182
  tests, 0 failures; GitHub TSan run 43664083135 182 tests, 0 failures.
  Round trips C++ → RR → wire → C → RR → C++ for all 189 message-shaped
  types of the B2 packages; RobotRaconteurGen's generated code unpacks and
  repacks our values identically; rejection paths for both flavors
  (docs/type_mapping.md, Values). UBSan found that the C type support's
  init leaves fixed arrays without defaults uninitialized; DynamicMessage
  zeroes the buffer first.

### B4 — Topics
- Exposure policy (deny-by-default). Subscriber objects (pipe and wire
  together) and publisher objects (function, pipe and wire).
- **AC:** a Python client subscribes to `sensor_msgs/JointState` and
  `std_msgs/String` using **both** the wire and the pipe at the same time, and
  publishes via all three paths. Denied topics are invisible. Transient-local
  values reach a late wire connection. The upstream `examples/turtlesim_*`
  scripts work after changing only the URL. Tests pass under both RMWs.
- **Done 2026-11-15** (`b4-topics` + fixes on `b5-graph`/`b6-services`): local
  release/asan and GitHub TSan run 43670539181, 296 tests, 0 failures. Python
  client subscribes to JointState and String with wire and pipe at once and
  publishes via function, pipe and wire; denied topics look nonexistent;
  transient-local value reaches a late wire; upstream turtlesim
  subscriber/publisher examples run with the URL changed (and the client
  import replaced by a safety-helper preamble, CLAUDE.md S2); both RMWs.
  Generic RR server layer in src/generic_service.*. Proxies' ROS entities
  live on their own node and thread (rcl type cache races, docs/topics.md).
  RR issues found and worked around: wire value lost on connect;
  docs/upstream_issues.md.

### B5 — Graph introspection
- Nodes, topics, services, actions, types, endpoint QoS, publisher and
  subscriber counts. Graph changes are reported (event or wire).
- **AC:** the client sees entities appear and disappear as nodes are composed and
  removed.
- **Done 2026-11-15** (`b5-graph`): same runs. graph property,
  graph_changes wire and topic_endpoints; nodes, topics, services and
  actions filtered by exposure rules; test sees a node and its topic appear
  and disappear, under both RMWs.

### B6 — Services
- `rosclient` objects backed by async ROS clients. Timeouts and errors map to RR
  exceptions. Optional `register_service` (capability-gated).
- **AC:** add-two-ints from a Python client. No RR thread blocks (verified with
  a stress test and TSan).
- **Done 2026-11-15** (`b6-services`): same runs. add-two-ints from Python;
  unavailable, timeout and denied cases; 40 concurrent long waits plus slow
  calls while property reads stay under 1 s (no RR thread waits; TSan
  clean); register_service (serve rule + serve_service) with a ROS client
  getting the Python servicefunction's answer. RR's
  AsyncProcessCallbackRequest is broken (docs/upstream_issues.md).

### B7 — Parameters
- List, describe, get and set across nodes. Parameter events available.
- **AC:** set and read parameters on a composed demo node. Setting is denied
  unless enabled.
- **Done 2026-11-15** (`b7-parameters`): local release/asan and GitHub TSan
  run 43679797431, 409 tests, 0 failures, both RMWs. list, describe, get
  and set on /test_params (composed in the test host) and the parameter
  events pipe; set needs the set rule and the set_parameter capability;
  denied nodes look nonexistent.

### B8 — Actions
- Generator-based goals: the generator yields a status struct (state plus optional
  feedback and result). Abort/Close cancels. Rejected goals raise errors.
- **AC:** Fibonacci action from a Python client, including cancelling mid-goal.
- **Done 2026-11-15** (`b7-parameters`, commit e555e85): same runs.
  Fibonacci from Python: feedback, result, cancel mid-goal (Abort), rejected
  goal raises, type from the graph, denial. The action protocol is
  implemented directly (rclcpp_action has no generic client).

### B9 — Component loading
- Load and unload components into the host process (class_loader) by
  package/plugin name. Default off.
- **AC:** load a demo component, see it in the graph, unload it, all under
  ASan.
- **Done 2026-11-15** (`b9-components`): same runs. Loads
  demo_nodes_cpp::Talker into /test_container, sees it in the graph, lists
  and unloads it (also under ASan); load failures and denial.

### B10 — Debug CLI tools
- A Python tool in `tools/` (RR dynamic client): `graph`, `echo` (`--wire` or
  `--pipe`), `pub`, `call`, `param`, `action`, `load`. Connects by URL. Never
  runs discovery unless the user passes an explicit flag.
- **AC:** an integration test drives every subcommand over `LocalTransport` in
  the container.
- **Done 2026-11-15** (`b10-cli`): same runs. test_cli drives graph, echo
  (pipe and wire), pub (three ways), call, param, action, load, components
  and unload over LocalTransport; errors exit cleanly. Only the URL's
  transport is started, never discovery.

### B11 — Security hardening
- TLS and authentication configuration, capability flags, a fuzz test on the
  inputs the converter receives from RR clients, and `docs/security.md`.
- **AC:** an unauthenticated client is rejected when auth is on. Fuzzing finds
  no crashes in a time-boxed run.
- **Done 2026-11-15** (`b11-security`): same runs. With authentication on,
  no credentials and wrong credentials are rejected, a valid user connects;
  TLS is configuration only (no node certificate in the container;
  docs/security.md). test_converter_fuzz: 20 s per variant, no crashes.

### B12 — Fast path and performance
- Use the `rmw_intraprocess_cpp` tap API when present. Benchmark latency and
  throughput for image and point-cloud sized messages, comparing wire and pipe.
- **AC:** a benchmark report in `docs/`. The fast path is on by default only if
  it's measurably better and passes every test.
- **Done 2026-11-15** (`b12-fast-path`): same runs. docs/benchmark.md:
  images and point clouds, pipe and wire, both RMWs. The fast path is at
  most 15 % faster but converts inside the application's publish()
  (+0.2 to 0.75 ms per message), so it is **off by default**
  (`fast_path: true` turns it on). Tests: tap used only with
  rmw_intraprocess_cpp and fast_path on, never for transient-local topics;
  benchmark smoke test on both RMWs. Also fixed: an RR node leak from the
  wire's resend timers (LeakSanitizer).

### B13 — Windows readiness
- `docs/windows.md`: build against the ROS Windows environment, the Boost/CRT
  compatibility plan, and the list of DLLs that get deployed.
- **AC:** the user confirms an MSVC build and a Python client test on Windows
  (Claude can't do this step).
- **Status 2026-11-15** (`b13-windows`, not done: the AC is the user's):
  docs/windows.md. The bridge CI has a Windows job (windows-2022, MSVC 2022,
  RoboStack Jazzy; Robot Raconteur master built from source with
  ci/patches applied): run 43690833593, 415 tests, 0 failures, 2 skipped
  (test_standalone's SIGINT case; Windows stops processes by closing stdin,
  `--stop-on-stdin-close`, user decision). Same run: TSan 415, 0 failures.
  The patch fixes an RR client bug only the MSVC build shows
  (docs/upstream_issues.md); drop it when RR master has the fix.

### Later — Cross-testing
The user will create a GitHub repo for cross-testing (real networks, Windows,
other RR clients) once a prototype works. Claude doesn't create, push to, or
add remotes for it. The user will give instructions when it exists.

## 4. Testing strategy
- gtest unit tests (mapper, converter, policy). Python client integration tests
  (pytest, RR dynamic client) inside the container.
- Golden files for generated definitions, plus the compatibility test against
  the existing bridge.
- Run every integration test against both RMWs.
- Sanitizers on every milestone. Fuzzing in B11.
- All tests run under `--network=none` with the RR test helper's safety
  checks.

## 5. Decisions and open questions

### Decided (user, 2026-11-13)
- ROS 2 **Jazzy only**.
- Robot Raconteur from **GitHub `master`**.
- Follow the **existing robotraconteur_ros2_bridge conventions**.
- **Wire and pipe both offered**, and the client chooses.
- Cross-testing via a user-created GitHub repo, later.
- **License (was Q3): Apache-2.0**, `Copyright 2026 Wason Technology, LLC`.

- **Q1 (decided): default TCP port 34573** when TCP is enabled (TCP stays off
  by default). The §2.2 names are used as proposed unless the user changes them.
- **Q2 (decided): no `com.robotraconteur.*` standard-types layer for now.**

### Open
- None.

## 6. Risks
| Risk | Mitigation |
|---|---|
| Accidental LAN discovery (RR/DDS defaults) | `--network=none`, test helper enforcement, safe default config |
| RR `master` breaks the build | SHA recorded in the image label; report the SHA and failure to the user, and don't silently pin around it |
| Drifting from upstream bridge conventions | Compatibility test in B2, `docs/type_mapping.md` |
| Boost/CRT clashes with the ROS Windows environment | No RR/Boost in public headers, `docs/windows.md` plan, early user MSVC build |
| RR thread starvation or deadlocks with ROS executors | Async-only rule, stress tests, TSan |
| Large-message copy costs | B12 fast path, benchmarks |
| Exposure policy bypass | Deny-by-default, denial-path tests, fuzzing |
