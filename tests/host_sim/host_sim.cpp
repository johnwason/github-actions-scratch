// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Fake embedded host (M2.2). Embeds Lua the way a simulator or game engine does: links the
// binding statically, opens it through package.preload, keeps several lua_States, calls each
// state's frame() once per step, and closes and recreates states mid-run (simulation stop,
// script reload). Later milestones add live connections and services to these scenarios.
//
// usage: host_sim <scenario>
//   states    several states stepping; one closed while threads still post to it, then
//             recreated; nodes owned by a closed state are shut down, others unaffected
//   borrowed  a host-owned node borrowed by two states survives either state closing
//   connection  states with live connections to RR core's C++ test service (loaded from
//               librobotraconteur_test_server_lib, path in RRLUA_TEST_SERVER_LIB) are closed
//               without disconnecting, several times; the service keeps working

#include "rrlua/lua.hpp"
#include "rrlua/module.hpp"
#include "rrlua/thread_pool.hpp"

#include <RobotRaconteur.h>

#include <dlfcn.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void fail(const std::string& what)
{
    std::fprintf(stderr, "FAIL %s\n", what.c_str());
    ++failures;
}

// A host-side state: opened like a host does it, with the module available to require().
lua_State* open_state()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopen_robotraconteur);
    lua_setfield(L, -2, "robotraconteur");
    lua_pop(L, 2);
    return L;
}

bool run(lua_State* L, const char* chunk, const char* what)
{
    if (luaL_loadstring(L, chunk) != 0 || lua_pcall(L, 0, 0, 0) != 0)
    {
        fail(std::string(what) + ": " + lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// Call a global function returning a number; NaN on error.
double call_number(lua_State* L, const char* fn)
{
    lua_getglobal(L, fn);
    if (lua_pcall(L, 0, 1, 0) != 0)
    {
        fail(std::string(fn) + ": " + lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

const char* const kScript = R"lua(
rr = require("robotraconteur")
node = rr.createNode { name = NODE_NAME }
handled = 0
function frame()
  handled = handled + rr.step(0.001)
  return handled
end
function post(n) rr._testPostFromThread(n) end
function node_id() return node:getNodeID() end
)lua";

lua_State* start_script(int i)
{
    lua_State* L = open_state();
    lua_pushfstring(L, "experimental.host_sim_%d", i);
    lua_setglobal(L, "NODE_NAME");
    run(L, kScript, "script");
    return L;
}

void scenario_states()
{
    const int kStates = 3;
    std::vector<lua_State*> states;
    for (int i = 0; i < kStates; ++i)
        states.push_back(start_script(i));

    // Keep RR-side threads posting into every state while the host steps.
    for (lua_State* L : states)
        run(L, "post(200000)", "post");

    for (int frame = 0; frame < 300; ++frame)
    {
        for (lua_State* L : states)
            call_number(L, "frame");

        if (frame == 100)
        {
            // Simulation stop on state 1 while its posting thread is still running.
            lua_close(states[1]);
            states[1] = start_script(1); // script reload
            run(states[1], "post(1000)", "post");
        }
    }

    // Keep stepping until every state has handled exactly what was posted to it: 200,000
    // each, except the recreated state 1, which only got its 1,000.
    const double expected[kStates] = {200000, 1000, 200000};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (std::size_t i = 0; i < states.size(); ++i)
    {
        double handled = call_number(states[i], "frame");
        while (handled < expected[i] && std::chrono::steady_clock::now() < deadline)
            handled = call_number(states[i], "frame");
        std::printf("state %zu handled %.0f tasks\n", i, handled);
        if (handled != expected[i])
            fail("state " + std::to_string(i) + " handled " + std::to_string(handled) + " tasks, expected " +
                 std::to_string(expected[i]));
    }
    for (lua_State* L : states)
        lua_close(L);
    std::printf("states: %d states, one closed and recreated mid-run\n", kStates);
}

void scenario_borrowed()
{
    // The host's own node, created and owned in C++.
    auto node = RR_MAKE_SHARED<RobotRaconteur::RobotRaconteurNode>();
    node->SetNodeName("experimental.host_sim_host");
    rl::init_node(node);
    std::string id = node->NodeID().ToString();

    lua_State* a = open_state();
    lua_State* b = open_state();
    for (lua_State* L : {a, b})
    {
        run(L, "rr = require('robotraconteur')", "require");
        rl::push_borrowed_node(L, node);
        lua_setglobal(L, "hostnode");
        run(L,
            "assert(hostnode:isOwned() == false, 'borrowed')\n"
            "assert(hostnode:getNodeName() == 'experimental.host_sim_host')\n"
            "local ok, err = pcall(function() hostnode:shutdown() end)\n"
            "assert(not ok and tostring(err):find('borrowed'), 'shutdown refused: ' .. tostring(err))\n",
            "borrowed checks");
    }

    lua_close(a);
    // Still attached to b, and still running in the host.
    run(b, "assert(hostnode:isShutdown() == false); assert(rr.step() >= 0)", "after closing a");
    lua_close(b);
    if (node->NodeID().ToString() != id)
        fail("host node changed");
    // The host can still use its node after both states are gone, and shuts it down itself.
    try
    {
        auto pool = node->GetThreadPool();
        (void)pool;
        node->Shutdown();
    }
    catch (std::exception& e)
    {
        fail(std::string("host node unusable after states closed: ") + e.what());
    }
    std::printf("borrowed: host node survived both states closing\n");
}

void scenario_connection()
{
    const char* lib = std::getenv("RRLUA_TEST_SERVER_LIB");
    if (!lib || !*lib)
    {
        fail("RRLUA_TEST_SERVER_LIB not set");
        return;
    }
    // RTLD_LOCAL: the library carries its own copy of RR core, which must stay separate.
    void* h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h)
    {
        fail(std::string("dlopen: ") + dlerror());
        return;
    }
    using start_fn = int32_t (*)(const char*, uint32_t);
    using url_fn = int32_t (*)(const char*, uint32_t, char*, uint32_t*);
    using stop_fn = int32_t (*)();
    auto start = reinterpret_cast<start_fn>(dlsym(h, "robotraconteur_test_server_lib_start"));
    auto get_url = reinterpret_cast<url_fn>(dlsym(h, "robotraconteur_test_server_lib_get_service_url"));
    auto stop = reinterpret_cast<stop_fn>(dlsym(h, "robotraconteur_test_server_lib_shutdown"));
    if (!start || !get_url || !stop)
    {
        fail("test server library symbols missing");
        return;
    }
    const char* name = "experimental.host_sim_test_server";
    if (start(name, static_cast<uint32_t>(std::strlen(name))) != 0)
    {
        fail("test server did not start");
        return;
    }
    char url[1024];
    uint32_t url_len = sizeof url;
    const char* svc = "RobotRaconteurTestService";
    if (get_url(svc, static_cast<uint32_t>(std::strlen(svc)), url, &url_len) != 0)
    {
        fail("no service URL");
        stop();
        return;
    }
    std::string service_url(url, url_len);
    std::printf("connection: service at %s\n", service_url.c_str());

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        std::vector<lua_State*> states;
        for (int i = 0; i < 2; ++i)
        {
            lua_State* L = open_state();
            lua_pushlstring(L, service_url.data(), service_url.size());
            lua_setglobal(L, "URL");
            run(L,
                "rr = require('robotraconteur')\n"
                "node = rr.createNode()\n"
                "c = node:connectService(URL)\n"
                "assert(c:getProperty('d1') ~= nil)\n"
                "for i = 1, 20 do c:callFunction('meaning_of_life') end\n"
                "o = c:getObjRef('o1')\n",
                "connect");
            states.push_back(L);
        }
        // Simulation stop: close the states with their connections and objrefs still live.
        for (lua_State* L : states)
            lua_close(L);
    }

    // The service is unaffected: a fresh state still talks to it.
    lua_State* L = open_state();
    lua_pushlstring(L, service_url.data(), service_url.size());
    lua_setglobal(L, "URL");
    run(L,
        "local rr = require('robotraconteur')\n"
        "local c = rr.createNode():connectService(URL)\n"
        "assert(c:callFunction('meaning_of_life') == 42)\n"
        "c:close()\n",
        "after closes");
    lua_close(L);
    stop();
    std::printf("connection: 3 cycles of closing states with live connections\n");
}

} // namespace

int main(int argc, char** argv)
{
    const char* scenario = argc > 1 ? argv[1] : "";
    if (std::strcmp(scenario, "states") == 0)
        scenario_states();
    else if (std::strcmp(scenario, "borrowed") == 0)
        scenario_borrowed();
    else if (std::strcmp(scenario, "connection") == 0)
        scenario_connection();
    else
    {
        std::fprintf(stderr, "usage: %s states|borrowed|connection\n", argv[0]);
        return 2;
    }
    std::printf("%s: %d failures\n", scenario, failures);
    return failures == 0 ? 0 : 1;
}
