# rmw_intraprocess — project plan

## 1. Goal

A ROS 2 RMW implementation, `rmw_intraprocess_cpp`, in which every entity
(node, publisher, subscription, service, client, guard condition, wait set)
lives in one process and communicates only through in-memory data structures.

Intended deployment: isolated ROS 2 components composed into a single
executable on **Windows 10/11 (MSVC)** and Linux. External communication is
provided by one Robot Raconteur gateway component (`../robotraconteur_ros2_debug_bridge`).

### Non-goals
- Communicating between processes or machines.
- SROS2 / security enclaves (report as unsupported).
- Compatibility with the `ros2` CLI or daemon in another process. The RR
  bridge replaces these.

### Success criteria (project level)
1. `rclcpp` nodes composed in one process work unchanged: pub/sub, services,
   actions, parameters, timers, and the single-threaded, multi-threaded and
   events executors.
2. The upstream RMW, rcl and rclcpp test suites pass, except for documented
   exclusions in `docs/unsupported.md`.
3. Zero sockets are opened (verified by running in `--network=none` and by a
   syscall/symbol check).
4. The code passes the portability check and is ready for an MSVC build.

## 2. Architecture

### 2.1 Package layout (proposed)
```
rmw_intraprocess/
  rmw_intraprocess_cpp/          # the RMW package (ament_cmake)
    include/rmw_intraprocess_cpp/
      visibility_control.h
      extension.hpp              # M9: optional tap API used by the RR bridge
    src/
      registry.*                 # process-global entity registry (single domain)
      context.*, node.*, publisher.*, subscription.*, service.*, client.*
      guard_condition.*, wait_set.*, graph.*, events.*, serialization.*, qos.*
    test/                        # gtest unit tests for internals
  docs/  design.md  unsupported.md  windows.md
  scripts/  build-image.sh  build.sh  test.sh  check-portability.sh
  container/ Containerfile
```
The implementation identifier is `"rmw_intraprocess_cpp"`. Register it in the
ament index the same way existing RMWs do, so `RMW_IMPLEMENTATION=rmw_intraprocess_cpp`
selects it.

### 2.2 Core design
- **Registry.** A process-wide singleton (function-local static) maps
  `topic name → TopicEntry {type name, type hash, publishers,
  subscriptions, transient-local cache}`. It also tracks services and nodes, for
  the graph API. **There is a single domain** (user decision): every context in
  the process shares the registry, and `domain_id` is accepted but ignored.
- **Delivery.** `rmw_publish` asks each matched subscription to store the
  message in its bounded queue (KEEP_LAST depth, or KEEP_ALL with a resource
  limit). The default representation is a type-erased deep copy made with
  `rosidl_typesupport_introspection_cpp`/`_c`. When a serialized subscriber
  exists, or `rmw_publish_serialized_message` is used, the message is CDR
  serialized/deserialized via `rosidl_typesupport_fastrtps_cpp`/`_c`. Serialize
  lazily, at most once per publish.
- **Wait sets.** Each waitable entity keeps a list of attached waiters. `rmw_wait`
  attaches to all entities, checks readiness, then blocks on one
  condition variable with the timeout, and detaches on exit. Avoid lost wakeups
  by checking readiness under the waiter's lock. This is the highest-risk
  component: design it in `docs/design.md` before coding it.
- **Services.** Each client has a unique GID and a sequence counter. Requests
  are queued to the matching service. Responses are routed back by
  `(client GID, sequence number)`.
- **Graph.** Every create/destroy updates the registry and triggers each
  context's graph guard condition. Graph queries read a consistent registry
  snapshot.
- **QoS.** Implement the history, depth, reliability and durability
  compatibility rules exactly as specified in the RMW docs, including the
  incompatible-QoS events. TRANSIENT_LOCAL publishers keep the last N messages and
  deliver them to compatible late-joining subscriptions. Deadline, lifespan and
  liveliness: implement lifespan (it's cheap). Decide deadline/liveliness after
  M3 based on need.
- **Message info.** Fill in source/received timestamps, publication sequence
  numbers and publisher GIDs.
- **Callbacks.** Support `rmw_*_set_on_new_*_callback` for the EventsExecutor.

### 2.3 Locking
Order: registry lock → topic/service entry lock → endpoint queue lock → waiter
lock. Never call user callbacks (on-new-message callbacks) while holding the
registry or entry locks. Record any change to this order in `docs/design.md`.

## 3. Milestones

Each milestone has acceptance criteria (AC) that must pass in the container,
including the sanitizer variants, before it counts as done.

### M0 — Build infrastructure (no RMW code)
- `container/Containerfile` FROM a pinned `ros:jazzy-ros-base` digest. Install
  build deps and the upstream test packages' sources (pinned to the Jazzy tags)
  needed in M7.
- `scripts/build-image.sh` (only step allowed network access).
  `scripts/build.sh` and `scripts/test.sh` run `podman run --rm --network=none`
  with the source mounted `:ro,Z` and named volumes for build/install/log.
- Build variants: `release`, `asan` (ASan+UBSan), `tsan`.
- `scripts/check-portability.sh`: fails on POSIX headers, socket/networking
  APIs (`socket`, `connect`, `bind`, `getaddrinfo`, `asio`), missing visibility
  macros on exported symbols, and GCC-only constructs.
- **AC:** an empty ament package builds and its trivial test passes through the
  scripts. The portability check runs. A smoke test confirms the container has
  no network (DNS lookup fails).

### M1 — Skeleton that loads
- Every `rmw.h` function exists. Unimplemented ones return `RMW_RET_UNSUPPORTED`
  with an error message. `rmw_get_implementation_identifier`, serialization format,
  init options, init/shutdown/fini, node create/destroy, guard conditions.
- Pulled forward from M2 (2026-11-13): publisher/subscription create/destroy,
  actual QoS and GIDs, because rclcpp always creates a `/parameter_events`
  subscription (TimeSource), so `rclcpp::Node` can't be built without them.
  Publish/take, matching and wait sets stay in M2.
- **AC:** `rclcpp::init` + creating a node + shutting down works under
  `RMW_IMPLEMENTATION=rmw_intraprocess_cpp`, and `rmw_implementation` loads the
  library dynamically.

### M2 — Publish/subscribe and wait sets
- Publishers, subscriptions, typed take, message info, wait sets, KEEP_LAST
  queues.
- Implemented with CDR as the in-memory representation (serialize once per
  publish, deserialize per take); see docs/design.md "Message delivery". The
  same-type deep-copy fast path is deferred to M10 benchmarks.
- Pulled forward (2026-11-13), needed by upstream tests and matching:
  `rmw_qos_profile_check_compatible` (M3), `rmw_serialize`/`rmw_deserialize` and
  serialized publish/take (M6). M3 still owns events, TRANSIENT_LOCAL delivery,
  KEEP_ALL limits and lifespan; M6 owns generic pub/sub and rosbag2.
- **AC:** an in-process talker/listener (composed nodes) exchanges 10k messages
  with no loss at the configured depth. Wait set unit tests cover timeouts,
  zero timeout, spurious wakeups, and attach/detach during shutdown. TSan is
  clean.

### M3 — QoS and events
- Compatibility rules, TRANSIENT_LOCAL, KEEP_ALL limits, lifespan, matched
  events, incompatible-QoS events, unsupported-feature reporting.
- Done 2026-11-13. Compatibility rules were implemented in M2. Deadline and
  liveliness are accepted and logged but not enforced, and their events are
  unsupported until Q3 is answered (docs/unsupported.md). KEEP_ALL limit:
  `RMW_INTRAPROCESS_KEEP_ALL_LIMIT`, overflow rejects + MESSAGE_LOST.
- **AC:** `/tf_static`-style latched delivery to late joiners works. The QoS
  compatibility matrix is covered by table-driven tests.

### M4 — Services and clients
- Request/response, `rmw_service_server_is_available`, request header info.
- Done 2026-11-13. Requests go to every matching service; responses are routed
  by client GID. Default `rclcpp::Node` options now work (parameter services,
  type description service).
- **AC:** `rclcpp` add_two_ints-style client/server in one process, plus
  concurrent clients under the multi-threaded executor.

### M5 — Graph API
- Node, topic and service names and types, counts, endpoint info by topic,
  graph guard condition, type hashes.
- Done 2026-11-13. A service name is listed if a server or a client uses it.
  Upstream `test_graph_api` added to the required suites. Cross-platform CI on
  GitHub (CLAUDE.md S6, RoboStack, Windows and Linux) was added alongside.
- **AC:** `rclcpp` `wait_for_service`, `get_topic_names_and_types`, and
  matched-count APIs behave correctly as entities come and go.

### M6 — Serialized messages
- `rmw_serialize`/`rmw_deserialize`, `rmw_get_serialized_message_size`, serialized
  publish/take, generic publishers and subscriptions.
- **AC:** an `rclcpp::GenericSubscription` receives typed publications and
  round-trips them. The rosbag2 recorder and player, run as components in the
  same process, record and replay a topic (if rosbag2 supports composition in
  Jazzy; otherwise document it).
- Done 2026-11-13. Serialize/deserialize and serialized publish/take landed in
  M2. Generic publisher/subscription tests and the rosbag2 test
  (`test_rosbag2_components`: Recorder and Player loaded from the ament index
  via class_loader, mcap storage) run against both RMWs. Allocations and
  `rmw_get_serialized_message_size` stay unsupported, as in `rmw_fastrtps_cpp`.

### M7 — Upstream conformance
- Run `test_rmw_implementation`, the rcl tests, the rclcpp tests, and the relevant
  `system_tests` with this RMW (all `--network=none`).
- **AC:** all pass, or each failure is fixed or recorded in
  `docs/unsupported.md` with the user's approval.
- Done 2026-11-13 (release gate `scripts/check-conformance.sh`): rcl 49/49,
  rclcpp 121/121, system_tests test_rclcpp 24/24, test_quality_of_service 9/9,
  test_communication 25/25, with the approved exclusions (cross-process tests,
  deadline/liveliness cases, linters needing the network). Fixed on the way:
  actual-QoS durations, lease-mismatch policy kind, the new-data callback
  contract, BEST_AVAILABLE adaptation. Upstream defects seen with both RMWs
  (rclcpp exit-order crash, ASan/UBSan findings in rcl/rclcpp) are recorded in
  docs/unsupported.md, pending user review.

### M8 — Higher-level ROS features
- Actions (rclcpp_action), parameters (including parameter events), lifecycle
  nodes, all three executor types, `rclcpp_components` manual composition.
- Components: add the upstream `rclcpp_components` tests (ComponentManager
  load/unload/list over in-process services) to the conformance suites, and load
  the demo components through their component factories (not constructed by
  hand).
- **AC:** a demo executable composes several standard demo components and runs
  them all together.
- Done 2026-11-14. `test_composition_demo` (both RMWs) runs an in-process
  `rclcpp_components::ComponentManager` and loads, through its `load_node`
  service, composition::Talker/Listener/Server/Client,
  demo_nodes_cpp::ParameterBlackboard (with a load-time parameter) and
  action_tutorials_cpp::FibonacciActionServer, checks each from outside
  (chatter, add_two_ints, parameters, an action goal), unloads the talker, then
  loads FibonacciActionClient, whose goal succeeds and whose rclcpp::shutdown()
  ends the demo. New rclcpp tests for executors, actions, parameters (including
  parameter events) and lifecycle are parameterized over all four rclcpp
  executor types (SingleThreaded, StaticSingleThreaded, MultiThreaded, Events)
  and pass with both RMWs. Upstream suites added to the conformance gate:
  rcl_action 16/16 targets (238 cases), rcl_lifecycle 9/9 (83),
  rclcpp_action 13/13 (246), rclcpp_lifecycle 19/19 (206), rclcpp_components
  (70 cases); only package linters are excluded (rclcpp_components' cppcheck
  flags Google Benchmark's header). No RMW change was needed. The demo packages
  (composition, demo_nodes_cpp, action_tutorials_cpp, lifecycle) were added to
  the image and the CI environment.

### M8b — Nav2 integration (deferred by the user, 2026-11-13: "probably isn't worth the effort")
Kept for reference. Nav2 stays the main use case for the bridge.
Nav2 is the main real-world target: the user considers it one of the most
valuable projects for this RMW plus the Robot Raconteur bridge (a separate
session prototyped an RR Python bridge for Nav2). It exercises the RMW broadly:
many topics with mixed QoS (TF, odom, scan, map, costmaps, plans, cmd_vel),
nested actions (NavigateToPose -> ComputePathToPose / FollowPath / behaviors),
about 10-15 lifecycle nodes driven by the lifecycle manager, large parameter
sets, and pluginlib plugins.
- Packages: `navigation2`, `nav2_bringup` from the ROS apt repo
  (Containerfile), and the RoboStack CI environment if available.
- A small C++ fake robot (cmd_vel -> odom, odom->base_link TF, synthetic scan;
  static map->odom instead of AMCL): Gazebo and Nav2's Python loopback
  simulator cannot join the process.
- Nav2's registered components loaded in-process from the ament index with the
  standard Nav2 parameter file (no launch service calls). This prototypes the
  custom component container the user plans.
- Run against both RMWs to separate Nav2/config issues from RMW issues.
- **AC:** in one process, the lifecycle manager brings Nav2 up and a
  NavigateToPose goal on a small map succeeds, with this RMW and with
  `rmw_fastrtps_cpp`.

### M8c — Torture test components (user request, 2026-11-13)
A test-only package (e.g. `rmw_intraprocess_torture`) of composable nodes, each
configured by parameters (rates, payload sizes, counts, duration, random seed),
loaded through their component factories in any mix into one process:
- **Topic flood:** many topics at max rate, payloads from empty to MBs (unbounded
  sequences/strings), C and C++ type supports, typed and generic. Payloads carry
  publisher id, sequence number and checksum: per-publisher order, no
  duplicates, no corruption, loss only where QoS allows it.
- **QoS matrix:** every reliability/durability/history/depth combination, late
  TRANSIENT_LOCAL joiners, lifespan expiry, KEEP_ALL overflow; exact expected
  delivery plus QoS/message-lost events.
- **Entity churn:** threads creating/destroying publishers, subscriptions,
  services, clients, timers, guard conditions and nodes during traffic, with
  concurrent graph queries; no crash/deadlock, graph and counts converge.
- **Service storm:** many concurrent clients, several servers per name, client
  or server destroyed mid-request; responses reach the right client only.
- **Action storm:** many goals, cancels, feedback floods, results after the
  client is gone; consistent goal states, no leaks.
- **Parameter and lifecycle churn:** rapid parameter set/get across nodes
  (parameter events in order), lifecycle transition loops.
- **Re-entrant/blocking callbacks:** callbacks that publish, create entities,
  destroy themselves, block or throw; no deadlock, callback rules hold.
- **Shutdown torture:** rclcpp/context shutdown during all of the above, and
  repeated re-init; clean exit, no use-after-free or leaks.
Run under the SingleThreaded, MultiThreaded and Events executors, with
intra-process comms on and off, and across contexts.
- Harness: a gtest driver loads a mix, runs for a fixed time, collects each
  component's stats via a results service, and asserts invariants and counts
  (never timing). Short mode (~30 s) in the gate (release + ASan, both RMWs
  where semantics match); long soak mode on demand. Failures report their seed.
- Most valuable with TSan, which runs in CI (locally disabled, docs/unsupported.md);
  the CI TSan job runs the torture driver too.
- The components are reusable in the user's custom container and next to the
  RR bridge for soak tests. They form the core of M10's stress testing.
- **AC:** the short mode passes in the gate on both RMWs (release + ASan) and in
  CI; a 30-minute soak passes on this RMW.
- Stage 1 (2026-11-14, branch `m8c-torture`): package `rmw_intraprocess_torture`
  (own Payload/GetStats interfaces) with TopicFlood (typed and generic,
  reliable and best effort, up to 2 MB payloads), EntityChurn and
  ServiceStorm, and `test_torture` (ComponentManager + load_node; all four
  executors, intra-process comms off/on). Passes on this RMW in release and
  ASan. Deviations: (1) rmw_fastrtps_cpp fails the invariants in this mix
  (lost RELIABLE samples, timeouts, hang), so the driver runs on this RMW only;
  (2) the short mix bounds every workload (rclcpp's StaticSingleThreaded
  executor starves/livelocks under endless load); (3) churn uses private nodes
  per thread and EntityChurn runs without intra-process comms, because of two
  rclcpp/rcl defects. All recorded in docs/unsupported.md, pending user review.
  TSan now runs in CI. Stage 2: QoS matrix, action storm, parameter and
  lifecycle churn, re-entrant callbacks, shutdown torture, soak mode.
- Done 2026-11-14 (stage 2): ActionStorm, ParamChurn, LifecycleChurn,
  ReentrantCallbacks and QosMatrix components; `ShutdownDuringMix`
  (rclcpp::shutdown() during the busy mix, re-init per test); soak mode
  `scripts/soak.sh [variant] [minutes]`. AC: `test_torture` (16 tests: 8
  executor/intra-process configurations x ShortMix and ShutdownDuringMix)
  passes in the gate (release + ASan) and in CI (Windows, Linux, TSan); the
  30-minute soak passed on this RMW (41 iterations, seeds 1001-1041).
  Deviations from the plan, each recorded in docs/unsupported.md (pending
  user review): the driver runs on this RMW only (rmw_fastrtps_cpp fails the
  mix); every workload of the short mix is bounded; callbacks never throw
  (rclcpp propagates the exception out of spin()); C type support is not
  flooded directly (it is exercised through rcl_action, rcl_lifecycle and the
  rcl conformance suites); the shutdown test stops the action storms and the
  entity churn first; EntityChurn and LifecycleChurn run without
  intra-process comms. Upstream defects found: rclcpp IntraProcessManager
  self-deadlock (abort or hang) on concurrent publisher destruction; rcl's
  per-node and process-wide state not thread-safe against concurrent entity
  init/fini and shutdown; StaticSingleThreaded executor starvation;
  rclcpp_action throwing from the executor after shutdown (aborts on a
  MultiThreadedExecutor worker); rclcpp_action result-before-accept race.

### M9 — Extension "tap" API for the RR bridge
- `extension.hpp`: a stable C++ API that lets an in-process gateway observe
  topics and get type-erased message pointers plus their introspection type
  support, so it can skip the CDR round trip. Keep it small and versioned.
- **AC:** a unit test observes messages through the tap. The bridge repo builds
  and works without the tap (feature-detected).
- Done 2026-11-14 (branch `m9-tap`). `extension.h`: versioned C ABI
  (`rmw_intraprocess_cpp_get_extension_api(version)` -> table with `add_tap`,
  `remove_tap`); `extension.hpp`: header-only `find_api()` (checks
  `rmw_get_implementation_identifier()`, looks the entry point up in the loaded
  RMW library: no link, so a client builds and runs with any RMW) and an RAII
  `Tap`. A tap gets every publication of a topic synchronously in
  rmw_publish: the native message plus its introspection members (no CDR round
  trip), and the CDR buffer and metadata. Deviation: the header is
  `extension.h` (C ABI, stable across compilers) plus `extension.hpp`, rather
  than a C++-only `extension.hpp`. AC: `test_tap` (unit) and
  `test_rclcpp_tap` (run-time discovery, rclcpp publications read through
  introspection) pass; with rmw_fastrtps_cpp `find_api()` returns null (the
  feature-detection path the bridge uses). The bridge side lands with the
  bridge milestones.

### M10 — Hardening and Windows readiness
- Benchmarks (latency/throughput vs `rmw_fastrtps_cpp` intra-process, in the
  container). Fuzz/stress tests for create/destroy races. Write
  `docs/windows.md`: MSVC build instructions for the user, the checklist of
  portability rules, and known risks.
- **AC:** no regressions across all variants. The user confirms an MSVC build
  on their Windows machine (Claude can't do this step).

## 4. Testing strategy
- Unit tests (gtest) for registry, queues, QoS rules and wait sets.
- Integration tests using rclcpp with composed nodes in one test binary.
- Upstream suites (M7).
- Sanitizers: ASan/UBSan and TSan for every milestone.
- Network check: every test runs under `--network=none`. Add a test that
  fails if the RMW library imports socket symbols (`nm`/`objdump` check).

## 5. Decisions and open questions

### Decided (user, 2026-11-13)
- ROS 2 **Jazzy only**.
- **Single domain.** Domain IDs are ignored, and all contexts share one registry.
- Cross-testing (Windows, real networks via the bridge) will use a GitHub repo
  the user creates once a prototype works. Claude doesn't push or add
  remotes.
- **License (was Q5): Apache-2.0**, `Copyright 2026 Wason Technology, LLC` headers;
  ament_copyright enabled.
- **package.xml without the `<?xml-model?>` schema line**: xmllint can't fetch the
  schema under `--network=none`; well-formedness only.
- **TSan disabled for now** (see `docs/unsupported.md`). ASan/UBSan still required.
- **Q3 (decided): deadline/liveliness are not important for now.** They stay
  accepted-but-unenforced, and their events stay unsupported.
- **Q4 (decided): no loaned messages for now.** Focus on getting it running;
  performance later.
- **Serialization is for completeness (user, 2026-11-13).** In most
  applications rclcpp's intra-process manager (`use_intra_process_comms`) passes
  smart pointers and data never reaches the RMW's CDR path. The RMW still
  serializes correctly for everything else (non-IPC nodes, other contexts,
  services, generic pub/sub, rosbag2, the bridge), but that path doesn't need
  optimizing. What must be right is the cooperation with rclcpp IPC:
  `ignore_local_publications` per context and matched counts.
- **Q6 (decided): RoboStack** is the Windows ROS environment for
  `docs/windows.md` (MSVC version follows RoboStack's conda-forge toolchain).
- **Upstream ASan leak:** the scoped LSan suppression for the upstream
  `check_qos` tests is accepted ("suppress and note").
- **Merging:** milestone branches are merged into `main` at Claude's discretion
  (local only, never pushed).
- **system_tests tag** for M7: Claude's discretion (pin the Jazzy tag matching
  the image).

### Open
- None.

## 6. Risks
| Risk | Mitigation |
|---|---|
| Wait-set races or lost wakeups | Design doc first, TSan, stress tests |
| RMW API drift | Jazzy only, base image pinned by digest |
| Windows-only failures (DLL boundaries, static init) | Portability check, design rules, user MSVC builds at M1, M5, M10 |
| Upstream tests that assume multiple processes | Document in `unsupported.md` with user approval |
