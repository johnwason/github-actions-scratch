// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/subscription.hpp"

#include "rrlua/async.hpp"
#include "rrlua/client.hpp"
#include "rrlua/lua.hpp"
#include "rrlua/marshal.hpp"
#include "rrlua/streaming.hpp"

#include <cmath>
#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
typedef RR_INTRUSIVE_PTR<MessageElement> Raw;

std::vector<std::string> string_list(const rl::Arg& a, const char* what)
{
    std::vector<std::string> out;
    lua_State* L = a.L;
    if (lua_type(L, a.idx) == LUA_TSTRING)
        out.push_back(lua_tostring(L, a.idx));
    else if (lua_type(L, a.idx) == LUA_TTABLE)
    {
        size_t n = lua_rawlen(L, a.idx);
        for (size_t i = 1; i <= n; ++i)
        {
            lua_rawgeti(L, a.idx, static_cast<int>(i));
            if (lua_type(L, -1) != LUA_TSTRING)
            {
                lua_pop(L, 1);
                throw InvalidArgumentException(std::string(what) + " must be strings");
            }
            out.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    if (out.empty())
        throw InvalidArgumentException(std::string(what) + ": give a string or a list of strings");
    return out;
}

// Unpack a value delivered by a wire connection of one of our stubs.
void push_wire_value(lua_State* L, const RR_INTRUSIVE_PTR<RRValue>& v, const RR_SHARED_PTR<WireConnectionBase>& c)
{
    RR_SHARED_PTR<LuaWireConnection> lc = RR_DYNAMIC_POINTER_CAST<LuaWireConnection>(c);
    RR_SHARED_PTR<DynamicStub> stub = lc ? lc->stub() : RR_SHARED_PTR<DynamicStub>();
    if (!stub)
        throw InvalidOperationException("the wire connection was released");
    if (!lua_checkstack(L, 8))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, rr_cast<MessageElement>(v), lc->type, stub->marshal_context());
}

void push_pipe_value(lua_State* L, const RR_INTRUSIVE_PTR<RRValue>& v, const RR_SHARED_PTR<PipeEndpointBase>& e)
{
    RR_SHARED_PTR<LuaPipeEndpoint> le = RR_DYNAMIC_POINTER_CAST<LuaPipeEndpoint>(e);
    RR_SHARED_PTR<DynamicStub> stub = le ? le->stub() : RR_SHARED_PTR<DynamicStub>();
    if (!stub)
        throw InvalidOperationException("the pipe endpoint was released");
    if (!lua_checkstack(L, 8))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, rr_cast<MessageElement>(v), le->type, stub->marshal_context());
}

// The stub of a connected client at the subscription's root (for the type of an outgoing value).
RR_SHARED_PTR<DynamicStub> any_root_stub(const RR_SHARED_PTR<ServiceSubscription>& sub, const std::string& path)
{
    if (!path.empty())
        throw InvalidOperationException("sending through a subscription needs a member of the service root");
    std::map<ServiceSubscriptionClientID, RR_SHARED_PTR<RRObject> > c = sub->GetConnectedClients();
    for (auto& e : c)
        if (RR_SHARED_PTR<DynamicStub> s = RR_DYNAMIC_POINTER_CAST<DynamicStub>(e.second))
            return s;
    return RR_SHARED_PTR<DynamicStub>();
}
} // namespace

// ------------------------------------------------------------------------------------------
// Subscription

Subscription::Subscription(std::shared_ptr<NodeEntry> node, RR_SHARED_PTR<ServiceSubscription> sub)
    : node_(std::move(node)), sub_(std::move(sub))
{}

Client* Subscription::wrap(lua_State* L, const RR_SHARED_PTR<RRObject>& o)
{
    RR_SHARED_PTR<DynamicStub> s = RR_DYNAMIC_POINTER_CAST<DynamicStub>(o);
    if (!s)
        throw InvalidOperationException("the subscription's client is not a dynamic Lua stub");
    return new Client(L, s, node_, false); // the subscription owns the connection
}

rl::Ret Subscription::getConnectedClients(lua_State* L)
{
    std::map<ServiceSubscriptionClientID, RR_SHARED_PTR<RRObject> > c = sub_->GetConnectedClients();
    lua_createtable(L, static_cast<int>(c.size()), 0);
    int t = lua_gettop(L);
    int i = 0;
    for (auto& e : c)
    {
        push_client(L, wrap(L, e.second));
        lua_rawseti(L, t, ++i);
    }
    return {1};
}

Client* Subscription::getDefaultClient(lua_State* L)
{
    RR_SHARED_PTR<RRObject> o;
    if (!sub_->TryGetDefaultClient<RRObject>(o) || !o)
        throw ConnectionException("no client connected");
    return wrap(L, o);
}

Client* Subscription::getDefaultClientWait(lua_State* L, double timeout)
{
    auto c = std::make_shared<Completion<RR_SHARED_PTR<RRObject> > >(blocking_queue(L));
    int32_t ms = !(timeout >= 0) || std::isinf(timeout) ? RR_TIMEOUT_INFINITE : static_cast<int32_t>(timeout * 1000.0);
    sub_->AsyncGetDefaultClient<RRObject>(c->handler(), ms);
    return wrap(L, c->wait(L));
}

WireSubscriptionH* Subscription::subscribeWire(const std::string& name) { return subscribeWire(name, ""); }

WireSubscriptionH* Subscription::subscribeWire(const std::string& name, const std::string& path)
{
    return new WireSubscriptionH(sub_->SubscribeWire<Raw>(name, path), sub_, name, path);
}

PipeSubscriptionH* Subscription::subscribePipe(const std::string& name) { return subscribePipe(name, ""); }

PipeSubscriptionH* Subscription::subscribePipe(const std::string& name, const std::string& path)
{
    return new PipeSubscriptionH(sub_->SubscribePipe<Raw>(name, path), sub_, name, path);
}

void Subscription::close() { sub_->Close(); }

// ------------------------------------------------------------------------------------------
// Wire and pipe subscriptions

WireSubscriptionH::WireSubscriptionH(RR_SHARED_PTR<WireSubscriptionBase> w, RR_SHARED_PTR<ServiceSubscription> sub,
                                     std::string name, std::string path)
    : w_(std::move(w)), sub_(std::move(sub)), name_(std::move(name)), path_(std::move(path))
{}

rl::Ret WireSubscriptionH::getInValue(lua_State* L)
{
    RR_INTRUSIVE_PTR<RRValue> v;
    RR_SHARED_PTR<WireConnectionBase> c;
    if (!w_->TryGetInValueBase(v, NULL, &c))
        throw ValueNotSetException("no wire value received (or it has expired)");
    push_wire_value(L, v, c);
    return {1};
}

rl::Ret WireSubscriptionH::tryGetInValue(lua_State* L)
{
    RR_INTRUSIVE_PTR<RRValue> v;
    RR_SHARED_PTR<WireConnectionBase> c;
    if (!w_->TryGetInValueBase(v, NULL, &c))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    push_wire_value(L, v, c);
    return {2};
}

int WireSubscriptionH::activeWireConnectionCount() { return static_cast<int>(w_->GetActiveWireConnectionCount()); }

void WireSubscriptionH::setInValueLifespan(double seconds)
{
    w_->SetInValueLifespan(!(seconds >= 0) ? -1 : static_cast<int32_t>(seconds * 1000.0 + 0.5));
}

void WireSubscriptionH::setOutValueAll(rl::Arg value)
{
    RR_SHARED_PTR<DynamicStub> s = any_root_stub(sub_, path_);
    if (!s)
        return;
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(value.L, value.idx, s->wire(name_)->type, s->marshal_context());
    el->ElementName = "value";
    w_->SetOutValueAllBase(el);
}

void WireSubscriptionH::close() { w_->Close(); }

PipeSubscriptionH::PipeSubscriptionH(RR_SHARED_PTR<PipeSubscriptionBase> p, RR_SHARED_PTR<ServiceSubscription> sub,
                                     std::string name, std::string path)
    : p_(std::move(p)), sub_(std::move(sub)), name_(std::move(name)), path_(std::move(path))
{}

rl::Ret PipeSubscriptionH::receivePacket(lua_State* L)
{
    RR_INTRUSIVE_PTR<RRValue> v;
    RR_SHARED_PTR<PipeEndpointBase> e;
    if (!p_->TryReceivePacketBaseWait(v, 0, false, &e))
        throw InvalidOperationException("no packet available");
    push_pipe_value(L, v, e);
    return {1};
}

rl::Ret PipeSubscriptionH::tryReceivePacket(lua_State* L)
{
    RR_INTRUSIVE_PTR<RRValue> v;
    RR_SHARED_PTR<PipeEndpointBase> e;
    if (!p_->TryReceivePacketBaseWait(v, 0, false, &e))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    push_pipe_value(L, v, e);
    return {2};
}

int PipeSubscriptionH::available() { return static_cast<int>(p_->Available()); }

int PipeSubscriptionH::activePipeEndpointCount() { return static_cast<int>(p_->GetActivePipeEndpointCount()); }

void PipeSubscriptionH::sendPacketAll(rl::Arg value)
{
    RR_SHARED_PTR<DynamicStub> s = any_root_stub(sub_, path_);
    if (!s)
        return;
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(value.L, value.idx, s->pipe(name_)->type, s->marshal_context());
    el->ElementName = "value";
    p_->AsyncSendPacketAllBase(el);
}

void PipeSubscriptionH::close() { p_->Close(); }

// ------------------------------------------------------------------------------------------
// Node functions

Subscription* subscribe_service(const std::shared_ptr<NodeEntry>& node, rl::Arg urls)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    return new Subscription(node, node->node->SubscribeService(string_list(urls, "subscribeService urls")));
}

Subscription* subscribe_service_by_type(const std::shared_ptr<NodeEntry>& node, rl::Arg types)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    return new Subscription(node, node->node->SubscribeServiceByType(string_list(types, "subscribeServiceByType types")));
}

} // namespace rl
