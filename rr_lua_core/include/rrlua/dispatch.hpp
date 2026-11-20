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

#include <boost/thread/mutex.hpp>

#include <cstddef>
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

  private:
    mutable boost::mutex m_;
    std::deque<std::unique_ptr<Task> > tasks_;
    bool closed_ = false;
    std::size_t posted_total_ = 0;
};

} // namespace rl
