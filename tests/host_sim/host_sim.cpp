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
//   streaming   (M3) a long frame loop streaming a wire and a pipe with listeners, stepping
//               only every few frames: the queue and memory stay bounded; then a state closed
//               while the service waits on its callback closes promptly
//   service     (M4) a Lua service on a host-owned node with a Lua client on another node over
//               TCP: the service state closed and recreated with the client connected, the
//               client's state closed, its node shut down; the other side keeps working

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
#include <thread>
#include <vector>

namespace
{

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kAddressSanitizer = true;
#else
constexpr bool kAddressSanitizer = false;
#endif

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

// RR core's C++ test service, from librobotraconteur_test_server_lib (RRLUA_TEST_SERVER_LIB).
struct TestServer
{
    int32_t (*start_)(const char*, uint32_t) = nullptr;
    int32_t (*url_)(const char*, uint32_t, char*, uint32_t*) = nullptr;
    int32_t (*stop_)() = nullptr;
    bool running = false;

    bool start()
    {
        const char* lib = std::getenv("RRLUA_TEST_SERVER_LIB");
        if (!lib || !*lib)
        {
            fail("RRLUA_TEST_SERVER_LIB not set");
            return false;
        }
        // RTLD_LOCAL: the library carries its own copy of RR core, which must stay separate.
        void* h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (!h)
        {
            fail(std::string("dlopen: ") + dlerror());
            return false;
        }
        start_ = reinterpret_cast<int32_t (*)(const char*, uint32_t)>(dlsym(h, "robotraconteur_test_server_lib_start"));
        url_ = reinterpret_cast<int32_t (*)(const char*, uint32_t, char*, uint32_t*)>(
            dlsym(h, "robotraconteur_test_server_lib_get_service_url"));
        stop_ = reinterpret_cast<int32_t (*)()>(dlsym(h, "robotraconteur_test_server_lib_shutdown"));
        if (!start_ || !url_ || !stop_)
        {
            fail("test server library symbols missing");
            return false;
        }
        const char* name = "experimental.host_sim_test_server";
        if (start_(name, static_cast<uint32_t>(std::strlen(name))) != 0)
        {
            fail("test server did not start");
            return false;
        }
        running = true;
        return true;
    }

    std::string url(const char* svc)
    {
        char buf[1024];
        uint32_t len = sizeof buf;
        if (url_(svc, static_cast<uint32_t>(std::strlen(svc)), buf, &len) != 0)
        {
            fail(std::string("no URL for ") + svc);
            return std::string();
        }
        return std::string(buf, len);
    }

    void stop()
    {
        if (running)
            stop_();
        running = false;
    }
};

lua_State* open_with_url(const std::string& url)
{
    lua_State* L = open_state();
    lua_pushlstring(L, url.data(), url.size());
    lua_setglobal(L, "URL");
    return L;
}

long rss_kb()
{
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return -1;
    char line[256];
    long kb = -1;
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, "VmRSS:", 6) == 0)
            kb = std::strtol(line + 6, nullptr, 10);
    std::fclose(f);
    return kb;
}

void scenario_connection()
{
    TestServer server;
    if (!server.start())
        return;
    std::string service_url = server.url("RobotRaconteurTestService");
    if (service_url.empty())
    {
        server.stop();
        return;
    }
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
    server.stop();
    std::printf("connection: 3 cycles of closing states with live connections\n");
}


const char* const kStreamScript = R"lua(
rr = require("robotraconteur")
node = rr.createNode()
c = node:connectService(URL)
w = c:connectWire("w1")
p = c:connectPipe("p1")
changed, received, max_queue = 0, 0, 0
w:addValueChangedListener(function(v) changed = changed + 1; assert(v:numel() == 1000) end)
p:addPacketReceivedListener(function()
  while true do
    local ok, v = p:tryReceivePacket()
    if not ok then break end
    assert(v:numel() == 1000)
    received = received + 1
  end
end)
ev = 0
c:addEventListener("ev1", function() ev = ev + 1 end)
data = rr.array("double", 1000)
for i = 1, 1000 do data[i] = i end
sent = 0
-- One host frame. The host steps the state only every few frames (a busy or paused game).
function frame(n)
  w:setOutValue(data)
  if n % 50 == 0 then c:asyncCallFunction(nil, "func1") end -- events from the service
  if n % 4 == 0 then p:sendPacket(data); sent = sent + 1 end
  local q = rr._queueSize()
  if q > max_queue then max_queue = q end
  if n % 5 == 0 then rr.step() end
  return max_queue
end
)lua";

void scenario_streaming()
{
    TestServer server;
    if (!server.start())
        return;
    std::string url = server.url("RobotRaconteurTestService");
    if (url.empty())
    {
        server.stop();
        return;
    }

    // 1. A long-running frame loop: memory and the queue stay bounded.
    lua_State* L = open_with_url(url);
    if (!run(L, kStreamScript, "stream setup"))
    {
        lua_close(L);
        server.stop();
        return;
    }
    const int kFrames = 6000;
    long rss_warm = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int n = 1; n <= kFrames; ++n)
    {
        lua_getglobal(L, "frame");
        lua_pushinteger(L, n);
        if (lua_pcall(L, 1, 0, 0) != 0)
        {
            fail(std::string("frame: ") + lua_tostring(L, -1));
            break;
        }
        if (n == 1000)
            rss_warm = rss_kb();
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    run(L, "for _ = 1, 50 do rr.sleep(0.01) end", "drain");
    long rss_end = rss_kb();
    lua_getglobal(L, "max_queue");
    lua_getglobal(L, "changed");
    lua_getglobal(L, "received");
    lua_getglobal(L, "sent");
    lua_getglobal(L, "ev");
    double max_queue = lua_tonumber(L, -5), changed = lua_tonumber(L, -4), received = lua_tonumber(L, -3),
           sent = lua_tonumber(L, -2), ev = lua_tonumber(L, -1);
    lua_pop(L, 5);
    std::printf("streaming: %d frames in %.1f s; wire listener %.0f calls, %.0f/%.0f packets, %.0f events, "
                "max queue %.0f, RSS %ld -> %ld KB\n",
                kFrames, secs, changed, received, sent, ev, max_queue, rss_warm, rss_end);
    if (received != sent)
        fail("pipe packets lost: " + std::to_string(received) + " of " + std::to_string(sent));
    if (changed < 1)
        fail("wire listener never ran");
    if (ev < 1)
        fail("no events received");
    // Coalescing bounds the queue: at most one wire and one pipe notification, plus a few
    // events and async completions per stepping interval.
    if (max_queue > 32)
        fail("queue grew to " + std::to_string(max_queue));
    // ASan keeps freed memory in quarantine (hundreds of MB), so RSS says nothing there.
    if (!kAddressSanitizer && rss_warm > 0 && rss_end - rss_warm > 16 * 1024)
        fail("RSS grew by " + std::to_string(rss_end - rss_warm) + " KB after warm-up");
    lua_close(L);

    // 2. A state closed while the service waits on its callback: the RR thread waiting for
    // the callback is released at once, and closing does not wait for the request timeout.
    lua_State* C = open_with_url(url);
    run(C,
        "rr = require('robotraconteur')\n"
        "c = rr.createNode():connectService(URL)\n"
        "c:setCallback('cb1', function() end)\n"
        "op = c:asyncCallFunction(nil, 'test_callbacks')\n",
        "callback pending");
    // The request reaches the service, which calls cb1 and waits; the host does not step.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    run(C, "assert(not op:isDone()); assert(rr._queueSize() >= 1, 'callback request queued')", "still pending");
    auto c0 = std::chrono::steady_clock::now();
    lua_close(C);
    double close_secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
    std::printf("streaming: closed a state with a pending callback in %.2f s\n", close_secs);
    if (close_secs > 5)
        fail("closing a state with a pending callback took " + std::to_string(close_secs) + " s");

    // The service is unaffected.
    lua_State* A = open_with_url(url);
    run(A,
        "local rr = require('robotraconteur')\n"
        "local c = rr.createNode():connectService(URL)\n"
        "assert(c:callFunction('meaning_of_life') == 42)\n"
        "c:close()\n",
        "after streaming");
    lua_close(A);
    server.stop();
}

const char* const kServiceDef = R"robdef(
service experimental.host_sim_service

stdver 0.10

object Calc
    function double add(double a, double b)
    property int32 generation
end
)robdef";

const char* const kServiceScript = R"lua(
rr = require("robotraconteur")
pcall(function() hostnode:registerServiceType(DEF) end) -- kept by the host node across states
svc = hostnode:registerService("calc", "experimental.host_sim_service.Calc", {
  callFunction = function(name, a, b) return a + b end,
  getProperty = function(name) return GENERATION end,
})
function frame() return rr.step() end
)lua";

const char* const kCalcClientScript = R"lua(
rr = require("robotraconteur")
node = rr.createNode()
function connect() c = node:connectService(URL) end
-- Start a call; the host steps both states until it completes. A call to a closed service
-- fails either at once (the client already saw the disconnect) or when its reply arrives.
function start(a, b)
  failed = not pcall(function() op = c:asyncCallFunction(nil, "add", a, b) end)
end
function frame() rr.step(); return failed or (op and op:isDone()) end
function result()
  if failed then return -1 end
  local ok, v = pcall(function() return op:result() end)
  if ok then return v end
  return -1
end
)lua";

// Step the given states until the client's op is done (or a timeout); returns its result.
double call_add(lua_State* client, std::vector<lua_State*> others, double a, double b)
{
    lua_getglobal(client, "start");
    lua_pushnumber(client, a);
    lua_pushnumber(client, b);
    if (lua_pcall(client, 2, 0, 0) != 0)
    {
        fail(std::string("start: ") + lua_tostring(client, -1));
        lua_pop(client, 1);
        return -2;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (lua_State* o : others)
            call_number(o, "frame");
        lua_getglobal(client, "frame");
        if (lua_pcall(client, 0, 1, 0) != 0)
        {
            fail(std::string("client frame: ") + lua_tostring(client, -1));
            lua_pop(client, 1);
            return -2;
        }
        bool done = lua_toboolean(client, -1) != 0;
        lua_pop(client, 1);
        if (done)
            return call_number(client, "result");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fail("call did not complete");
    return -2;
}

lua_State* start_service_state(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& host, int generation)
{
    lua_State* L = open_state();
    run(L, "rr = require('robotraconteur')", "require");
    rl::push_borrowed_node(L, host);
    lua_setglobal(L, "hostnode");
    lua_pushstring(L, kServiceDef);
    lua_setglobal(L, "DEF");
    lua_pushinteger(L, generation);
    lua_setglobal(L, "GENERATION");
    run(L, kServiceScript, "service state");
    return L;
}

lua_State* start_client_state(const std::string& url)
{
    lua_State* L = open_with_url(url);
    run(L, kCalcClientScript, "client state");
    run(L, "connect()", "connect");
    return L;
}

void scenario_service()
{
    // The host's node, with a TCP server the host starts; Lua states come and go.
    auto host = RR_MAKE_SHARED<RobotRaconteur::RobotRaconteurNode>();
    host->SetNodeName("experimental.host_sim_service_host");
    rl::init_node(host);
    auto tcp = RR_MAKE_SHARED<RobotRaconteur::TcpTransport>(host);
    tcp->StartServer(0);
    host->RegisterTransport(tcp);
    std::string url = "rr+tcp://localhost:" + std::to_string(tcp->GetListenPort()) + "?service=calc";

    // 1. A Lua service on the host node and a Lua client on its own node, over TCP.
    lua_State* S = start_service_state(host, 1);
    lua_State* C = start_client_state(url);
    if (call_add(C, {S}, 1, 2) != 3)
        fail("first call");

    // 2. Close the service's state with the client connected: no crash; the client's next call
    // fails cleanly, and the client state keeps working.
    lua_close(S);
    double r = call_add(C, {}, 1, 2);
    if (r != -1)
        fail("call to a closed service returned " + std::to_string(r));

    // 3. Recreate the service state (script reload): the service is back on the host node.
    for (int gen = 2; gen <= 4; ++gen)
    {
        S = start_service_state(host, gen);
        run(C, "connect()", "reconnect");
        if (call_add(C, {S}, gen, 10) != gen + 10)
            fail("call after recreating the service, generation " + std::to_string(gen));
        if (gen < 4)
            lua_close(S);
    }

    // 4. Close the client's state while connected (its owned node shuts down): the service keeps
    // serving a new client.
    lua_close(C);
    lua_State* C2 = start_client_state(url);
    if (call_add(C2, {S}, 20, 22) != 42)
        fail("call from a new client after the old client's state closed");

    // 5. Shut the client's node down from Lua while connected; the service is unaffected.
    run(C2, "node:shutdown()", "client node shutdown");
    lua_State* C3 = start_client_state(url);
    if (call_add(C3, {S}, 5, 5) != 10)
        fail("call after a client node shut down");

    lua_close(C2);
    lua_close(C3);
    lua_close(S);
    host->Shutdown();
    std::printf("service: a Lua service survived 4 state cycles and its clients' states closing\n");
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
    else if (std::strcmp(scenario, "streaming") == 0)
        scenario_streaming();
    else if (std::strcmp(scenario, "service") == 0)
        scenario_service();
    else
    {
        std::fprintf(stderr, "usage: %s states|borrowed|connection|streaming|service\n", argv[0]);
        return 2;
    }
    std::printf("%s: %d failures\n", scenario, failures);
    return failures == 0 ? 0 : 1;
}
