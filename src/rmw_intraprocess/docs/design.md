# rmw_intraprocess design

Status: M4 (services and clients). Sections are filled in as milestones land.

## Entities and ownership

| rmw handle | `data` points to | Owned by | Notes |
|---|---|---|---|
| `rmw_context_t` | `impl` = `rmw_context_impl_s` (`src/context.hpp`) | caller; `impl` freed in `rmw_context_fini` | `is_shutdown`, live node count. `rmw_context_fini` refuses while nodes exist. |
| `rmw_node_t` | `NodeImpl` (`src/node.hpp`) | `rmw_create_node` / `rmw_destroy_node` | Owns its name/namespace strings and its graph guard condition. |
| `rmw_guard_condition_t` | `GuardCondition` (`src/guard_condition.hpp`) | create/destroy pair, or the node (graph GC) | M1: atomic flag. M2 adds waiter attachment. |
| `rmw_publisher_t` / `rmw_subscription_t` | `PublisherImpl` / `SubscriptionImpl` (`src/endpoint.hpp`) | create/destroy pair | Resolved introspection type support, resolved QoS, GID. M2 adds queues and delivery. |

Handles are allocated with `new`; failures map to `RMW_RET_BAD_ALLOC` and no
C++ exception crosses the C API.

## Registry
`Registry::instance()` (`src/registry.hpp`) is a function-local static pointer to
a registry that is **never destroyed**. That avoids static destruction order
problems across DLLs (Windows rule) and keeps late `rmw_destroy_*` calls during
process exit safe.

There is a **single domain**: every context in the process shares the registry,
and `domain_id` is reported back in `actual_domain_id` but never used for
matching.

The registry holds all nodes and a map `topic name → TopicEntry {publishers,
subscriptions}`. Endpoints on one topic may have different types; matching (M2)
requires equal type names. Any node or endpoint change triggers the graph guard
condition of every node, in every context.

GIDs come from a process-wide counter (first 8 bytes of `rmw_gid_t::data`).

## Type support
`resolve_message_type_support` (`src/type_support.cpp`) asks the given handle for
`rosidl_typesupport_introspection_cpp` first, then `_c`, and records the ROS type
name (`pkg/msg/Type`) from the introspection namespace and name. A type without
introspection support is rejected at endpoint creation.

## QoS
`resolve_qos` (`src/qos.cpp`) replaces SYSTEM_DEFAULT / BEST_AVAILABLE / UNKNOWN:
KEEP_LAST (depth 0 → 1), RELIABLE, VOLATILE, AUTOMATIC. This is what
`*_get_actual_qos` reports. Compatibility rules and BEST_AVAILABLE matching are M3.

## Locking order
Registry lock → topic/service entry lock → endpoint queue lock → waiter lock.
Never call user callbacks (on-new-message callbacks) while holding the registry
or entry locks. Any change to this order must be recorded here.

M2 concretely:
- Registry lock: held only to read/modify the topic map and node list and to
  snapshot matched endpoints. Graph guard condition triggers happen under it
  (guard condition lock → wait set lock nest inside it, consistent with the
  order).
- Entity (queue / guard condition) lock → wait set lock: `notify_locked()`.
- `rmw_wait` never holds a wait set lock while taking an entity lock.
- Subscription callback mutex: held only while calling the user callback, with
  no other lock held.
- Callback mutexes (subscription, event) may take their own entity lock (to read
  unread counts when a callback is set), never the reverse.
- Taps (M9): `TapRegistry::mutex_` and each tap's mutex are independent of
  all the locks above and never nested with them; tap callbacks run with no
  RMW lock held (see "Extension API: taps").


## Message delivery (M2)

**Representation: CDR, serialized once per publish.** `rmw_publish` serializes
the message with the rosidl_typesupport_fastrtps (C or C++) callbacks into one
immutable buffer shared (`std::shared_ptr`) by every matched subscription.
`rmw_take` deserializes it with the *subscription's* type support. CDR is
layout independent, so a C publisher (rclpy, rcl) and a C++ subscription of the
same type interoperate, and serialized publish/take (M6) is the same path
without the conversion.

Deviation from PLAN §2.2 (which proposed an introspection deep copy as the
default): the deep-copy fast path for publisher and subscription with the
*same* type support is deferred until benchmarks (M10) show it matters. rclcpp's
intra-process manager already gives zero-copy for rclcpp-to-rclcpp when enabled.

A sample is `{shared CDR buffer, publisher GID, source timestamp, publication
sequence number}`; delivery adds the received timestamp and the subscription's
reception sequence number.

**Matching (M2 subset):** same topic name, same ROS type name, and QoS
compatible in reliability (BEST_EFFORT pub + RELIABLE sub do not match) and
durability (VOLATILE pub + TRANSIENT_LOCAL sub do not match). Remaining rules
and events are M3. `ignore_local_publications` skips publishers in the same
*context*, which is the scope of rclcpp's intra-process manager, so a message is
never delivered twice.

**Queues:** each subscription has a FIFO. KEEP_LAST drops the oldest sample at
`depth`. KEEP_ALL is unbounded until M3 adds resource limits. Delivery is
synchronous inside `rmw_publish`, so `rmw_publisher_wait_for_all_acked` returns
immediately.

**Ownership for concurrent publish/destroy:** the registry owns each
`SubscriptionImpl` through a `std::shared_ptr`. `rmw_publish` copies the matched
subscriptions (shared_ptrs) under the registry lock, releases it, then
delivers. A subscription destroyed during a publish stays alive until that
publish finishes; it is marked `destroyed` under its queue lock and ignores
late deliveries. Publishers are plain pointers in the registry (only matching
reads them, under the registry lock).

## Wait sets (M2)

Every waitable entity (guard condition, subscription; services, clients and
events in M3/M4) derives from `Waitable`: an entity mutex plus the list of wait
sets currently attached. A `WaitSetImpl` has its own mutex, condition variable
and `triggered` flag.

`rmw_wait` protocol:
1. Reset `ws.triggered` (under the wait set lock).
2. **Attach** to every entity (entity lock; push `ws`).
3. Loop:
   1. Check readiness of every entity (entity lock each; no wait set lock held).
      Ready = subscription queue non-empty, guard condition triggered.
   2. If something is ready, or the timeout is zero, stop.
   3. Lock the wait set; wait on its condition variable until `triggered` or
      the deadline; reset `triggered`; unlock. On timeout, stop.
4. **Detach** from every entity.
5. Final pass: null out entries that are not ready. Guard conditions are
   *consumed* here (`take_triggered`); subscriptions are not (taking does that).
6. Return `RMW_RET_OK` if anything was ready, otherwise `RMW_RET_TIMEOUT`.

An entity that becomes ready does so under its entity lock, then calls
`notify_locked()`, which for each attached wait set locks the wait set mutex,
sets `triggered` and notifies its condition variable. **No lost wakeups:** an
event before step 2 is seen by the readiness check; an event after step 2 sets
`triggered`, which the wait in 3.3 checks under the wait set lock before
blocking. Spurious wakeups and stolen readiness (another thread took the
sample) just loop. A timeout of `nullptr` waits forever; very large timeouts
(`RMW_DURATION_INFINITE`) are treated as infinite to avoid overflow.

Contract (same as other RMWs): an entity must not be destroyed while a wait on
it is in progress; a wait set must not be used by two `rmw_wait` calls at once.

rcl passes `rmw_*_t::data` pointers in the subscription, guard condition,
service and client arrays, and `rmw_event_t *` in the events array.

## QoS behavior (M3)

- **Compatibility** (`check_qos_compatible`, also `rmw_qos_profile_check_compatible`)
  follows rmw_dds_common: errors for BEST_EFFORT pub + RELIABLE sub, VOLATILE pub +
  TRANSIENT_LOCAL sub, sub deadline / lease shorter than (or finite vs. infinite)
  the publisher's, AUTOMATIC pub + MANUAL_BY_TOPIC sub; warnings when a side is
  SYSTEM_DEFAULT/UNKNOWN (including both sides). BEST_AVAILABLE is compatible with
  anything. Matching uses the same function on the *resolved* profiles.
- **TRANSIENT_LOCAL:** a TRANSIENT_LOCAL publisher keeps its last `depth`
  samples (KEEP_ALL: the KEEP_ALL limit) in `PublisherImpl::history`, guarded by
  the registry lock. `prepare_publish` appends to the history and snapshots the
  targets in one registry critical section; `add_subscription` delivers the
  history to a matched TRANSIENT_LOCAL subscription in the critical section that
  makes it visible. A late joiner therefore gets every sample exactly once, in
  order per publisher. VOLATILE subscriptions get no history.
- **Lifespan:** samples carry an expiry (source timestamp + publisher lifespan).
  Expired samples are purged on take, on readiness checks and from history
  replay, so they never make a wait set ready.
- **KEEP_ALL limit:** `RMW_INTRAPROCESS_KEEP_ALL_LIMIT` (default 100000) samples
  per subscription queue. A full KEEP_ALL queue rejects the new sample and
  raises MESSAGE_LOST. Blocking the publisher (DDS RELIABLE KEEP_ALL) is avoided
  on purpose: the publisher may run on the executor thread that drains the queue.
- **Deadline and liveliness** are accepted, reported in the actual QoS, used for
  compatibility, and logged as not enforced. Their events are unsupported
  (PLAN.md Q3).
- **BEST_AVAILABLE (M7)** adapts to the endpoints already on the topic when the
  endpoint is created (`adapt_best_available_qos`, rmw_dds_common's rules,
  implemented here so the RMW links no DDS library). A subscription takes
  RELIABLE / TRANSIENT_LOCAL / MANUAL_BY_TOPIC only if every existing publisher
  has it, and the largest finite deadline/lease. A publisher is RELIABLE and
  TRANSIENT_LOCAL, MANUAL_BY_TOPIC if any subscription is, and takes the
  smallest finite deadline/lease. Later endpoints do not change it (as in DDS).
- **Actual QoS (M7)** reports default deadline, lifespan and lease as
  `RMW_DURATION_INFINITE`, and a lease mismatch as policy LIVELINESS, as the DDS
  RMWs do (rcl test_get_actual_qos / test_events).

## Events (M3)

Supported: PUBLICATION_MATCHED, SUBSCRIPTION_MATCHED, OFFERED/REQUESTED_QOS_INCOMPATIBLE
(with the first offending policy), PUBLISHER/SUBSCRIPTION_INCOMPATIBLE_TYPE (same
topic, different type), MESSAGE_LOST (KEEP_ALL limit).

Each endpoint owns one `EventState` per supported type (created with the
endpoint, `shared_ptr`). `rmw_*_event_init` points `rmw_event_t::data` at it, so
`rmw_event_fini` (in librmw) has nothing to free. Status counters accumulate from
endpoint creation; `rmw_take_event` always reports `taken = true` and resets the
`*_change` fields. An `EventState` is a `Waitable`, ready while it has unread
changes.

The registry records changes for both sides of every publisher/subscription pair
on a topic when an endpoint appears (and matched-count decrements for the
survivor when one disappears), under the registry lock, and calls the user
callbacks after releasing it. Callback lifetime rules are the same as for
new-message callbacks.

## Services and clients (M4)

`ServiceImpl` and `ClientImpl` (`src/service.hpp`) are `MessageQueue`s (a Waitable
FIFO with a new-data callback, same rules as subscriptions) owned by the registry
via `shared_ptr`. CDR comes from the fastrtps service type support's request and
response message callbacks, so C and C++ endpoints interoperate.

- A client and a service match on equal service name and service type name.
  QoS is stored and reported but not used for matching.
- `rmw_send_request` assigns the next per-client sequence number and delivers to
  every matching service (as DDS RMWs do); with no service the request is dropped.
- `rmw_send_response` routes by `request_id.writer_guid` (= client GID) to the
  client with that GID on the service name; if it is gone, the response is
  dropped and `RMW_RET_OK` is returned.
- Queues honor KEEP_LAST depth (the default services QoS is KEEP_LAST(10), so a
  burst of requests can drop some, as on any RMW) and the KEEP_ALL limit.

## Graph API (M5)
- One graph for the whole process (single domain): every query sees every node
  and endpoint in every context. Queries take a snapshot under the registry
  lock, then copy it into the caller's rmw/rcutils structures after the lock is
  released (`rmw_graph.cpp`).
- Names are stored as ROS names, so `no_demangle`/`no_mangle` have no effect.
- `rmw_get_topic_names_and_types` lists publishers and subscriptions. Services
  are not topics. `rmw_get_service_names_and_types` lists a service name if a
  server **or** a client uses it, like the DDS RMWs. Types per name are sorted
  and unique.
- The `*_by_node` queries match node name and namespace and return
  `RMW_RET_NODE_NAME_NON_EXISTENT` if no such node exists (checked last, after
  argument validation, matching the upstream tests).
- `rmw_count_*` must not allocate (upstream `EXPECT_NO_MEMORY_OPERATIONS`).
  The registry maps use `std::less<>`, so a lookup by `const char *` builds no
  `std::string`.
- Endpoint info reports the node, type name, RIHS type hash (from the type
  support's `get_type_hash_func`), GID and the resolved QoS.
- Graph guard conditions fire on every add/remove (see Registry).

## New-message callbacks (EventsExecutor)
Subscriptions, service/client queues and events share one implementation,
`NewDataCallback` (new_data_callback.hpp), of the contract the DDS RMWs follow
and rclcpp's on_new_*_callback tests check (fixed in M7):
- each new item is reported with count 1 when it arrives;
- items that arrive while no callback is set are counted; that count is
  reported once when a callback is set, then reset (rclcpp sets each callback
  twice, temporary then permanent, so it must not be repeated);
- taking items does not change the count; TRANSIENT_LOCAL history delivered to
  a new subscription counts as arrived without a callback.

Calls happen *after* the queue lock is released and outside the registry lock.
The recursive callback mutex is held while calling and taken when the callback
is changed or the entity destroyed, so a callback never runs after it was unset
(a callback may re-enter the API on its own thread).

## Extension API: taps (M9)
`include/rmw_intraprocess_cpp/extension.h` is a versioned C ABI for an
in-process gateway (the Robot Raconteur bridge):
`rmw_intraprocess_cpp_get_extension_api(version)` returns a static table of
function pointers (`version`, `struct_size`, `add_tap`, `remove_tap`). Later
versions only append members. Clients do not link this library: they check
`rmw_get_implementation_identifier()` and look the entry point up in the RMW
library rmw_implementation loaded (`extension.hpp`: `find_api()` and an RAII
`Tap`), so they build and run unchanged with any RMW and fall back to ordinary
subscriptions when `find_api()` returns null.

A tap observes every publication on one topic (or all topics), process-wide,
without being a graph entity or a subscription (no QoS, no queue, no matching).
`publish_cdr` (`src/rmw_publisher.cpp`) calls the taps synchronously on the
publishing thread after delivering the sample to the subscriptions, with no RMW
lock held, passing the publisher's native message (null for a serialized
publish) with its introspection MessageMembers and identifier (same language
as the message: a C publisher's dispatch handle only resolves C type
supports), the shared CDR buffer, the publisher GID, source timestamp and
sequence number. Everything is valid only during the callback. With no taps
registered the cost is one atomic load per publish.

Locking (`src/tap.hpp`): `TapRegistry::mutex_` guards the tap map; a publish
snapshots the matching taps under it and calls them without it. Each tap counts
calls in progress under its own mutex; `remove_tap` unregisters, then waits
until none is running, except the caller's own call when it removes a tap from
inside that tap's callback (tracked per thread). The tap registry is
independent of the registry, and no registry lock is held while a tap runs, so
a callback may publish. Exceptions from a callback are contained (C ABI).

## Testing layout
- Unit tests link `rmw_intraprocess_cpp_test_static`, a static build of the same
  sources, so they can inspect internal classes. Each test process therefore has
  exactly one registry.
- `test_symbols` loads the shipped shared library and checks that every function
  `rmw_implementation` resolves is exported.
- `test_rclcpp_smoke` does not link the library. It goes through
  `rmw_implementation` with `RMW_IMPLEMENTATION=rmw_intraprocess_cpp`.
- `scripts/upstream-test.sh` builds the pinned upstream `test_rmw_implementation`
  and runs the suites listed in `scripts/upstream-tests.txt` (part of `check-all.sh`).
- rclcpp-level tests (`test_rclcpp_*`, `test_rosbag2_components`,
  `test_composition_demo`) run against both this RMW and `rmw_fastrtps_cpp` as
  a reference. The M8 tests (executors, actions, parameters, lifecycle) are
  parameterized over every rclcpp executor type (SingleThreaded,
  StaticSingleThreaded, MultiThreaded, Events) via `test/rclcpp_test_utils.hpp`.
  `test_composition_demo` runs an in-process `rclcpp_components::ComponentManager`
  and loads the standard demo components (composition, demo_nodes_cpp,
  action_tutorials_cpp) through its `load_node` service.
- `scripts/check-conformance.sh [variant...]` (M7, M8) builds and runs the pinned
  upstream rcl, rcl_action, rcl_lifecycle, rclcpp, rclcpp_action,
  rclcpp_lifecycle, rclcpp_components and system_tests (test_rclcpp,
  test_quality_of_service, test_communication) suites with this RMW, in
  separate build/install bases.
  The approved exclusions (cross-process tests, deadline/liveliness cases,
  package linters needing the network) are listed in
  `scripts/in-container/conformance.sh` and `docs/unsupported.md`.
- Torture tests (M8c): the test-only package `rmw_intraprocess_torture` has
  composable torture components (TopicFlood, EntityChurn, ServiceStorm,
  ActionStorm, ParamChurn, LifecycleChurn, ReentrantCallbacks, QosMatrix),
  each with a `~/get_stats` service. `test_torture` loads a mix into an
  in-process ComponentManager through `load_node`, under every executor type
  with intra-process comms off and on, and checks invariants and counts, never
  timing (`ShortMix`), plus `rclcpp::shutdown()` during the mix
  (`ShutdownDuringMix`). `TORTURE_SEED`, `TORTURE_RUN_MS` and `TORTURE_ONLY`
  select seed, run time and a subset; `scripts/soak.sh [variant] [minutes]`
  loops it with a new seed per iteration. Rules learned from it: each node's
  entities are created and destroyed by one thread (rcl), and every workload
  in the short mix is bounded (StaticSingleThreaded executor). Upstream
  defects it found are in docs/unsupported.md.
- Cross-platform CI (Windows + Linux, RoboStack): `scripts/push-ci.sh`, see
  CLAUDE.md S6.
