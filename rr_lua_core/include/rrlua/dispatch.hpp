// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Per-lua_State dispatch queue (CLAUDE.md D6, D8). RR threads post tasks; only the thread
// that calls rr.step() runs them, with the lua_State. Tasks never touch Lua anywhere else,
// and a task's destructor must not touch Lua either: a task can be destroyed on an RR
// thread (when posted after close) or during state teardown.
//
// RR code holds the queue through a shared_ptr (or weak_ptr), so posting after the state
// has closed is safe: close() makes later posts no-ops.
#pragma once

#include "rrlua/lua_api.hpp"

#include <boost/chrono.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

namespace rl
{

class Task
{
  public:
    virtual ~Task() = default;
    // Runs on the Lua thread inside rr.step. May throw; the error propagates out of rr.step
    // as a Lua error after the task is destroyed.
    virtual void run(lua_State* L) = 0;
};

class DispatchQueue
{
  public:
    // Thread-safe. Returns false (and destroys the task) if the queue is closed.
    bool post(std::unique_ptr<Task> task);

    // Lua thread only. Removes the oldest task, or returns null.
    std::unique_ptr<Task> pop();

    // Thread-safe. Refuse further posts and destroy every queued task.
    void close();

    bool closed() const;
    std::size_t size() const;

    // Total tasks ever accepted; for tests and diagnostics.
    std::size_t posted_total() const;

    // Waiting on the Lua thread for a task or a completion (blocking calls pump the queue).
    // Read generation(), check the condition, then wait(gen, ...): a wake() or post() in between
    // changes the generation, so the wait returns at once and no notification is lost.
    std::uint64_t generation() const;
    // Thread-safe. Wake a waiter (an RR operation the Lua thread waits for has completed).
    void wake();
    // Lua thread. Wait until the generation differs from gen, a task is queued, or the timeout
    // passes. Returns at once if the queue is closed.
    void wait(std::uint64_t gen, boost::chrono::milliseconds timeout);

  private:
    mutable boost::mutex m_;
    boost::condition_variable cv_;
    std::uint64_t generation_ = 0;
    std::deque<std::unique_ptr<Task> > tasks_;
    bool closed_ = false;
    std::size_t posted_total_ = 0;
};

} // namespace rl
