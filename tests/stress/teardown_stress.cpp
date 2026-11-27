// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Repeated teardown stress test (M0.5). Several threads repeatedly create a lua_State,
// load the M0 test module, throw C++ exceptions through it, and close the state while
// non-singleton RR nodes are created, given an IntraTransport, and shut down. Run under
// ASan and TSan by scripts/verify-sanitizers.sh, and plainly by scripts/verify.sh.
// Later milestones extend it with live connections and services.
//
// usage: teardown_stress <iterations> <threads>   (LUA_CPATH must find rrlua_m0)

#include "rrlua/lua.hpp"
#include "rrlua/thread_pool.hpp"

#include <RobotRaconteur.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

const char* const kChunk = R"lua(
local m = require("rrlua_m0")
local p = m.Probe()
local s = string.rep("x", 1024)
for i = 1, 50 do
  assert(not pcall(p.throwWith, p, s))
  assert(not pcall(m.varargs_throw, s))
  assert(p:echo(nil) == nil)
end
-- Leave live userdata behind so lua_close runs their finalizers.
KEEP = { m.Probe(), m.Probe() }
)lua";

std::atomic<int> g_failures{0};

void fail(const char* what, const char* detail)
{
    std::fprintf(stderr, "FAIL %s: %s\n", what, detail ? detail : "");
    ++g_failures;
}

// Run a chunk in a fresh state, then close it. Returns false on a Lua error.
bool run_state(const char* chunk)
{
    lua_State* L = luaL_newstate();
    if (!L)
    {
        fail("luaL_newstate", "out of memory");
        return false;
    }
    luaL_openlibs(L);
    bool ok = luaL_loadstring(L, chunk) == 0 && lua_pcall(L, 0, 0, 0) == 0;
    if (!ok)
        fail("lua", lua_tostring(L, -1));
    lua_close(L);
    return ok;
}

void rr_node_cycle()
{
    try
    {
        auto node = RR_MAKE_SHARED<RobotRaconteur::RobotRaconteurNode>();
        rl::init_node(node);
        auto intra = RR_MAKE_SHARED<RobotRaconteur::IntraTransport>(node);
        intra->StartServer();
        node->RegisterTransport(intra);
        node->Shutdown();
    }
    catch (std::exception& e)
    {
        fail("rr node", e.what());
    }
}

// True if a shared object whose path contains name is mapped into this process.
bool is_mapped(const char* name)
{
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line))
        if (line.find(name) != std::string::npos)
            return true;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    int iterations = argc > 1 ? std::atoi(argv[1]) : 20;
    int threads = argc > 2 ? std::atoi(argv[2]) : 4;

    // SWIG initializes its type table in a global on first load, unlocked: load the module
    // once on one thread before any concurrent load (CLAUDE.md, "SWIG").
    if (!run_state("require('rrlua_m0')"))
        return 1;
    // That state is closed now. The module must stay loaded (-z nodelete): if it were
    // unloaded, the concurrent requires below would each reinitialize SWIG's globals.
    if (!is_mapped("rrlua_m0"))
    {
        fail("module unloaded", "rrlua_m0 was unloaded when its last lua_State closed");
        return 1;
    }

    for (int it = 0; it < iterations; ++it)
    {
        std::vector<std::thread> ts;
        for (int t = 0; t < threads; ++t)
        {
            ts.emplace_back([t]() {
                if (t % 2 == 0)
                {
                    run_state(kChunk);
                    rr_node_cycle();
                }
                else
                {
                    rr_node_cycle();
                    run_state(kChunk);
                }
            });
        }
        for (auto& th : ts)
            th.join();
    }

    // Every C++ object created on a throwing path must have been destroyed.
    run_state("local p = require('rrlua_m0').Probe()\n"
              "assert(p:liveTrackers() == 0, 'live trackers: ' .. p:liveTrackers())");

    std::printf("teardown_stress: %d iterations x %d threads, %d failures\n", iterations, threads,
                g_failures.load());
    return g_failures.load() == 0 ? 0 : 1;
}
