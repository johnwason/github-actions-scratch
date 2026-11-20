// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/streaming.hpp"

#include "rrlua/async.hpp"
#include "rrlua/client.hpp"
#include "rrlua/marshal.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
const char* const kListenerKey = "listener";

class NotifyTask : public Task
{
  public:
    explicit NotifyTask(const RR_SHARED_PTR<Notifier>& n) : n_(n) {}
    void run(lua_State* L) override
    {
        if (RR_SHARED_PTR<Notifier> n = n_.lock())
            n->notify(L);
    }

  private:
    RR_WEAK_PTR<Notifier> n_;
};

std::string direction_name(MemberDefinition_Direction d)
{
    switch (d)
    {
    case MemberDefinition_Direction_readonly:
        return "readonly";
    case MemberDefinition_Direction_writeonly:
        return "writeonly";
    default:
        return "both";
    }
}

int32_t lifespan_ms(double seconds)
{
    if (!(seconds >= 0) || std::isinf(seconds))
        return -1; // never expires
    double ms = std::round(seconds * 1000.0);
    if (ms > static_cast<double>(std::numeric_limits<int32_t>::max()))
        return -1;
    return static_cast<int32_t>(ms);
}

double lifespan_seconds(int32_t ms) { return ms < 0 ? -1.0 : ms / 1000.0; }

void push_value(lua_State* L, const RR_INTRUSIVE_PTR<MessageElement>& el, const RR_SHARED_PTR<TypeDefinition>& type,
                const RR_SHARED_PTR<DynamicStub>& stub)
{
    if (!lua_checkstack(L, 8))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, el, type, stub->marshal_context());
}

RR_INTRUSIVE_PTR<MessageElement> pack_value(const rl::Arg& v, const RR_SHARED_PTR<TypeDefinition>& type,
                                            const RR_SHARED_PTR<DynamicStub>& stub)
{
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(v.L, v.idx, type, stub->marshal_context());
    el->ElementName = "value";
    return el;
}

RR_SHARED_PTR<DynamicStub> stub_of(const RR_SHARED_PTR<ServiceStub>& s)
{
    RR_SHARED_PTR<DynamicStub> d = RR_DYNAMIC_POINTER_CAST<DynamicStub>(s);
    if (!d)
        throw InvalidOperationException("the client has been released");
    return d;
}
} // namespace

// ------------------------------------------------------------------------------------------
// Notifier

std::shared_ptr<LuaHandlers> Notifier::attach(lua_State* L, const boost::weak_ptr<void>& owner)
{
    StateContext* ctx = StateContext::require(L);
    std::shared_ptr<LuaHandlers> h;
    {
        boost::mutex::scoped_lock lock(m_);
        if (!handlers_)
        {
            queue_ = ctx->queue();
            handlers_ = std::make_shared<LuaHandlers>();
            handlers_->owner = owner;
        }
        h = handlers_;
    }
    ctx->track(h);
    return h;
}

std::shared_ptr<LuaHandlers> Notifier::handlers()
{
    boost::mutex::scoped_lock lock(m_);
    return handlers_;
}

long long Notifier::add_listener(lua_State* L, const boost::weak_ptr<void>& owner, const char* what, int fn_idx)
{
    if (lua_type(L, fn_idx) != LUA_TFUNCTION)
        throw InvalidArgumentException(std::string(what) + ": expected a function");
    std::shared_ptr<LuaHandlers> h = attach(L, owner);
    lua_pushvalue(L, fn_idx);
    LuaHandlers::Listener l;
    l.id = h->next_id++;
    l.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    l.since = 0;
    h->events[kListenerKey].push_back(l);
    listeners_.fetch_add(1);
    return l.id;
}

bool Notifier::remove_listener(lua_State* L, long long id)
{
    std::shared_ptr<LuaHandlers> h = handlers();
    if (!h)
        return false;
    std::vector<LuaHandlers::Listener>& v = h->events[kListenerKey];
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->id == id)
        {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            v.erase(it);
            listeners_.fetch_sub(1);
            return true;
        }
    return false;
}

std::vector<int> Notifier::listener_refs()
{
    std::vector<int> out;
    std::shared_ptr<LuaHandlers> h = handlers();
    if (!h || h->closed.load())
        return out;
    for (const LuaHandlers::Listener& l : h->events[kListenerKey])
        out.push_back(l.ref);
    return out;
}

void Notifier::post_notification(const RR_SHARED_PTR<Notifier>& self)
{
    if (listeners_.load() == 0)
        return;
    if (pending_.exchange(true))
        return; // one is already queued; it will see this value or packet too
    std::shared_ptr<DispatchQueue> q;
    {
        boost::mutex::scoped_lock lock(m_);
        q = queue_.lock();
    }
    if (!q || !q->post(std::unique_ptr<Task>(new NotifyTask(self))))
        pending_.store(false);
}

void Notifier::mark_closed()
{
    boost::mutex::scoped_lock lock(m_);
    if (handlers_)
        handlers_->closed.store(true);
}

// ------------------------------------------------------------------------------------------
// Wires

LuaWireConnection::LuaWireConnection(const RR_SHARED_PTR<WireBase>& parent, const RR_SHARED_PTR<TypeDefinition>& t,
                                     MemberDefinition_Direction direction)
    : WireConnectionBase(parent, 0, direction), type(t)
{}

bool LuaWireConnection::try_in_value(RR_INTRUSIVE_PTR<MessageElement>& v)
{
    RR_INTRUSIVE_PTR<RRValue> o;
    TimeSpec ts;
    if (!TryGetInValueBase(o, ts))
        return false;
    v = rr_cast<MessageElement>(o);
    return true;
}

bool LuaWireConnection::try_out_value(RR_INTRUSIVE_PTR<MessageElement>& v)
{
    RR_INTRUSIVE_PTR<RRValue> o;
    TimeSpec ts;
    if (!TryGetOutValueBase(o, ts))
        return false;
    v = rr_cast<MessageElement>(o);
    return true;
}

void LuaWireConnection::set_out_value(const RR_INTRUSIVE_PTR<MessageElement>& v) { SetOutValueBase(v); }

void LuaWireConnection::fire_WireValueChanged(const RR_INTRUSIVE_PTR<RRValue>&, TimeSpec)
{
    post_notification(RR_DYNAMIC_POINTER_CAST<LuaWireConnection>(shared_from_this()));
}

void LuaWireConnection::fire_WireClosedCallback() { mark_closed(); }

void LuaWireConnection::notify(lua_State* L)
{
    notification_started();
    std::vector<int> refs = listener_refs();
    if (refs.empty())
        return;
    RR_INTRUSIVE_PTR<MessageElement> el;
    if (!try_in_value(el))
        return;
    RR_SHARED_PTR<LuaWireClient> parent = RR_DYNAMIC_POINTER_CAST<LuaWireClient>(GetParent());
    RR_SHARED_PTR<DynamicStub> stub = stub_of(parent->GetStub());
    for (int ref : refs)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        push_value(L, el, type, stub);
        call_lua(L, 1, 0);
    }
}

LuaWireClient::LuaWireClient(const std::string& name, const RR_SHARED_PTR<ServiceStub>& stub,
                             const RR_SHARED_PTR<TypeDefinition>& t, MemberDefinition_Direction direction)
    : WireClientBase(name, stub, direction), type(t)
{
    rawelements = true;
}

RR_SHARED_PTR<WireConnectionBase> LuaWireClient::CreateNewWireConnection(MemberDefinition_Direction direction)
{
    return RR_MAKE_SHARED<LuaWireConnection>(RR_DYNAMIC_POINTER_CAST<WireBase>(shared_from_this()), type, direction);
}

void LuaWireClient::connect(
    boost::function<void(const RR_SHARED_PTR<WireConnectionBase>&, const RR_SHARED_PTR<RobotRaconteurException>&)>
        handler,
    int32_t timeout)
{
    AsyncConnect_internal(RR_MOVE(handler), timeout);
}

void LuaWireClient::peek(bool in,
                         boost::function<void(const RR_INTRUSIVE_PTR<RRValue>&, const TimeSpec&,
                                              const RR_SHARED_PTR<RobotRaconteurException>&)>
                             handler,
                         int32_t timeout)
{
    if (in)
        AsyncPeekInValueBase(RR_MOVE(handler), timeout);
    else
        AsyncPeekOutValueBase(RR_MOVE(handler), timeout);
}

void LuaWireClient::poke(const RR_INTRUSIVE_PTR<MessageElement>& v,
                         boost::function<void(const RR_SHARED_PTR<RobotRaconteurException>&)> handler, int32_t timeout)
{
    AsyncPokeOutValueBase(v, RR_MOVE(handler), timeout);
}

// ------------------------------------------------------------------------------------------
// Pipes

LuaPipeEndpoint::LuaPipeEndpoint(const RR_SHARED_PTR<PipeBase>& parent, int32_t index,
                                 const RR_SHARED_PTR<TypeDefinition>& t, bool unreliable,
                                 MemberDefinition_Direction direction)
    : PipeEndpointBase(parent, index, 0, unreliable, direction), type(t), index_(index)
{}

void LuaPipeEndpoint::send(const RR_INTRUSIVE_PTR<MessageElement>& v,
                           boost::function<void(uint32_t, const RR_SHARED_PTR<RobotRaconteurException>&)> handler)
{
    AsyncSendPacketBase(v, RR_MOVE(handler));
}

RR_INTRUSIVE_PTR<MessageElement> LuaPipeEndpoint::next(bool peek)
{
    if (Available() == 0)
        return RR_INTRUSIVE_PTR<MessageElement>();
    return rr_cast<MessageElement>(peek ? PeekPacketBase() : ReceivePacketBase());
}

void LuaPipeEndpoint::fire_PipeEndpointClosedCallback() { mark_closed(); }

void LuaPipeEndpoint::fire_PacketReceivedEvent()
{
    post_notification(RR_DYNAMIC_POINTER_CAST<LuaPipeEndpoint>(shared_from_this()));
}

void LuaPipeEndpoint::fire_PacketAckReceivedEvent(uint32_t) {}

void LuaPipeEndpoint::notify(lua_State* L)
{
    notification_started();
    for (int ref : listener_refs())
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        call_lua(L, 0, 0);
    }
}

LuaPipeClient::LuaPipeClient(const std::string& name, const RR_SHARED_PTR<ServiceStub>& stub,
                             const RR_SHARED_PTR<TypeDefinition>& t, bool unreliable,
                             MemberDefinition_Direction direction)
    : PipeClientBase(name, stub, unreliable, direction), type(t)
{
    rawelements = true;
}

RR_SHARED_PTR<PipeEndpointBase> LuaPipeClient::CreateNewPipeEndpoint(int32_t index, bool unreliable,
                                                                      MemberDefinition_Direction direction)
{
    return RR_MAKE_SHARED<LuaPipeEndpoint>(RR_DYNAMIC_POINTER_CAST<PipeBase>(shared_from_this()), index, type,
                                           unreliable, direction);
}

void LuaPipeClient::connect(
    int32_t index,
    boost::function<void(const RR_SHARED_PTR<PipeEndpointBase>&, const RR_SHARED_PTR<RobotRaconteurException>&)>
        handler,
    int32_t timeout)
{
    AsyncConnect_internal(index, RR_MOVE(handler), timeout);
}

// ------------------------------------------------------------------------------------------
// WireConnection (Lua handle)

WireConnection::WireConnection(lua_State* L, RR_SHARED_PTR<LuaWireConnection> c, RR_SHARED_PTR<DynamicStub> stub)
    : c_(std::move(c)), stub_(std::move(stub))
{
    c_->attach(L, boost::weak_ptr<void>(c_));
}

RR_SHARED_PTR<LuaWireConnection> WireConnection::conn()
{
    if (closed_)
        throw InvalidOperationException("wire connection has been closed");
    return c_;
}

rl::Ret WireConnection::getInValue(lua_State* L)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    RR_INTRUSIVE_PTR<MessageElement> el;
    if (!c->try_in_value(el))
        throw ValueNotSetException("wire in value is not set (or has expired)");
    push_value(L, el, c->type, stub_);
    return {1};
}

rl::Ret WireConnection::tryGetInValue(lua_State* L)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    RR_INTRUSIVE_PTR<MessageElement> el;
    if (!c->try_in_value(el))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    push_value(L, el, c->type, stub_);
    return {2};
}

rl::Ret WireConnection::getOutValue(lua_State* L)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    RR_INTRUSIVE_PTR<MessageElement> el;
    if (!c->try_out_value(el))
        throw ValueNotSetException("wire out value is not set (or has expired)");
    push_value(L, el, c->type, stub_);
    return {1};
}

rl::Ret WireConnection::tryGetOutValue(lua_State* L)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    RR_INTRUSIVE_PTR<MessageElement> el;
    if (!c->try_out_value(el))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    push_value(L, el, c->type, stub_);
    return {2};
}

void WireConnection::setOutValue(rl::Arg value)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    c->set_out_value(pack_value(value, c->type, stub_));
}

bool WireConnection::inValueValid() { return conn()->GetInValueValid(); }
bool WireConnection::outValueValid() { return conn()->GetOutValueValid(); }
double WireConnection::getInValueLifespan() { return lifespan_seconds(conn()->GetInValueLifespan()); }
void WireConnection::setInValueLifespan(double seconds) { conn()->SetInValueLifespan(lifespan_ms(seconds)); }
double WireConnection::getOutValueLifespan() { return lifespan_seconds(conn()->GetOutValueLifespan()); }
void WireConnection::setOutValueLifespan(double seconds) { conn()->SetOutValueLifespan(lifespan_ms(seconds)); }
std::string WireConnection::direction() { return direction_name(conn()->Direction()); }

long long WireConnection::addValueChangedListener(lua_State* L, rl::Arg fn)
{
    RR_SHARED_PTR<LuaWireConnection> c = conn();
    return c->add_listener(L, boost::weak_ptr<void>(c), "addValueChangedListener", fn.idx);
}

bool WireConnection::removeValueChangedListener(lua_State* L, long long id) { return c_->remove_listener(L, id); }

void WireConnection::close(lua_State* L)
{
    if (closed_)
        return;
    closed_ = true;
    auto c = std::make_shared<VoidCompletion>(blocking_queue(L));
    c_->AsyncClose(c->handler(), request_timeout(c_->GetNode()));
    try
    {
        c->wait(L);
    }
    catch (const RobotRaconteurException&)
    {
        // Already closed by the other side or by the connection closing: nothing to do.
    }
    StateContext::require(L)->sweep(L);
}

// ------------------------------------------------------------------------------------------
// PipeEndpoint (Lua handle)

PipeEndpoint::PipeEndpoint(lua_State* L, RR_SHARED_PTR<LuaPipeEndpoint> e, RR_SHARED_PTR<DynamicStub> stub)
    : e_(std::move(e)), stub_(std::move(stub))
{
    e_->attach(L, boost::weak_ptr<void>(e_));
}

RR_SHARED_PTR<LuaPipeEndpoint> PipeEndpoint::ep()
{
    if (closed_)
        throw InvalidOperationException("pipe endpoint has been closed");
    return e_;
}

double PipeEndpoint::sendPacket(rl::Arg value)
{
    RR_SHARED_PTR<LuaPipeEndpoint> e = ep();
    RR_INTRUSIVE_PTR<MessageElement> el = pack_value(value, e->type, stub_);
    auto c = std::make_shared<Completion<uint32_t> >(blocking_queue(value.L));
    e->send(el, c->handler());
    return static_cast<double>(c->wait(value.L));
}

rl::Ret PipeEndpoint::receivePacket(lua_State* L)
{
    RR_SHARED_PTR<LuaPipeEndpoint> e = ep();
    RR_INTRUSIVE_PTR<MessageElement> el = e->next(false);
    if (!el)
        throw InvalidOperationException("no packet available");
    push_value(L, el, e->type, stub_);
    return {1};
}

rl::Ret PipeEndpoint::tryReceivePacket(lua_State* L)
{
    RR_SHARED_PTR<LuaPipeEndpoint> e = ep();
    RR_INTRUSIVE_PTR<MessageElement> el = e->next(false);
    if (!el)
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    push_value(L, el, e->type, stub_);
    return {2};
}

rl::Ret PipeEndpoint::peekNextPacket(lua_State* L)
{
    RR_SHARED_PTR<LuaPipeEndpoint> e = ep();
    RR_INTRUSIVE_PTR<MessageElement> el = e->next(true);
    if (!el)
        throw InvalidOperationException("no packet available");
    push_value(L, el, e->type, stub_);
    return {1};
}

int PipeEndpoint::available() { return static_cast<int>(ep()->Available()); }
int PipeEndpoint::index() { return e_->index(); }
bool PipeEndpoint::isUnreliable() { return e_->IsUnreliable(); }
std::string PipeEndpoint::direction() { return direction_name(e_->Direction()); }
bool PipeEndpoint::getRequestPacketAck() { return ep()->GetRequestPacketAck(); }
void PipeEndpoint::setRequestPacketAck(bool ack) { ep()->SetRequestPacketAck(ack); }

long long PipeEndpoint::addPacketReceivedListener(lua_State* L, rl::Arg fn)
{
    RR_SHARED_PTR<LuaPipeEndpoint> e = ep();
    return e->add_listener(L, boost::weak_ptr<void>(e), "addPacketReceivedListener", fn.idx);
}

bool PipeEndpoint::removePacketReceivedListener(lua_State* L, long long id) { return e_->remove_listener(L, id); }

void PipeEndpoint::close(lua_State* L)
{
    if (closed_)
        return;
    closed_ = true;
    auto c = std::make_shared<VoidCompletion>(blocking_queue(L));
    e_->AsyncClose(c->handler(), request_timeout(e_->GetNode()));
    try
    {
        c->wait(L);
    }
    catch (const RobotRaconteurException&)
    {
    }
    StateContext::require(L)->sweep(L);
}

} // namespace rl
