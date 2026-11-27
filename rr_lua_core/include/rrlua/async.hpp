// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Waiting for RR async operations on the Lua thread (CLAUDE.md D9). Blocking calls are an
// async call plus the pump: while the Lua thread waits, it runs this state's queued tasks
// (event listeners, callbacks, async completions), so a service that calls back into the
// client during a call does not deadlock. RR threads only store the result and wake the queue.
#pragma once

#include "rrlua/dispatch.hpp"
#include "rrlua/lua_api.hpp"

#include <RobotRaconteur.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace rl
{

// Run this state's queued tasks on the Lua thread until done() returns true. done() is
// checked on the Lua thread only. Errors from tasks propagate (the operation keeps running
// and its result is dropped). Throws if the state's queue is closed. A deadline only shortens
// the waits, so done() is checked again by then.
void pump_until(lua_State* L, const std::function<bool()>& done,
                boost::chrono::steady_clock::time_point deadline = boost::chrono::steady_clock::time_point::max());

// The result of one RR async operation. complete() may run on any thread; the first call wins
// and later ones are dropped (RR core can invoke a handler twice; see OpRef in ops.hpp).
template <typename T>
class Completion : public std::enable_shared_from_this<Completion<T> >
{
  public:
    explicit Completion(std::weak_ptr<DispatchQueue> q) : q_(std::move(q)) {}

    void complete(const T& v, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& e)
    {
        if (claimed_.exchange(true, std::memory_order_acq_rel))
            return;
        value_ = v;
        err_ = e;
        done_.store(true, std::memory_order_release);
        if (std::shared_ptr<DispatchQueue> q = q_.lock())
            q->wake();
    }

    bool done() const { return done_.load(std::memory_order_acquire); }

    // An RR completion handler bound to this slot.
    boost::function<void(const T&, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)> handler()
    {
        std::shared_ptr<Completion<T> > self = this->shared_from_this();
        return [self](const T& v, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& e) {
            self->complete(v, e);
        };
    }

    // Lua thread: pump until done, then return the value or throw the RR error.
    T wait(lua_State* L)
    {
        pump_until(L, [this] { return done(); });
        if (err_)
            throw RobotRaconteur::RobotRaconteurException(*err_);
        return value_;
    }

  private:
    std::weak_ptr<DispatchQueue> q_;
    std::atomic<bool> claimed_{false};
    std::atomic<bool> done_{false};
    T value_{};
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException> err_;
};

// For operations that report only an error.
class VoidCompletion : public std::enable_shared_from_this<VoidCompletion>
{
  public:
    explicit VoidCompletion(std::weak_ptr<DispatchQueue> q) : inner_(std::make_shared<Completion<bool> >(q)) {}

    boost::function<void(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)> handler()
    {
        std::shared_ptr<Completion<bool> > c = inner_;
        return [c](const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& e) { c->complete(true, e); };
    }
    void wait(lua_State* L) { inner_->wait(L); }

  private:
    std::shared_ptr<Completion<bool> > inner_;
};

// lua_pcall the function and nargs arguments on top of the stack. On error, pop the error and
// throw it as a LuaError (its text already holds the script location).
void call_lua(lua_State* L, int nargs, int nresults);
// The text of a Lua error value (a string, or a description of another value).
std::string lua_error_text(lua_State* L, int idx);

// The calling state's queue (throws if the module was never opened in this state).
std::shared_ptr<DispatchQueue> state_queue(lua_State* L);
// The queue for a blocking call: also checks, before anything is sent, that the call may wait
// (blocking calls nested in handlers are limited; see pump_until).
std::shared_ptr<DispatchQueue> blocking_queue(lua_State* L);

// The request timeout of a node in RR's int32 milliseconds.
int32_t request_timeout(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node);

} // namespace rl
