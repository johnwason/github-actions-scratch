// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/context.hpp"

#include "rrlua/thread_pool.hpp"

#include <new>
#include <stdexcept>

namespace rl
{
namespace
{
// Address used as the registry key of the sentinel userdata.
const char kContextKey = 0;
const char* const kContextMeta = "robotraconteur.state_context";
} // namespace

StateContext::StateContext() : queue_(std::make_shared<DispatchQueue>()) {}

StateContext::~StateContext() { teardown(); }

StateContext* StateContext::get(lua_State* L)
{
    lua_pushlightuserdata(L, const_cast<char*>(&kContextKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<StateContext*>(p);
}

StateContext* StateContext::require(lua_State* L)
{
    StateContext* c = get(L);
    if (!c)
        throw std::logic_error("robotraconteur module is not open in this lua_State");
    return c;
}

StateContext* StateContext::ensure(lua_State* L)
{
    if (StateContext* c = get(L))
        return c;
    void* mem = lua_newuserdata(L, sizeof(StateContext));
    StateContext* c = nullptr;
    try
    {
        c = new (mem) StateContext();
    }
    catch (...)
    {
        lua_pop(L, 1);
        throw;
    }
    // Only now give it a finalizer, so __gc never sees an unconstructed object.
    if (luaL_newmetatable(L, kContextMeta))
    {
        lua_pushcfunction(L, &StateContext::gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, const_cast<char*>(&kContextKey));
    lua_insert(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    return c;
}

int StateContext::gc(lua_State* L)
{
    auto* c = static_cast<StateContext*>(lua_touserdata(L, 1));
    if (c)
        c->~StateContext(); // teardown; must not raise
    return 0;
}

void StateContext::teardown()
{
    if (torn_down_)
        return;
    torn_down_ = true;
    // 1. Detach from RR threads: nothing posted from now on reaches this state.
    queue_->close();
    for (auto& t : test_threads)
        if (t.joinable())
            t.join();
    test_threads.clear();
    // 2. Shut down the nodes this state owns. Borrowed nodes keep running.
    for (auto& e : nodes_)
    {
        if (e->owned && !e->shutdown)
        {
            try
            {
                e->node->Shutdown();
            }
            catch (...)
            {
            }
            e->shutdown = true;
        }
    }
    nodes_.clear();
    default_.reset();
}

void StateContext::prune()
{
    std::vector<std::shared_ptr<NodeEntry> > keep;
    for (auto& e : nodes_)
        if (!e->shutdown)
            keep.push_back(e);
    nodes_.swap(keep);
}

std::shared_ptr<NodeEntry> StateContext::create_node(const std::string& name, int thread_count)
{
    prune();
    auto node = RR_MAKE_SHARED<RobotRaconteur::RobotRaconteurNode>();
    if (!name.empty())
        node->SetNodeName(name);
    init_node(node);
    if (thread_count > 0)
        node->SetThreadPoolCount(thread_count);
    auto e = std::make_shared<NodeEntry>();
    e->node = node;
    e->owned = true;
    nodes_.push_back(e);
    return e;
}

std::shared_ptr<NodeEntry> StateContext::attach_borrowed(
    const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node)
{
    if (!node)
        throw std::invalid_argument("attach_borrowed: null node");
    prune();
    for (auto& e : nodes_)
        if (e->node == node)
            return e;
    auto e = std::make_shared<NodeEntry>();
    e->node = node;
    e->owned = false;
    nodes_.push_back(e);
    return e;
}

std::shared_ptr<NodeEntry> StateContext::default_node()
{
    if (!default_ || default_->shutdown)
        default_ = create_node(std::string(), 0);
    return default_;
}

} // namespace rl
