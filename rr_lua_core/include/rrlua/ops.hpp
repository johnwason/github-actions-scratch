// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Async operations (M3): `obj:asyncCallFunction(handler, name, ...)` and friends return an op.
// The RR completion posts a task; on the Lua thread the task stores the result in the op, calls
// the handler as handler(value, err), and resumes coroutines waiting in rr.await(op).
//
// An op is a userdata (OpHeader, trivially destructible) whose user value is a table
// {handler=, waiters={...}, value=, err=}. While pending, the op is held by a registry
// reference so the Lua GC keeps it until the completion task has run.
#pragma once

#include "rrlua/dispatch.hpp"
#include "rrlua/lua_api.hpp"

#include <RobotRaconteur.h>

#include <atomic>
#include <functional>
#include <memory>

namespace rl
{

// On the Lua thread: push the operation's result value (exactly one value; nil for none).
using OpConverter = std::function<void(lua_State*)>;

// Lua thread: push a new pending op. handler_idx is a function, or 0 / a nil slot for none.
// Returns the registry reference that keeps the op alive until it completes. Allocates Lua
// memory (call with no non-trivial C++ object live in the calling frames, or accept the rare
// out-of-memory leak, as the marshaller does).
int push_new_op(lua_State* L, int handler_idx);

// Lua thread: drop the reference of an op whose operation could not be started.
void release_op(lua_State* L, int ref);

// Any thread: complete op `ref` of the state that owns q. conv runs on the Lua thread (only if
// err is null). Dropped if the state has closed.
void post_op_result(const std::shared_ptr<DispatchQueue>& q, int ref, OpConverter conv,
                    const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& err);

// The pending op `ref`, claimed once: the first of complete() (any thread) and abandon() (Lua
// thread) acts, later calls do nothing. RR core can invoke a request's handler after the call
// that registered it threw (ClientContext::AsyncProcessRequest leaves the request registered when
// the send throws; see PROGRESS.md, 2026-11-27), and acting twice would unref a registry slot
// that may by then hold another value.
class OpRef
{
  public:
    OpRef(std::weak_ptr<DispatchQueue> q, int ref) : q_(std::move(q)), ref_(ref) {}

    // Any thread: the operation's result.
    void complete(OpConverter conv, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& err)
    {
        if (!claimed_.exchange(true))
            post_op_result(q_.lock(), ref_, std::move(conv), err);
    }

    // Lua thread: the call that starts the operation threw.
    void abandon(lua_State* L)
    {
        if (!claimed_.exchange(true))
            release_op(L, ref_);
    }

  private:
    std::weak_ptr<DispatchQueue> q_;
    int ref_;
    std::atomic<bool> claimed_{false};
};

// Module init: the op metatable and rr.await. The module table is at module_idx.
void open_ops(lua_State* L, int module_idx);

} // namespace rl
