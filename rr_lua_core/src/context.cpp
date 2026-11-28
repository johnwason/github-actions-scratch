// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/context.hpp"

#include "rrlua/client.hpp"

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
    // 2. Close the services this state registered on borrowed nodes; the host's node keeps
    // running without them. (Requests already queued were answered with errors in step 1.)
    for (auto& s : services_)
    {
        if (s.first->owned || s.first->shutdown)
            continue;
        try
        {
            s.first->node->CloseService(s.second);
        }
        catch (...)
        {
        }
    }
    services_.clear();
    // 3. Shut down the nodes this state owns. Borrowed nodes keep running.
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
    node->SetDynamicServiceFactory(RR_MAKE_SHARED<DynamicStubFactory>());
    node->Init();
    if (thread_count > 0)
        node->SetThreadPoolCount(thread_count);
    // Client transports. Server transports are started explicitly (M4).
    RR_SHARED_PTR<RobotRaconteur::TcpTransport> tcp = RR_MAKE_SHARED<RobotRaconteur::TcpTransport>(node);
    node->RegisterTransport(tcp);
    // Discovery listening, as RR's client node setup does (node-, link- and site-local).
    try
    {
        tcp->EnableNodeDiscoveryListening(RobotRaconteur::IPNodeDiscoveryFlags_NODE_LOCAL |
                                          RobotRaconteur::IPNodeDiscoveryFlags_LINK_LOCAL |
                                          RobotRaconteur::IPNodeDiscoveryFlags_SITE_LOCAL);
    }
    catch (std::exception&)
    {
        // No usable network interface: discovery is unavailable, connections by URL still work.
    }
    node->RegisterTransport(RR_MAKE_SHARED<RobotRaconteur::LocalTransport>(node));
    RR_SHARED_PTR<RobotRaconteur::IntraTransport> intra = RR_MAKE_SHARED<RobotRaconteur::IntraTransport>(node);
    intra->StartClient();
    node->RegisterTransport(intra);
    auto e = std::make_shared<NodeEntry>();
    e->node = node;
    e->owned = true;
    e->tcp = tcp;
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

void LuaHandlers::release(lua_State* L)
{
    for (auto& e : events)
        for (auto& l : e.second)
            luaL_unref(L, LUA_REGISTRYINDEX, l.ref);
    events.clear();
    for (auto& c : callbacks)
        luaL_unref(L, LUA_REGISTRYINDEX, c.second);
    callbacks.clear();
}

void StateContext::track(const std::shared_ptr<LuaHandlers>& h)
{
    for (auto& e : handlers_)
        if (e == h)
            return;
    handlers_.push_back(h);
}

void StateContext::track_service(const std::shared_ptr<NodeEntry>& node, const std::string& name)
{
    services_.push_back(std::make_pair(node, name));
}

void StateContext::sweep(lua_State* L)
{
    std::vector<std::shared_ptr<LuaHandlers> > keep;
    for (auto& h : handlers_)
    {
        if (h->closed.load() || h->owner.expired())
            h->release(L);
        else
            keep.push_back(h);
    }
    handlers_.swap(keep);
}

std::shared_ptr<NodeEntry> StateContext::default_node()
{
    if (!default_ || default_->shutdown)
        default_ = create_node(std::string(), 0);
    return default_;
}

} // namespace rl
