// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// The thread pool every node created by this binding uses (set through
// RobotRaconteurNode::SetThreadPoolFactory before Init).
//
// Why: RR core's ThreadPool::Shutdown() stops its io_context and joins its threads, but
// handlers still queued in that io_context stay there. Many of them hold a strong
// shared_ptr to the node (ThreadPool::Post binds GetNode(); InitThreadPool posts
// StartPeriodicCleanupTask bound to shared_from_this()), and the node holds the pool, so
// the node, its pool, its threads and its service definitions leak. Found by the M0.5
// ASan/LSan run of teardown_stress on GitHub. See
// docs/upstream-proposals/threadpool-shutdown-pending-handlers.md.
//
// This pool runs the base shutdown, then drains the io_context on the calling thread so
// every queued handler runs (RR's handlers already cope with a shut-down node) and is
// destroyed, releasing what it holds.
#pragma once

#include <RobotRaconteur.h>

namespace rl
{

class ThreadPool : public RobotRaconteur::ThreadPool
{
  public:
    explicit ThreadPool(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node);
    void Shutdown() RR_OVERRIDE;
};

class ThreadPoolFactory : public RobotRaconteur::ThreadPoolFactory
{
  public:
    RR_SHARED_PTR<RobotRaconteur::ThreadPool> NewThreadPool(
        const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node) RR_OVERRIDE;
};

// Initialize a node the binding created: install ThreadPoolFactory, Init(), create the
// thread pool, and wait until the pool has run everything posted so far.
//
// Why the wait: RR posts StartPeriodicCleanupTask once, when the pool is created, and that
// task does not check whether the node is shut down. RobotRaconteurNode::Shutdown cancels the
// cleanup timer before it shuts the pool down, so if the task runs after that point it arms a
// new timer bound to the node, which the node then holds: a cycle no pool drain can break.
// Making sure the task has run before init_node returns closes that window.
//
// Throws if the pool does not quiesce within timeout_ms.
void init_node(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node, int timeout_ms = 10000);

} // namespace rl
