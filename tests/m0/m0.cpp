// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "m0.hpp"

#include <atomic>
#include <stdexcept>

namespace m0
{
namespace
{
std::atomic<int> g_live{0};

struct Tracker
{
    std::string payload;
    explicit Tracker(const std::string& p) : payload(p) { ++g_live; }
    ~Tracker() { --g_live; }
    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;
};
} // namespace

int live_trackers() { return g_live.load(); }

Probe::Probe() = default;
Probe::~Probe() = default;

rl::Ret Probe::echo(rl::Arg a)
{
    if (!lua_checkstack(a.L, 1))
        throw std::runtime_error("Lua stack overflow");
    lua_pushvalue(a.L, a.idx);
    return {1};
}

rl::Ret Probe::swap(rl::Arg a, rl::Arg b)
{
    if (!lua_checkstack(a.L, 2))
        throw std::runtime_error("Lua stack overflow");
    lua_pushvalue(a.L, b.idx);
    lua_pushvalue(a.L, a.idx);
    return {2};
}

rl::Ret Probe::none(lua_State*) { return {0}; }

std::string Probe::typeName(rl::Arg a) { return lua_typename(a.L, lua_type(a.L, a.idx)); }

void Probe::throwWith(const std::string& msg)
{
    Tracker t(msg);
    throw std::runtime_error("m0 error: " + msg);
}

int Probe::liveTrackers() { return live_trackers(); }

} // namespace m0

int m0_varargs(lua_State* L)
{
    return rl::guard(L, [L]() {
        int n = lua_gettop(L);
        if (!lua_checkstack(L, n + 1))
            throw std::runtime_error("Lua stack overflow");
        lua_pushinteger(L, n);
        lua_insert(L, 1);
        return n + 1;
    });
}

int m0_varargs_throw(lua_State* L)
{
    return rl::guard(L, [L]() -> int {
        size_t len = 0;
        const char* s = lua_tolstring(L, 1, &len);
        std::string msg = s ? std::string(s, len) : std::string("(no message)");
        m0::Tracker t(msg); // live when the exception is thrown; must be destroyed
        throw std::runtime_error("m0 native error: " + t.payload);
    });
}
