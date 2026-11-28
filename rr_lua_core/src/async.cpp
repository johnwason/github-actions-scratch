// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/async.hpp"

#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"

#include <limits>
#include <stdexcept>

namespace rl
{

std::shared_ptr<DispatchQueue> state_queue(lua_State* L) { return StateContext::require(L)->queue(); }

namespace
{
void check_depth(StateContext* ctx)
{
    if (ctx->pump_depth >= StateContext::kMaxPumpDepth)
        throw RobotRaconteur::InvalidOperationException(
            "blocking calls nested too deeply in event, callback or completion handlers (a handler "
            "that triggers itself, or a likely deadlock); use an async call in the handler");
}
} // namespace

std::shared_ptr<DispatchQueue> blocking_queue(lua_State* L)
{
    StateContext* ctx = StateContext::require(L);
    check_depth(ctx);
    return ctx->queue();
}

void pump_until(lua_State* L, const std::function<bool()>& done, boost::chrono::steady_clock::time_point deadline)
{
    StateContext* ctx = StateContext::require(L);
    check_depth(ctx);
    struct Depth
    {
        int& d;
        explicit Depth(int& x) : d(x) { ++d; }
        ~Depth() { --d; }
    } depth(ctx->pump_depth);
    std::shared_ptr<DispatchQueue> q = ctx->queue();
    for (;;)
    {
        std::uint64_t gen = q->generation();
        if (done())
            return;
        if (q->closed())
            throw RobotRaconteur::InvalidOperationException("the Lua state is closing");
        if (std::unique_ptr<Task> t = q->pop())
        {
            t->run(L);
            continue;
        }
        boost::chrono::milliseconds wait(100);
        auto now = boost::chrono::steady_clock::now();
        if (deadline != boost::chrono::steady_clock::time_point::max())
        {
            if (deadline <= now)
                continue;
            auto left = boost::chrono::duration_cast<boost::chrono::milliseconds>(deadline - now) +
                        boost::chrono::milliseconds(1);
            if (left < wait)
                wait = left;
        }
        q->wait(gen, wait);
    }
}

std::string lua_error_text(lua_State* L, int idx)
{
    if (lua_type(L, idx) == LUA_TSTRING || lua_type(L, idx) == LUA_TNUMBER)
    {
        size_t n = 0;
        const char* s = lua_tolstring(L, idx, &n);
        return std::string(s, n);
    }
    return std::string("(error object is a ") + luaL_typename(L, idx) + " value)";
}

void call_lua(lua_State* L, int nargs, int nresults)
{
    if (lua_pcall(L, nargs, nresults, 0) != 0)
    {
        std::string msg = lua_error_text(L, -1);
        lua_pop(L, 1);
        throw LuaError(msg);
    }
}

int32_t request_timeout(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node)
{
    uint32_t t = node->GetRequestTimeout();
    if (t > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(t);
}

} // namespace rl
