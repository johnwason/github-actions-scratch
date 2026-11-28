# Shut-down nodes are never freed: queued handlers and a late cleanup timer hold them

Status (2026-11-27): **resolved upstream; kept for the record.** Written against Robot
Raconteur core at `7cd1610c519613f241b25453754fb118b270cdaf`; rechecked at robotraconteur PR
503's head `22c20c146412c8654ec115e80c14f5a762a18322`:

- **Fixed in core:** `Post` and `TryPost` bind a `weak_ptr` to the node; `StartPeriodicCleanupTask`
  returns when the node is shut down (the second change below); `Shutdown()` clears the member
  `threads`.
- **Handled by design, not by a drain:** `ThreadPool::Shutdown()` still leaves unrun handlers in
  `_io_context`, but RR's objects (clients, connectors, discovery searches, subscriptions)
  release themselves from node shutdown listeners (`RobotRaconteurNode::AddShutdownListener`),
  which run before the pool stops. Code that posts work holding the node strongly is expected to
  do the same. The first change below is therefore not needed.
- **Evidence:** with the binding's workaround removed (RR's own pool, plain `Init()`), every test
  passes under ASan and TSan (scratch CI run 46084375403), `teardown_stress` included; the only
  leak left was `node_release`'s own handlers, which hold the node strongly on purpose.

## Problem

`ThreadPool::Shutdown()` (`RobotRaconteurCore/src/ThreadPool.cpp`) sets `keepgoing = false`,
calls `_io_context.stop()`, and joins the pool threads. Handlers that were posted but had not
run yet stay queued in `_io_context`, which is a member of the pool. Many of them hold a
`shared_ptr<RobotRaconteurNode>`:

- `ThreadPool::Post` and `TryPost` bind `GetNode()` (a strong reference), although
  `ThreadPool_post_wrapper` only needs a `weak_ptr`.
- `RobotRaconteurNode::InitThreadPool` posts `StartPeriodicCleanupTask` bound to
  `shared_from_this()`.
- `IntraTransport::StartServer` → `SendNodeDiscovery` → `NodeDetected` posts discovery handlers
  to the pools of other intra-process nodes.

`RobotRaconteurNode::Shutdown()` shuts the pool down but keeps `thread_pool`, which is released
only in the node's destructor. So node → pool → `_io_context` → handler → node is a cycle: the
node, its pool, its threads and its parsed service definitions are never freed.

There is also a smaller issue in the same function: the local `threads` copy shadows the member,
so `threads.clear()` clears the copy and `this->threads` keeps the `boost::thread` objects.

A second, related race keeps a node alive even with the fix below. `InitThreadPool` posts
`StartPeriodicCleanupTask` once, and that task never checks `is_shutdown`. `Shutdown()` cancels
`PeriodicCleanupTask_timer` before it shuts down the pool. If the task runs after that, on a pool
thread before `stop()` or while the remaining handlers run, it arms a new 5-second timer bound to
the node, which the node itself then holds.

This matters for any process that creates and shuts down nodes repeatedly. Embedded hosts do
this, for example a simulator that recreates its scripting state on every simulation restart.

## Evidence

- LeakSanitizer, on a GitHub `ubuntu-24.04` runner, in robotraconteur_lua's `teardown_stress`
  (20 iterations × 4 threads, each creating a node, registering an `IntraTransport`, starting
  its server, and shutting down): 3 of 80 nodes leaked, 61,565 bytes in 467 allocations, all
  indirect (a cycle). The leaked allocations include
  `executor_op<binder0<bind_t<..., value<shared_ptr<RobotRaconteurNode>>>>>` handlers from
  `ThreadPool::TryPost` via `IntraTransport::NodeDetected`.
- A deterministic reproduction, `tests/node_release/node_release.cpp`: post 200 slow handlers
  holding the node to a 2-thread pool, call `Shutdown()`, release the node. With RR's pool
  (`node_release stock`), 30 of 30 nodes are not released. With only the first change below
  (applied as a subclass), 0 to 3 of every 10 still leak through the cleanup-timer race. With
  both (the second emulated by making sure the cleanup task has run before shutdown), 0 of 180
  over six runs.

## Proposed change

After joining the threads, run the remaining handlers on the calling thread so that they and
everything they hold are released. RR's handlers already cope with a shut-down node: this is the
state they would have seen had the pool run them a moment earlier. Also clear the member
`threads`.

An alternative is to destroy the queued handlers without running them, but `io_context` offers
that only through its destructor, which the cycle prevents. Binding a `weak_ptr` in `Post` and
`TryPost` would be good to do as well, but it is not enough by itself, because posted functions
such as `StartPeriodicCleanupTask` hold strong references themselves.

```diff
--- a/RobotRaconteurCore/src/ThreadPool.cpp
+++ b/RobotRaconteurCore/src/ThreadPool.cpp
@@ -163,6 +163,27 @@
 
         boost::mutex::scoped_lock lock(queue_mutex);
         threads.clear();
+        this->threads.clear();
+    }
+
+    // Handlers still queued after stop() would stay in _io_context, and many hold a
+    // shared_ptr to the node, which holds this pool: node, pool and threads leak. Run them
+    // now (no pool thread is left and Post/TryPost refuse new work) so they are released.
+    _io_context.restart();
+    for (int pass = 0; pass < 1000; ++pass)
+    {
+        size_t n = 0;
+        try
+        {
+            n = _io_context.poll();
+        }
+        catch (std::exception& exp)
+        {
+            RobotRaconteurNode::TryHandleException(node, &exp);
+            n = 1;
+        }
+        if (n == 0)
+            break;
     }
 }
 
```

And make the cleanup task respect shutdown. `Shutdown()` sets `is_shutdown` under
`thread_pool_lock`, releases it, and only later takes `PeriodicCleanupTask_timer_lock`, so taking
`thread_pool_lock` inside the timer lock here cannot deadlock:

```diff
--- a/RobotRaconteurCore/src/RobotRaconteurNode.cpp
+++ b/RobotRaconteurCore/src/RobotRaconteurNode.cpp
@@ -2573,6 +2573,14 @@
 void RobotRaconteurNode::StartPeriodicCleanupTask(const RR_SHARED_PTR<RobotRaconteurNode>& node)
 {
     boost::unique_lock<boost::shared_mutex> lock(node->PeriodicCleanupTask_timer_lock);
+    {
+        // Shutdown() sets is_shutdown before it cancels this timer under
+        // PeriodicCleanupTask_timer_lock; a timer armed after that would never be cancelled
+        // and would keep the node alive.
+        boost::shared_lock<boost::shared_mutex> lock2(node->thread_pool_lock);
+        if (node->is_shutdown)
+            return;
+    }
     node->PeriodicCleanupTask_timer =
         node->CreateTimer(boost::posix_time::seconds(5),
                           boost::bind(&RobotRaconteurNode::PeriodicCleanupTask, node, RR_BOOST_PLACEHOLDERS(_1)));
```

## Workaround in this project (removed 2026-11-27)

`rl::ThreadPool` subclassed `ThreadPool` and drained `_io_context` after the base shutdown, and
`rl::init_node` waited until the cleanup task had run before returning. Both, and the
`node_release` test, were removed once the pinned core no longer needed them.
