// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/module.hpp"

#include "rrlua/async.hpp"
#include "rrlua/client.hpp"

#include <boost/chrono.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace rl
{
namespace
{

struct NodeOptions
{
    std::string name;
    int threads = 0;
};

// Read an options table with raw access only; reject unknown keys so typos are caught.
// Errors are C++ exceptions (the caller's guard turns them into Lua errors).
NodeOptions read_node_options(lua_State* L, int idx)
{
    NodeOptions o;
    if (lua_isnoneornil(L, idx))
        return o;
    if (!lua_istable(L, idx))
        throw std::invalid_argument("createNode: options must be a table");
    if (!lua_checkstack(L, 3))
        throw std::runtime_error("Lua stack overflow");
    lua_pushnil(L);
    while (lua_next(L, idx) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
        {
            lua_pop(L, 2);
            throw std::invalid_argument("createNode: option keys must be strings");
        }
        std::string key = lua_tostring(L, -2);
        if (key == "name")
        {
            if (lua_type(L, -1) != LUA_TSTRING)
            {
                lua_pop(L, 2);
                throw std::invalid_argument("createNode: name must be a string");
            }
            o.name = lua_tostring(L, -1);
        }
        else if (key == "threads")
        {
            double v = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : -1;
            if (!(v >= 1 && v <= 1000 && v == std::floor(v)))
            {
                lua_pop(L, 2);
                throw std::invalid_argument("createNode: threads must be an integer from 1 to 1000");
            }
            o.threads = static_cast<int>(v);
        }
        else
        {
            lua_pop(L, 2);
            throw std::invalid_argument("createNode: unknown option '" + key + "'");
        }
        lua_pop(L, 1);
    }
    return o;
}

class TestTask : public Task
{
  public:
    void run(lua_State* L) override { StateContext::require(L)->test_counter += 1; }
};

} // namespace

Node* createNode(lua_State* L) { return new Node(StateContext::require(L)->create_node("", 0)); }

Node* createNode(lua_State* L, rl::Arg opts)
{
    NodeOptions o = read_node_options(L, opts.idx);
    return new Node(StateContext::require(L)->create_node(o.name, o.threads));
}

Node* defaultNode(lua_State* L) { return new Node(StateContext::require(L)->default_node()); }

Client* connectService(lua_State* L, const std::string& url)
{
    return connect_service(L, StateContext::require(L)->default_node(), url);
}

Client* connectService(lua_State* L, const std::string& url, rl::Arg options)
{
    return connect_service(L, StateContext::require(L)->default_node(), url, connect_options(L, options.idx));
}

void _testPostFromThread(lua_State* L, int n)
{
    StateContext* ctx = StateContext::require(L);
    std::weak_ptr<DispatchQueue> q = ctx->queue();
    ctx->test_threads.emplace_back([q, n]() {
        for (int i = 0; i < n; ++i)
        {
            std::shared_ptr<DispatchQueue> qq = q.lock();
            if (!qq || !qq->post(std::unique_ptr<Task>(new TestTask())))
                return;
        }
    });
}

double _testCounter(lua_State* L)
{
    StateContext* ctx = StateContext::require(L);
    // Join finished test threads so the counter is final once they are done.
    for (auto& t : ctx->test_threads)
        if (t.joinable())
            t.join();
    ctx->test_threads.clear();
    return static_cast<double>(ctx->test_counter);
}

int _queueSize(lua_State* L) { return static_cast<int>(StateContext::require(L)->queue()->size()); }

} // namespace rl

namespace
{
struct InStep
{
    rl::StateContext* ctx;
    explicit InStep(rl::StateContext* c) : ctx(c) { ctx->in_step = true; }
    ~InStep() { ctx->in_step = false; }
};
} // namespace

int rl_step(lua_State* L)
{
    return rl::guard(L, [L]() -> int {
        double budget = -1;
        if (!lua_isnoneornil(L, 1))
        {
            if (lua_type(L, 1) != LUA_TNUMBER || !(lua_tonumber(L, 1) >= 0))
                throw std::invalid_argument("step: budget must be a non-negative number of seconds");
            budget = lua_tonumber(L, 1);
        }
        rl::StateContext* ctx = rl::StateContext::require(L);
        if (ctx->in_step)
            throw std::logic_error("step: called from inside a task that step is running");
        InStep guard(ctx);
        ctx->sweep(L);
        std::shared_ptr<rl::DispatchQueue> q = ctx->queue();

        auto start = boost::chrono::steady_clock::now();
        std::size_t limit = budget < 0 ? q->size() : static_cast<std::size_t>(-1);
        lua_Integer n = 0;
        while (static_cast<std::size_t>(n) < limit)
        {
            std::unique_ptr<rl::Task> t = q->pop();
            if (!t)
                break;
            t->run(L);
            ++n;
            if (budget >= 0 &&
                boost::chrono::duration<double>(boost::chrono::steady_clock::now() - start).count() >= budget)
                break;
        }
        lua_pushinteger(L, n);
        return 1;
    });
}

int rl_sleep(lua_State* L)
{
    return rl::guard(L, [L]() -> int {
        if (lua_type(L, 1) != LUA_TNUMBER || !(lua_tonumber(L, 1) >= 0))
            throw std::invalid_argument("sleep: expected a non-negative number of seconds");
        double secs = lua_tonumber(L, 1);
        rl::StateContext* ctx = rl::StateContext::require(L);
        ctx->sweep(L);
        auto end = boost::chrono::steady_clock::now() +
                   boost::chrono::duration_cast<boost::chrono::steady_clock::duration>(
                       boost::chrono::duration<double>(secs));
        rl::pump_until(L, [&]() { return boost::chrono::steady_clock::now() >= end; }, end);
        return 0;
    });
}

int rl_time(lua_State* L)
{
    // Monotonic seconds (for loops paced by rr.sleep); os.clock is CPU time.
    double t = boost::chrono::duration<double>(boost::chrono::steady_clock::now().time_since_epoch()).count();
    lua_pushnumber(L, t);
    return 1;
}
