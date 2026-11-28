// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/server.hpp"

#include "rrlua/array.hpp"
#include "rrlua/async.hpp"
#include "rrlua/client.hpp"
#include "rrlua/generator.hpp"
#include "rrlua/memory.hpp"
#include "rrlua/marshal.hpp"

#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
const char* const kHandlersKey = "handlers";

template <typename T>
RR_SHARED_PTR<T> member(const RR_SHARED_PTR<ServiceEntryDefinition>& def, const std::string& name, const char* kind)
{
    for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        if (m->Name == name)
            if (RR_SHARED_PTR<T> t = RR_DYNAMIC_POINTER_CAST<T>(m))
                return t;
    throw MemberNotFoundException(std::string(kind) + " '" + name + "' not found in " + def->ResolveQualifiedName());
}

MarshalContext server_context(const RR_SHARED_PTR<RobotRaconteurNode>& node,
                              const RR_SHARED_PTR<ServiceEntryDefinition>& def)
{
    MarshalContext ctx;
    ctx.node = node;
    if (RR_SHARED_PTR<ServiceDefinition> sd = def->ServiceDefinition_.lock())
        ctx.defs.push_back(sd);
    return ctx;
}

RR_SHARED_PTR<ServiceEntryDefinition> find_object_type(const RR_SHARED_PTR<RobotRaconteurNode>& node,
                                                       const std::string& qualified)
{
    boost::tuple<boost::string_ref, boost::string_ref> parts = SplitQualifiedName(qualified);
    RR_SHARED_PTR<ServiceFactory> f = node->GetServiceType(parts.get<0>());
    for (const RR_SHARED_PTR<ServiceEntryDefinition>& e : f->ServiceDef()->Objects)
        if (e->Name == parts.get<1>())
            return e;
    throw ServiceException("object type " + qualified + " not found; register its service type first");
}

// The text of a Lua error as an RR error for the client.
RR_SHARED_PTR<RobotRaconteurException> lua_failure(const std::string& msg)
{
    return RR_MAKE_SHARED<OperationFailedException>(msg);
}

// One property or function request, answered on the Lua thread.
class ServerTask : public Task
{
  public:
    ServerTask(LuaServiceSkel::Kind kind, const RR_SHARED_PTR<LuaServiceSkel>& skel,
               const RR_INTRUSIVE_PTR<MessageEntry>& m, const RR_SHARED_PTR<ServerEndpoint>& ep)
        : kind_(kind), skel_(skel), m_(m), ep_(ep)
    {}

    // Dropped unrun (the state closed): answer with an error so the client does not wait.
    ~ServerTask() override
    {
        if (answered_)
            return;
        try
        {
            LuaServiceSkel::answer(kind_, skel_, RR_INTRUSIVE_PTR<MessageElement>(),
                                   RR_MAKE_SHARED<OperationAbortedException>("the Lua service's state has closed"), m_,
                                   ep_);
        }
        catch (...)
        {
        }
    }

    void run(lua_State* L) override
    {
        answered_ = true;
        RR_INTRUSIVE_PTR<MessageElement> value;
        RR_SHARED_PTR<RobotRaconteurException> err;
        try
        {
            value = invoke(L);
        }
        catch (const RobotRaconteurException& e)
        {
            err = RR_MAKE_SHARED<RobotRaconteurException>(e);
        }
        catch (const std::exception& e)
        {
            err = lua_failure(e.what());
        }
        LuaServiceSkel::answer(kind_, skel_, value, err, m_, ep_);
    }

  private:
    RR_INTRUSIVE_PTR<MessageElement> invoke(lua_State* L)
    {
        RR_SHARED_PTR<LuaServiceSkel> skel = RR_DYNAMIC_POINTER_CAST<LuaServiceSkel>(skel_.lock());
        if (!skel)
            throw OperationAbortedException("the service object was released");
        RR_SHARED_PTR<LuaServiceObject> obj = skel->object();
        std::shared_ptr<LuaHandlers> h = obj->handlers;
        auto it = h->callbacks.find(kHandlersKey);
        if (h->closed.load() || it == h->callbacks.end())
            throw OperationAbortedException("the Lua service has been closed");
        std::string name = m_->MemberName.str().to_string();
        MarshalContext ctx = server_context(skel->RRGetNode(), obj->type);
        const char* fn = kind_ == LuaServiceSkel::Kind::get   ? "getProperty"
                         : kind_ == LuaServiceSkel::Kind::set ? "setProperty"
                                                              : "callFunction";
        if (!lua_checkstack(L, 32))
            throw std::runtime_error("Lua stack overflow");
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        lua_pushstring(L, fn);
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            throw MemberNotFoundException(std::string("the Lua service has no ") + fn + " handler");
        }
        lua_remove(L, -2); // the handler table
        lua_pushlstring(L, name.data(), name.size());
        RR_INTRUSIVE_PTR<MessageElement> out;
        try
        {
            switch (kind_)
            {
            case LuaServiceSkel::Kind::get: {
                RR_SHARED_PTR<PropertyDefinition> p = member<PropertyDefinition>(obj->type, name, "property");
                call_lua(L, 1, 1);
                out = pack_element(L, -1, p->Type, ctx);
                out->ElementName = "value";
                break;
            }
            case LuaServiceSkel::Kind::set: {
                RR_SHARED_PTR<PropertyDefinition> p = member<PropertyDefinition>(obj->type, name, "property");
                unpack_element(L, m_->FindElement("value"), p->Type, ctx);
                call_lua(L, 2, 0);
                break;
            }
            case LuaServiceSkel::Kind::function: {
                RR_SHARED_PTR<FunctionDefinition> f = member<FunctionDefinition>(obj->type, name, "function");
                if (f->IsGenerator())
                {
                    // The handler returns a Lua generator; serve it and answer with its index.
                    RR_SHARED_PTR<TypeDefinition> rt, pt;
                    std::vector<RR_SHARED_PTR<TypeDefinition> > params;
                    generator_types(f, rt, pt, params);
                    for (const RR_SHARED_PTR<TypeDefinition>& p : params)
                        unpack_element(L, m_->FindElement(p->Name), p, ctx);
                    call_lua(L, 1 + static_cast<int>(params.size()), 1);
                    int32_t index = register_lua_generator(L, skel, name, f, ctx, ep_);
                    out = CreateMessageElement("index", ScalarToRRArray<int32_t>(index));
                    break;
                }
                for (const RR_SHARED_PTR<TypeDefinition>& p : f->Parameters)
                    unpack_element(L, m_->FindElement(p->Name), p, ctx);
                bool is_void = f->ReturnType->Type == DataTypes_void_t;
                call_lua(L, 1 + static_cast<int>(f->Parameters.size()), is_void ? 0 : 1);
                if (!is_void)
                {
                    out = pack_element(L, -1, f->ReturnType, ctx);
                    out->ElementName = "return";
                }
                break;
            }
            }
        }
        catch (...)
        {
            lua_settop(L, top);
            throw;
        }
        lua_settop(L, top);
        return out;
    }

    LuaServiceSkel::Kind kind_;
    RR_WEAK_PTR<ServiceSkel> skel_;
    RR_INTRUSIVE_PTR<MessageEntry> m_;
    RR_SHARED_PTR<ServerEndpoint> ep_;
    bool answered_ = false;
};
} // namespace


// ------------------------------------------------------------------------------------------
// Wire and pipe members

void LuaWireMember::use(Mode m)
{
    boost::mutex::scoped_lock lock(m_);
    if (mode_ == m)
        return;
    if (mode_ != Mode::none)
        throw InvalidOperationException("this wire is already used as a " +
                                        std::string(mode_ == Mode::broadcaster ? "broadcaster" : "receiver"));
    mode_ = m;
    if (m == Mode::broadcaster)
        bcast_ = RR_MAKE_SHARED<WireBroadcaster<RawElement> >();
    else
        recv_ = RR_MAKE_SHARED<WireUnicastReceiver<RawElement> >();
    if (server_)
    {
        if (bcast_)
            bcast_->Init(server_);
        if (recv_)
            recv_->Init(server_);
    }
}

void LuaWireMember::attach(const RR_SHARED_PTR<WireServer<RawElement> >& server)
{
    boost::mutex::scoped_lock lock(m_);
    if (server_)
        return; // the object is served by one skel
    server_ = server;
    if (bcast_)
    {
        bcast_->Init(server_);
        if (pending_)
            bcast_->SetOutValue(pending_);
    }
    if (recv_)
    {
        recv_->Init(server_);
        recv_->SetInValueLifespan(lifespan_);
    }
}

void LuaWireMember::set_out_value(const RawElement& v)
{
    boost::mutex::scoped_lock lock(m_);
    pending_ = v;
    if (server_ && bcast_)
        bcast_->SetOutValue(v);
}

bool LuaWireMember::try_in_value(RawElement& v)
{
    boost::mutex::scoped_lock lock(m_);
    if (!recv_ || !server_)
        return false;
    TimeSpec ts;
    uint32_t ep = 0;
    return recv_->TryGetInValue(v, ts, ep);
}

void LuaWireMember::set_in_value_lifespan(int32_t ms)
{
    boost::mutex::scoped_lock lock(m_);
    lifespan_ = ms;
    if (recv_ && server_)
        recv_->SetInValueLifespan(ms);
}

void LuaWireMember::shutdown()
{
    boost::mutex::scoped_lock lock(m_);
    if (server_)
        server_->Shutdown();
}

void LuaPipeMember::attach(const RR_SHARED_PTR<PipeServer<RawElement> >& server)
{
    boost::mutex::scoped_lock lock(m_);
    if (server_)
        return;
    server_ = server;
    std::weak_ptr<LuaPipeMember> self = shared_from_this();
    server_->SetPipeConnectCallback([self](const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& ep) {
        if (std::shared_ptr<LuaPipeMember> p = self.lock())
            p->connected(ep);
    });
}

void LuaPipeMember::connected(const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& ep)
{
    boost::mutex::scoped_lock lock(m_);
    std::vector<RR_WEAK_PTR<RobotRaconteur::PipeEndpoint<RawElement> > > keep;
    for (auto& w : endpoints_)
        if (!w.expired())
            keep.push_back(w);
    keep.push_back(ep);
    endpoints_.swap(keep);
}

std::vector<RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> > > LuaPipeMember::live()
{
    boost::mutex::scoped_lock lock(m_);
    std::vector<RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> > > out;
    for (auto& w : endpoints_)
        if (RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> > e = w.lock())
            out.push_back(e);
    return out;
}

int LuaPipeMember::send(const RawElement& v)
{
    int n = 0;
    for (const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& e : live())
    {
        try
        {
            // Each endpoint gets its own copy: an element's name is set when it is sent.
            e->AsyncSendPacket(ShallowCopyMessageElement(v),
                               [](uint32_t, const RR_SHARED_PTR<RobotRaconteurException>&) {});
            ++n;
        }
        catch (const std::exception&)
        {
            // The endpoint closed meanwhile.
        }
    }
    return n;
}

RawElement LuaPipeMember::receive()
{
    for (const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& e : live())
        if (e->Available() > 0)
            return e->ReceivePacket();
    return RawElement();
}

int LuaPipeMember::available()
{
    size_t n = 0;
    for (const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& e : live())
        n += e->Available();
    return static_cast<int>(n);
}

int LuaPipeMember::endpoint_count() { return static_cast<int>(live().size()); }

void LuaPipeMember::shutdown()
{
    boost::mutex::scoped_lock lock(m_);
    if (server_)
        server_->Shutdown();
}

// ------------------------------------------------------------------------------------------
// LuaServiceObject

LuaServiceObject::LuaServiceObject(const RR_SHARED_PTR<ServiceEntryDefinition>& t, std::weak_ptr<DispatchQueue> q,
                                   std::shared_ptr<LuaHandlers> h)
    : type(t), type_name(t->ResolveQualifiedName()), queue(std::move(q)), handlers(std::move(h))
{}

void LuaServiceObject::set_child(const std::string& name, const std::string& index,
                                 const RR_SHARED_PTR<LuaServiceObject>& o)
{
    boost::mutex::scoped_lock lock(m_);
    children_[name + "[" + index + "]"] = o;
}

RR_SHARED_PTR<LuaServiceObject> LuaServiceObject::child(const std::string& name, const std::string& index)
{
    boost::mutex::scoped_lock lock(m_);
    auto it = children_.find(name + "[" + index + "]");
    return it == children_.end() ? RR_SHARED_PTR<LuaServiceObject>() : it->second;
}

std::shared_ptr<LuaWireMember> LuaServiceObject::wire(const std::string& name)
{
    member<WireDefinition>(type, name, "wire");
    boost::mutex::scoped_lock lock(m_);
    std::shared_ptr<LuaWireMember>& w = wires_[name];
    if (!w)
        w = std::make_shared<LuaWireMember>();
    return w;
}

std::shared_ptr<LuaPipeMember> LuaServiceObject::pipe(const std::string& name)
{
    member<PipeDefinition>(type, name, "pipe");
    boost::mutex::scoped_lock lock(m_);
    std::shared_ptr<LuaPipeMember>& p = pipes_[name];
    if (!p)
        p = std::make_shared<LuaPipeMember>();
    return p;
}

void LuaServiceObject::set_memory(const std::string& name, const std::shared_ptr<MemoryAccess>& m)
{
    boost::mutex::scoped_lock lock(m_);
    memories_[name] = m;
}

std::shared_ptr<MemoryAccess> LuaServiceObject::memory(const std::string& name)
{
    boost::mutex::scoped_lock lock(m_);
    auto it = memories_.find(name);
    return it == memories_.end() ? std::shared_ptr<MemoryAccess>() : it->second;
}

void LuaServiceObject::shutdown_members()
{
    std::vector<std::shared_ptr<LuaWireMember> > w;
    std::vector<std::shared_ptr<LuaPipeMember> > p;
    {
        boost::mutex::scoped_lock lock(m_);
        for (auto& e : wires_)
            w.push_back(e.second);
        for (auto& e : pipes_)
            p.push_back(e.second);
    }
    for (auto& e : w)
        e->shutdown();
    for (auto& e : p)
        e->shutdown();
}

void LuaServiceObject::add_skel(const RR_SHARED_PTR<LuaServiceSkel>& s)
{
    boost::mutex::scoped_lock lock(m_);
    std::vector<RR_WEAK_PTR<LuaServiceSkel> > keep;
    for (auto& w : skels_)
        if (!w.expired())
            keep.push_back(w);
    keep.push_back(s);
    skels_.swap(keep);
}

std::vector<RR_SHARED_PTR<LuaServiceSkel> > LuaServiceObject::skels()
{
    boost::mutex::scoped_lock lock(m_);
    std::vector<RR_SHARED_PTR<LuaServiceSkel> > out;
    for (auto& w : skels_)
        if (RR_SHARED_PTR<LuaServiceSkel> s = w.lock())
            out.push_back(s);
    return out;
}

// ------------------------------------------------------------------------------------------
// LuaServiceSkel

void LuaServiceSkel::Init(boost::string_ref s, const RR_SHARED_PTR<RRObject>& o, const RR_SHARED_PTR<ServerContext>& c)
{
    RR_SHARED_PTR<LuaServiceObject> obj = RR_DYNAMIC_POINTER_CAST<LuaServiceObject>(o);
    if (!obj)
        throw ServiceException("not a Lua service object");
    obj_ = obj;
    ServiceSkel::Init(s, o, c);
    obj->add_skel(RR_DYNAMIC_POINTER_CAST<LuaServiceSkel>(shared_from_this()));
}

RR_SHARED_PTR<LuaServiceObject> LuaServiceSkel::object()
{
    RR_SHARED_PTR<LuaServiceObject> o = obj_.lock();
    if (!o)
        throw OperationAbortedException("the service object was released");
    return o;
}

std::string LuaServiceSkel::GetObjectType() { return object()->type_name; }

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::queue_request(Kind kind, const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    RR_SHARED_PTR<LuaServiceObject> obj = object();
    std::string name = m->MemberName.str().to_string();
    if (kind == Kind::function)
        member<FunctionDefinition>(obj->type, name, "function");
    else
    {
        RR_SHARED_PTR<PropertyDefinition> p = member<PropertyDefinition>(obj->type, name, "property");
        // As generated skels do: the definition's direction is enforced before Lua sees it.
        if (kind == Kind::set && p->Direction() == MemberDefinition_Direction_readonly)
            throw ReadOnlyMemberException("property '" + name + "' is read only");
        if (kind == Kind::get && p->Direction() == MemberDefinition_Direction_writeonly)
            throw WriteOnlyMemberException("property '" + name + "' is write only");
    }
    std::shared_ptr<DispatchQueue> q = obj->queue.lock();
    if (!q)
        throw OperationAbortedException("the Lua service's state has closed");
    if (q->full())
    {
        q->note_refused();
        throw ServiceException("the Lua service is busy: " + std::to_string(DispatchQueue::kMaxRemoteTasks) +
                               " requests are waiting for its state to step");
    }
    RR_SHARED_PTR<LuaServiceSkel> self = RR_DYNAMIC_POINTER_CAST<LuaServiceSkel>(shared_from_this());
    std::unique_ptr<ServerTask> t(new ServerTask(kind, self, m, ServerEndpoint::GetCurrentEndpoint()));
    if (!q->post(std::move(t)))
        return RR_INTRUSIVE_PTR<MessageEntry>(); // the dropped task answered with an error
    return RR_INTRUSIVE_PTR<MessageEntry>();     // answered later by the task (async)
}

int32_t LuaServiceSkel::add_generator(const std::function<RR_SHARED_PTR<GeneratorServerBase>(int32_t)>& make)
{
    boost::mutex::scoped_lock lock(generators_lock);
    int32_t index = get_new_generator_index();
    generators.insert(std::make_pair(index, make(index)));
    return index;
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallGetProperty(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    return queue_request(Kind::get, m);
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallSetProperty(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    return queue_request(Kind::set, m);
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallFunction(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    return queue_request(Kind::function, m);
}

RR_SHARED_PTR<RRObject> LuaServiceSkel::GetSubObj(boost::string_ref name, boost::string_ref ind)
{
    RR_SHARED_PTR<LuaServiceObject> c = object()->child(name.to_string(), ind.to_string());
    if (!c)
        throw ObjectNotFoundException("object '" + name.to_string() +
                                      (ind.empty() ? std::string() : "[" + ind.to_string() + "]") + "' not found");
    return c;
}


void LuaServiceSkel::ReleaseCastObject()
{
    for (auto& w : wire_servers_)
        w.second->Shutdown();
    for (auto& p : pipe_servers_)
        p.second->Shutdown();
}

void LuaServiceSkel::InitWireServers(const RR_SHARED_PTR<RRObject>& o)
{
    RR_SHARED_PTR<LuaServiceObject> obj = RR_DYNAMIC_POINTER_CAST<LuaServiceObject>(o);
    if (!obj || !wire_servers_.empty())
        return;
    for (const RR_SHARED_PTR<MemberDefinition>& m : obj->type->Members)
        if (RR_SHARED_PTR<WireDefinition> w = RR_DYNAMIC_POINTER_CAST<WireDefinition>(m))
        {
            RR_SHARED_PTR<WireServer<RawElement> > ws =
                RR_MAKE_SHARED<WireServer<RawElement> >(w->Name, shared_from_this(), w->Direction());
            wire_servers_[w->Name] = ws;
            obj->wire(w->Name)->attach(ws);
        }
}

void LuaServiceSkel::InitPipeServers(const RR_SHARED_PTR<RRObject>& o)
{
    RR_SHARED_PTR<LuaServiceObject> obj = RR_DYNAMIC_POINTER_CAST<LuaServiceObject>(o);
    if (!obj || !pipe_servers_.empty())
        return;
    for (const RR_SHARED_PTR<MemberDefinition>& m : obj->type->Members)
        if (RR_SHARED_PTR<PipeDefinition> p = RR_DYNAMIC_POINTER_CAST<PipeDefinition>(m))
        {
            RR_SHARED_PTR<PipeServer<RawElement> > ps =
                RR_MAKE_SHARED<PipeServer<RawElement> >(p->Name, shared_from_this(), p->IsUnreliable(), p->Direction());
            pipe_servers_[p->Name] = ps;
            obj->pipe(p->Name)->attach(ps);
        }
}

void LuaServiceSkel::DispatchWireMessage(const RR_INTRUSIVE_PTR<MessageEntry>& m, uint32_t e)
{
    auto it = wire_servers_.find(m->MemberName.str().to_string());
    if (it == wire_servers_.end())
        throw MemberNotFoundException("wire member not found");
    it->second->WirePacketReceived(m, e);
}

void LuaServiceSkel::DispatchPipeMessage(const RR_INTRUSIVE_PTR<MessageEntry>& m, uint32_t e)
{
    auto it = pipe_servers_.find(m->MemberName.str().to_string());
    if (it == pipe_servers_.end())
        throw MemberNotFoundException("pipe member not found");
    it->second->PipePacketReceived(m, e);
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallWireFunction(const RR_INTRUSIVE_PTR<MessageEntry>& m, uint32_t e)
{
    auto it = wire_servers_.find(m->MemberName.str().to_string());
    if (it == wire_servers_.end())
        throw MemberNotFoundException("wire member not found");
    return it->second->WireCommand(m, e);
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallPipeFunction(const RR_INTRUSIVE_PTR<MessageEntry>& m, uint32_t e)
{
    auto it = pipe_servers_.find(m->MemberName.str().to_string());
    if (it == pipe_servers_.end())
        throw MemberNotFoundException("pipe member not found");
    return it->second->PipeCommand(m, e);
}

RR_INTRUSIVE_PTR<MessageEntry> LuaServiceSkel::CallMemoryFunction(const RR_INTRUSIVE_PTR<MessageEntry>& m,
                                                                  const RR_SHARED_PTR<Endpoint>& e)
{
    RR_SHARED_PTR<LuaServiceObject> obj = object();
    std::string name = m->MemberName.str().to_string();
    RR_SHARED_PTR<MemoryDefinition> d = member<MemoryDefinition>(obj->type, name, "memory");
    std::shared_ptr<MemoryAccess> mem = obj->memory(name);
    if (!mem)
        throw InvalidOperationException("memory '" + name + "' has no contents; the service sets it with svc:memory");
    return mem->serve(m, e, shared_from_this(), name, d->Direction());
}

void LuaServiceSkel::answer(Kind kind, const RR_WEAK_PTR<ServiceSkel>& skel, const RR_INTRUSIVE_PTR<MessageElement>& value,
                            const RR_SHARED_PTR<RobotRaconteurException>& err, const RR_INTRUSIVE_PTR<MessageEntry>& m,
                            const RR_SHARED_PTR<ServerEndpoint>& ep)
{
    switch (kind)
    {
    case Kind::get:
        EndAsyncCallGetProperty(skel, value, err, m, ep);
        break;
    case Kind::set:
        EndAsyncCallSetProperty(skel, err, m, ep);
        break;
    case Kind::function:
        EndAsyncCallFunction(skel, value, err, m, ep);
        break;
    }
}

// ------------------------------------------------------------------------------------------
// Service (Lua handle)

namespace
{
std::shared_ptr<LuaHandlers> new_handlers(lua_State* L, const rl::Arg& handlers)
{
    if (lua_type(handlers.L, handlers.idx) != LUA_TTABLE)
        throw InvalidArgumentException("handlers must be a table of functions (getProperty, setProperty, "
                                       "callFunction)");
    std::shared_ptr<LuaHandlers> h = std::make_shared<LuaHandlers>();
    lua_pushvalue(L, handlers.idx);
    h->callbacks[kHandlersKey] = luaL_ref(L, LUA_REGISTRYINDEX);
    return h;
}

RR_SHARED_PTR<LuaServiceObject> new_object(lua_State* L, const std::shared_ptr<NodeEntry>& node,
                                           const std::string& type, const rl::Arg& handlers)
{
    RR_SHARED_PTR<ServiceEntryDefinition> def = find_object_type(node->node, type);
    StateContext* ctx = StateContext::require(L);
    std::shared_ptr<LuaHandlers> h = new_handlers(L, handlers);
    RR_SHARED_PTR<LuaServiceObject> obj = RR_MAKE_SHARED<LuaServiceObject>(def, ctx->queue(), h);
    h->owner = boost::weak_ptr<void>(obj);
    ctx->track(h);
    return obj;
}
} // namespace

Service::Service(std::shared_ptr<NodeEntry> node, std::string name, RR_SHARED_PTR<LuaServiceObject> obj, bool root)
    : node_(std::move(node)), name_(std::move(name)), obj_(std::move(obj)), root_(root)
{}

std::string Service::getName() { return name_; }

std::string Service::getType() { return obj_->type_name; }

int Service::fire_event(lua_State* L)
{
    if (lua_type(L, 2) != LUA_TSTRING)
        throw InvalidArgumentException("fireEvent: the first argument must be the event name");
    std::string name = lua_tostring(L, 2);
    if (closed_ || node_->shutdown)
        throw InvalidOperationException("the service has been closed");
    RR_SHARED_PTR<EventDefinition> ev = member<EventDefinition>(obj_->type, name, "event");
    int nargs = lua_gettop(L) - 2;
    if (nargs != static_cast<int>(ev->Parameters.size()))
        throw InvalidArgumentException("event '" + name + "' takes " + std::to_string(ev->Parameters.size()) +
                                       " arguments, got " + std::to_string(nargs));
    MarshalContext ctx = server_context(node_->node, obj_->type);
    std::vector<RR_INTRUSIVE_PTR<MessageElement> > args;
    for (int i = 0; i < nargs; ++i)
    {
        RR_INTRUSIVE_PTR<MessageElement> el = pack_element(L, 3 + i, ev->Parameters[i], ctx);
        el->ElementName = ev->Parameters[i]->Name;
        args.push_back(el);
    }
    for (const RR_SHARED_PTR<LuaServiceSkel>& s : obj_->skels())
    {
        RR_INTRUSIVE_PTR<MessageEntry> m = CreateMessageEntry(MessageEntryType_EventReq, name);
        for (const RR_INTRUSIVE_PTR<MessageElement>& a : args)
            m->AddElement(a);
        try
        {
            s->send_event(m);
        }
        catch (const std::exception&)
        {
            // A skel being released while the event is sent: skip it.
        }
    }
    return 0;
}

Service* Service::add_child(lua_State* L, const std::string& name, const std::string& index, const std::string& type,
                            rl::Arg handlers)
{
    if (closed_ || node_->shutdown)
        throw InvalidOperationException("the service has been closed");
    RR_SHARED_PTR<ObjRefDefinition> d = member<ObjRefDefinition>(obj_->type, name, "objref");
    bool indexed = d->ArrayType != DataTypes_ArrayTypes_none || d->ContainerType != DataTypes_ContainerTypes_none;
    if (indexed && index.empty())
        throw InvalidArgumentException("objref '" + name + "' is indexed; give an index");
    if (!indexed && !index.empty())
        throw InvalidArgumentException("objref '" + name + "' is not indexed");
    std::string qualified = type;
    if (qualified.find('.') == std::string::npos)
    {
        RR_SHARED_PTR<ServiceDefinition> sd = obj_->type->ServiceDefinition_.lock();
        qualified = (sd ? sd->Name : std::string()) + "." + type;
    }
    RR_SHARED_PTR<LuaServiceObject> o = new_object(L, node_, qualified, handlers);
    obj_->set_child(name, index, o);
    return new Service(node_, name_, o, false);
}

Service* Service::addObjRef(lua_State* L, const std::string& name, const std::string& type, rl::Arg handlers)
{
    return add_child(L, name, std::string(), type, handlers);
}

Service* Service::addObjRef(lua_State* L, const std::string& name, rl::Arg index, const std::string& type,
                            rl::Arg handlers)
{
    std::string ind;
    if (lua_type(index.L, index.idx) == LUA_TSTRING)
        ind = lua_tostring(index.L, index.idx);
    else if (lua_type(index.L, index.idx) == LUA_TNUMBER)
    {
        int32_t v = 0;
        if (set_element_raw(index.L, DataTypes_int32_t, &v, index.idx))
            throw InvalidArgumentException("objref index must be an int32 integer or a string");
        ind = std::to_string(v);
    }
    else
        throw InvalidArgumentException("objref index must be an int32 integer or a string");
    return add_child(L, name, ind, type, handlers);
}


ServiceWire* Service::wireBroadcaster(const std::string& name)
{
    std::shared_ptr<LuaWireMember> w = obj_->wire(name);
    w->use(LuaWireMember::Mode::broadcaster);
    RR_SHARED_PTR<WireDefinition> d = member<WireDefinition>(obj_->type, name, "wire");
    return new ServiceWire(w, d->Type, std::make_shared<MarshalContext>(server_context(node_->node, obj_->type)), true);
}

ServiceWire* Service::wireReceiver(const std::string& name)
{
    std::shared_ptr<LuaWireMember> w = obj_->wire(name);
    w->use(LuaWireMember::Mode::receiver);
    RR_SHARED_PTR<WireDefinition> d = member<WireDefinition>(obj_->type, name, "wire");
    return new ServiceWire(w, d->Type, std::make_shared<MarshalContext>(server_context(node_->node, obj_->type)), false);
}

ServicePipe* Service::pipe(const std::string& name)
{
    std::shared_ptr<LuaPipeMember> p = obj_->pipe(name);
    RR_SHARED_PTR<PipeDefinition> d = member<PipeDefinition>(obj_->type, name, "pipe");
    return new ServicePipe(p, d->Type, std::make_shared<MarshalContext>(server_context(node_->node, obj_->type)));
}

ServiceWire::ServiceWire(std::shared_ptr<LuaWireMember> w, RR_SHARED_PTR<TypeDefinition> type, MarshalContextHolder ctx,
                         bool broadcaster)
    : w_(std::move(w)), type_(std::move(type)), ctx_(std::move(ctx)), broadcaster_(broadcaster)
{}

void ServiceWire::setOutValue(rl::Arg value)
{
    if (!broadcaster_)
        throw InvalidOperationException("setOutValue: this is a wire receiver");
    RawElement el = pack_element(value.L, value.idx, type_, *ctx_);
    el->ElementName = "value";
    w_->set_out_value(el);
}

rl::Ret ServiceWire::getInValue(lua_State* L)
{
    RawElement el;
    if (broadcaster_ || !w_->try_in_value(el))
        throw ValueNotSetException("wire in value is not set (or has expired)");
    unpack_element(L, el, type_, *ctx_);
    return {1};
}

rl::Ret ServiceWire::tryGetInValue(lua_State* L)
{
    RawElement el;
    if (broadcaster_ || !w_->try_in_value(el))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    unpack_element(L, el, type_, *ctx_);
    return {2};
}

void ServiceWire::setInValueLifespan(double seconds)
{
    w_->set_in_value_lifespan(!(seconds >= 0) ? -1 : static_cast<int32_t>(seconds * 1000.0 + 0.5));
}

ServicePipe::ServicePipe(std::shared_ptr<LuaPipeMember> p, RR_SHARED_PTR<TypeDefinition> type, MarshalContextHolder ctx)
    : p_(std::move(p)), type_(std::move(type)), ctx_(std::move(ctx))
{}

int ServicePipe::sendPacket(rl::Arg value)
{
    RawElement el = pack_element(value.L, value.idx, type_, *ctx_);
    el->ElementName = "value";
    return p_->send(el);
}

rl::Ret ServicePipe::receivePacket(lua_State* L)
{
    RawElement el = p_->receive();
    if (!el)
        throw InvalidOperationException("no packet available");
    unpack_element(L, el, type_, *ctx_);
    return {1};
}

rl::Ret ServicePipe::tryReceivePacket(lua_State* L)
{
    RawElement el = p_->receive();
    if (!el)
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    unpack_element(L, el, type_, *ctx_);
    return {2};
}

int ServicePipe::available() { return p_->available(); }
int ServicePipe::endpointCount() { return p_->endpoint_count(); }

Memory* Service::memory(lua_State* L, const std::string& name, rl::Arg initial)
{
    RR_SHARED_PTR<MemoryDefinition> d = member<MemoryDefinition>(obj_->type, name, "memory");
    std::shared_ptr<MemoryAccess> m = make_local_memory(L, initial.idx, d);
    obj_->set_memory(name, m);
    return new Memory(m);
}

void Service::setAttributes(lua_State* L, rl::Arg attributes)
{
    if (!root_)
        throw InvalidOperationException("attributes belong to the registered service, not an objref");
    if (closed_ || node_->shutdown)
        throw InvalidOperationException("the service has been closed");
    std::map<std::string, RR_INTRUSIVE_PTR<RRValue> > a = pack_attributes(L, attributes.idx);
    node_->node->GetService(name_)->SetAttributes(a);
}

void Service::close()
{
    if (closed_)
        return;
    if (!root_)
        throw InvalidOperationException("only the registered service can be closed");
    closed_ = true;
    obj_->handlers->closed.store(true);
    if (!node_->shutdown)
        node_->node->CloseService(name_);
    obj_->shutdown_members();
}

// ------------------------------------------------------------------------------------------
// Node functions

std::string register_service_type(const std::shared_ptr<NodeEntry>& node, const std::string& def)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    RR_SHARED_PTR<StubFactory> f = RR_MAKE_SHARED<StubFactory>(def);
    node->node->RegisterServiceType(f);
    return f->GetServiceName();
}

int start_tcp_server(const std::shared_ptr<NodeEntry>& node, int port)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    RR_SHARED_PTR<TcpTransport> tcp = node->tcp;
    if (!tcp)
        throw InvalidOperationException("start the server transports of a node borrowed from the host in the host");
    tcp->StartServer(port);
    try
    {
        tcp->EnableNodeAnnounce(IPNodeDiscoveryFlags_NODE_LOCAL | IPNodeDiscoveryFlags_LINK_LOCAL |
                                IPNodeDiscoveryFlags_SITE_LOCAL);
    }
    catch (std::exception&)
    {
        // No usable interface for announcements: clients connect by URL.
    }
    return tcp->GetListenPort();
}

namespace
{
// {users = "<password file>", policies = {name = "value"}} -> a security policy, or null.
RR_SHARED_PTR<ServiceSecurityPolicy> security_policy(lua_State* L, const rl::Arg& options)
{
    if (options.L == nullptr || lua_isnoneornil(L, options.idx))
        return RR_SHARED_PTR<ServiceSecurityPolicy>();
    if (lua_type(L, options.idx) != LUA_TTABLE)
        throw InvalidArgumentException("service options must be a table {users=, policies=, attributes=}");
    lua_getfield(L, options.idx, "users");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return RR_SHARED_PTR<ServiceSecurityPolicy>();
    }
    if (lua_type(L, -1) != LUA_TSTRING)
    {
        lua_pop(L, 1);
        throw InvalidArgumentException("users must be password file text: \"name md5hash privileges\" lines");
    }
    std::string users = lua_tostring(L, -1);
    lua_pop(L, 1);
    std::map<std::string, std::string> policies;
    policies["requirevaliduser"] = "true";
    lua_getfield(L, options.idx, "policies");
    int pt = lua_gettop(L);
    if (lua_type(L, pt) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, pt) != 0)
        {
            if (lua_type(L, -2) != LUA_TSTRING || !(lua_type(L, -1) == LUA_TSTRING || lua_isboolean(L, -1)))
            {
                lua_pop(L, 3);
                throw InvalidArgumentException("policies must map names to strings or booleans");
            }
            std::string v = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) ? "true" : "false") : lua_tostring(L, -1);
            policies[lua_tostring(L, -2)] = v;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return RR_MAKE_SHARED<ServiceSecurityPolicy>(RR_MAKE_SHARED<PasswordFileUserAuthenticator>(users), policies);
}
} // namespace

Service* register_service(lua_State* L, const std::shared_ptr<NodeEntry>& node, const std::string& name,
                          const std::string& type, rl::Arg handlers, rl::Arg options)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    RR_SHARED_PTR<ServiceSecurityPolicy> policy = security_policy(L, options);
    // Attributes are packed before anything is registered, so a bad value leaves no service.
    std::map<std::string, RR_INTRUSIVE_PTR<RRValue> > attributes;
    bool has_attributes = false;
    if (options.L != nullptr && lua_type(L, options.idx) == LUA_TTABLE)
    {
        lua_getfield(L, options.idx, "attributes");
        if (!lua_isnil(L, -1))
        {
            try
            {
                attributes = pack_attributes(L, -1);
            }
            catch (...)
            {
                lua_pop(L, 1);
                throw;
            }
            has_attributes = true;
        }
        lua_pop(L, 1);
    }
    RR_SHARED_PTR<LuaServiceObject> obj = new_object(L, node, type, handlers);
    boost::tuple<boost::string_ref, boost::string_ref> parts = SplitQualifiedName(type);
    RR_SHARED_PTR<ServerContext> context;
    if (policy)
        context = node->node->RegisterService(name, parts.get<0>().to_string(), obj, policy);
    else
        context = node->node->RegisterService(name, parts.get<0>().to_string(), obj);
    if (has_attributes)
        context->SetAttributes(attributes);
    StateContext::require(L)->track_service(node, name);
    return new Service(node, name, obj, true);
}

} // namespace rl
