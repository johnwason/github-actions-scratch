// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/client.hpp"

#include "rrlua/array.hpp"
#include "rrlua/async.hpp"
#include "rrlua/ops.hpp"

#include <boost/thread/condition_variable.hpp>

#include <algorithm>
#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
template <typename T>
RR_SHARED_PTR<T> find_member(const RR_SHARED_PTR<ServiceEntryDefinition>& def, const std::string& name,
                             const char* kind)
{
    for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        if (m->Name == name)
        {
            RR_SHARED_PTR<T> t = RR_DYNAMIC_POINTER_CAST<T>(m);
            if (!t)
                throw MemberNotFoundException("member '" + name + "' of " + def->ResolveQualifiedName() + " is not a " +
                                              kind);
            return t;
        }
    throw MemberNotFoundException(std::string(kind) + " '" + name + "' not found in " + def->ResolveQualifiedName());
}


[[noreturn]] void not_used() { throw ServiceException("not used by the Lua binding (dynamic types)"); }
} // namespace

// ------------------------------------------------------------------------------------------
// DynamicStub

DynamicStub::DynamicStub(boost::string_ref path, const RR_SHARED_PTR<ServiceEntryDefinition>& type,
                         const RR_SHARED_PTR<ClientContext>& c)
    : ServiceStub(path, c), objtype(type)
{}

MarshalContext DynamicStub::marshal_context()
{
    MarshalContext ctx;
    ctx.node = RRGetNode();
    ctx.client = RR_DYNAMIC_POINTER_CAST<RRObject>(shared_from_this());
    return ctx;
}

// ------------------------------------------------------------------------------------------
// Events and callbacks: RR threads post tasks; Lua runs in the tasks on the Lua thread.

namespace
{
// Push the arguments of an event or callback request; returns how many.
int push_args(lua_State* L, const RR_INTRUSIVE_PTR<MessageEntry>& m,
              const std::vector<RR_SHARED_PTR<TypeDefinition> >& params, const MarshalContext& ctx)
{
    if (!lua_checkstack(L, static_cast<int>(params.size()) + 8))
        throw std::runtime_error("Lua stack overflow");
    for (const RR_SHARED_PTR<TypeDefinition>& p : params)
        unpack_element(L, m->FindElement(p->Name), p, ctx);
    return static_cast<int>(params.size());
}

class EventTask : public Task
{
  public:
    EventTask(const RR_SHARED_PTR<DynamicStub>& stub, const RR_INTRUSIVE_PTR<MessageEntry>& m,
              unsigned long long seq)
        : stub_(stub), m_(m), seq_(seq)
    {}

    void run(lua_State* L) override
    {
        RR_SHARED_PTR<DynamicStub> s = stub_.lock();
        if (!s)
            return;
        std::shared_ptr<LuaHandlers> h = s->handlers();
        if (!h || h->closed.load())
            return;
        std::string name = m_->MemberName.str().to_string();
        auto it = h->events.find(name);
        if (it == h->events.end() || it->second.empty())
            return;
        RR_SHARED_PTR<EventDefinition> ev = find_member<EventDefinition>(s->objtype, name, "event");
        MarshalContext ctx = s->marshal_context();
        std::vector<LuaHandlers::Listener> listeners = it->second;
        for (const LuaHandlers::Listener& l : listeners)
        {
            if (l.since >= seq_)
                continue; // added after this event arrived
            // A listener may remove others; call only those still registered.
            auto now = h->events.find(name);
            if (now == h->events.end() ||
                std::find_if(now->second.begin(), now->second.end(),
                             [&](const LuaHandlers::Listener& x) { return x.id == l.id; }) == now->second.end())
                continue;
            lua_rawgeti(L, LUA_REGISTRYINDEX, l.ref);
            int n = push_args(L, m_, ev->Parameters, ctx);
            call_lua(L, n, 0);
        }
    }

  private:
    RR_WEAK_PTR<DynamicStub> stub_;
    RR_INTRUSIVE_PTR<MessageEntry> m_;
    unsigned long long seq_;
};

// One callback request from the service, answered on the Lua thread.
struct CallbackRequest
{
    boost::mutex m;
    boost::condition_variable cv;
    bool done = false;
    RR_INTRUSIVE_PTR<MessageEntry> response;
    RR_SHARED_PTR<RobotRaconteurException> error;

    void finish(const RR_INTRUSIVE_PTR<MessageEntry>& r, const RR_SHARED_PTR<RobotRaconteurException>& e)
    {
        boost::mutex::scoped_lock lock(m);
        if (done)
            return;
        response = r;
        error = e;
        done = true;
        cv.notify_all();
    }
};

class CallbackTask : public Task
{
  public:
    CallbackTask(const RR_SHARED_PTR<DynamicStub>& stub, const RR_INTRUSIVE_PTR<MessageEntry>& m,
                 std::shared_ptr<CallbackRequest> r)
        : stub_(stub), m_(m), r_(std::move(r))
    {}

    // A task dropped unrun (the state closed) releases the waiting RR thread at once.
    ~CallbackTask() override
    {
        try
        {
            r_->finish(RR_INTRUSIVE_PTR<MessageEntry>(),
                       RR_MAKE_SHARED<OperationAbortedException>("the Lua state closed before the callback ran"));
        }
        catch (...)
        {
        }
    }

    void run(lua_State* L) override
    {
        try
        {
            r_->finish(invoke(L), RR_SHARED_PTR<RobotRaconteurException>());
        }
        catch (const RobotRaconteurException& e)
        {
            r_->finish(RR_INTRUSIVE_PTR<MessageEntry>(), RR_MAKE_SHARED<RobotRaconteurException>(e));
        }
        catch (const std::exception& e)
        {
            // A Lua error in the callback, or a value that does not marshal: the service gets it.
            r_->finish(RR_INTRUSIVE_PTR<MessageEntry>(), RR_MAKE_SHARED<OperationFailedException>(e.what()));
        }
    }

  private:
    RR_INTRUSIVE_PTR<MessageEntry> invoke(lua_State* L)
    {
        RR_SHARED_PTR<DynamicStub> s = stub_.lock();
        if (!s)
            throw OperationAbortedException("the client was released");
        std::string name = m_->MemberName.str().to_string();
        RR_SHARED_PTR<CallbackDefinition> cb = find_member<CallbackDefinition>(s->objtype, name, "callback");
        std::shared_ptr<LuaHandlers> h = s->handlers();
        auto it = h ? h->callbacks.find(name) : std::map<std::string, int>::iterator();
        if (!h || h->closed.load() || it == h->callbacks.end())
            throw InvalidOperationException("Lua callback '" + name + "' is not set");
        MarshalContext ctx = s->marshal_context();
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        int n = push_args(L, m_, cb->Parameters, ctx);
        bool is_void = cb->ReturnType->Type == DataTypes_void_t;
        call_lua(L, n, is_void ? 0 : 1);
        RR_INTRUSIVE_PTR<MessageEntry> ret = CreateMessageEntry(MessageEntryType_CallbackCallRet, name);
        ret->ServicePath = m_->ServicePath;
        ret->RequestID = m_->RequestID;
        if (is_void)
            ret->AddElement("return", ScalarToRRArray<int32_t>(0));
        else
        {
            RR_INTRUSIVE_PTR<MessageElement> el;
            try
            {
                el = pack_element(L, -1, cb->ReturnType, ctx);
            }
            catch (...)
            {
                lua_settop(L, top);
                throw;
            }
            lua_settop(L, top);
            el->ElementName = "return";
            ret->AddElement(el);
        }
        return ret;
    }

    RR_WEAK_PTR<DynamicStub> stub_;
    RR_INTRUSIVE_PTR<MessageEntry> m_;
    std::shared_ptr<CallbackRequest> r_;
};
} // namespace

std::shared_ptr<DispatchQueue> DynamicStub::queue()
{
    boost::mutex::scoped_lock lock(m_);
    return queue_.lock();
}

std::shared_ptr<LuaHandlers> DynamicStub::attach(lua_State* L)
{
    StateContext* ctx = StateContext::require(L);
    boost::mutex::scoped_lock lock(m_);
    if (!handlers_)
    {
        queue_ = ctx->queue();
        handlers_ = std::make_shared<LuaHandlers>();
        handlers_->owner = boost::weak_ptr<void>(shared_from_this());
    }
    else if (queue_.lock() != ctx->queue())
        throw InvalidOperationException("this client belongs to another lua_State");
    return handlers_;
}

std::shared_ptr<LuaHandlers> DynamicStub::handlers()
{
    boost::mutex::scoped_lock lock(m_);
    return handlers_;
}

void DynamicStub::RRInitStub()
{
    RR_SHARED_PTR<ServiceStub> self = RR_DYNAMIC_POINTER_CAST<ServiceStub>(shared_from_this());
    for (const RR_SHARED_PTR<MemberDefinition>& m : objtype->Members)
    {
        if (RR_SHARED_PTR<WireDefinition> w = RR_DYNAMIC_POINTER_CAST<WireDefinition>(m))
            wires_[w->Name] = RR_MAKE_SHARED<LuaWireClient>(w->Name, self, w->Type, w->Direction());
        else if (RR_SHARED_PTR<PipeDefinition> p = RR_DYNAMIC_POINTER_CAST<PipeDefinition>(m))
            pipes_[p->Name] =
                RR_MAKE_SHARED<LuaPipeClient>(p->Name, self, p->Type, p->IsUnreliable(), p->Direction());
    }
}

void DynamicStub::RRClose()
{
    {
        boost::mutex::scoped_lock lock(m_);
        if (handlers_)
            handlers_->closed.store(true);
    }
    for (auto& p : pipes_)
        p.second->Shutdown();
    for (auto& w : wires_)
        w.second->Shutdown();
    ServiceStub::RRClose();
}

void DynamicStub::DispatchPipeMessage(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    auto it = pipes_.find(m->MemberName.str().to_string());
    if (it == pipes_.end())
        throw MemberNotFoundException("pipe member not found");
    it->second->PipePacketReceived(m);
}

void DynamicStub::DispatchWireMessage(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    auto it = wires_.find(m->MemberName.str().to_string());
    if (it == wires_.end())
        throw MemberNotFoundException("wire member not found");
    it->second->WirePacketReceived(m);
}

RR_SHARED_PTR<PipeClientBase> DynamicStub::RRGetPipeClient(boost::string_ref membername)
{
    return pipe(membername.to_string());
}

RR_SHARED_PTR<WireClientBase> DynamicStub::RRGetWireClient(boost::string_ref membername)
{
    return wire(membername.to_string());
}

RR_SHARED_PTR<LuaWireClient> DynamicStub::wire(const std::string& name)
{
    auto it = wires_.find(name);
    if (it == wires_.end())
        throw MemberNotFoundException("wire '" + name + "' not found in " + objtype->ResolveQualifiedName());
    return it->second;
}

RR_SHARED_PTR<LuaPipeClient> DynamicStub::pipe(const std::string& name)
{
    auto it = pipes_.find(name);
    if (it == pipes_.end())
        throw MemberNotFoundException("pipe '" + name + "' not found in " + objtype->ResolveQualifiedName());
    return it->second;
}

void DynamicStub::DispatchEvent(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    std::shared_ptr<DispatchQueue> q;
    std::shared_ptr<LuaHandlers> h;
    {
        boost::mutex::scoped_lock lock(m_);
        q = queue_.lock();
        h = handlers_;
    }
    if (!q || !h)
        return; // no Lua state wraps this stub, so nobody listens
    unsigned long long seq = ++h->event_seq;
    RR_SHARED_PTR<DynamicStub> self = RR_DYNAMIC_POINTER_CAST<DynamicStub>(shared_from_this());
    q->post(std::unique_ptr<Task>(new EventTask(self, m, seq)));
}

// Runs on an RR thread, which RR core requires to return the reply. It waits for the Lua
// thread to run the callback (the MEX model), bounded by the node's request timeout; closing
// the state releases it at once (CallbackTask's destructor). See PROGRESS.md for this
// exception to "a pool thread never waits on the pump".
RR_INTRUSIVE_PTR<MessageEntry> DynamicStub::CallbackCall(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    std::shared_ptr<DispatchQueue> q = queue();
    if (!q)
        throw InvalidOperationException("no Lua state handles callbacks for this client");
    std::shared_ptr<CallbackRequest> r = std::make_shared<CallbackRequest>();
    RR_SHARED_PTR<DynamicStub> self = RR_DYNAMIC_POINTER_CAST<DynamicStub>(shared_from_this());
    if (!q->post(std::unique_ptr<Task>(new CallbackTask(self, m, r))))
        throw OperationAbortedException("the Lua state is closed");
    boost::mutex::scoped_lock lock(r->m);
    boost::posix_time::time_duration timeout = boost::posix_time::milliseconds(request_timeout(RRGetNode()));
    if (!r->cv.timed_wait(lock, timeout, [&] { return r->done; }))
    {
        r->done = true; // a late run of the task is ignored
        throw RequestTimeoutException("the Lua callback did not run in time (is rr.step being called?)");
    }
    if (r->error)
        throw RobotRaconteurException(*r->error);
    return r->response;
}

std::string DynamicStub::RRType() { return objtype->ResolveQualifiedName(); }

// ------------------------------------------------------------------------------------------
// StubFactory

StubFactory::StubFactory(const std::string& defstring) : defstring_(defstring)
{
    def_ = RR_MAKE_SHARED<ServiceDefinition>();
    def_->FromString(defstring);
}

std::string StubFactory::GetServiceName() { return def_->Name; }
std::string StubFactory::DefString() { return defstring_; }
RR_SHARED_PTR<ServiceDefinition> StubFactory::ServiceDef() { return def_; }
RR_SHARED_PTR<StructureStub> StubFactory::FindStructureStub(boost::string_ref) { not_used(); }
RR_INTRUSIVE_PTR<MessageElementNestedElementList> StubFactory::PackStructure(const RR_INTRUSIVE_PTR<RRStructure>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<RRValue> StubFactory::UnpackStructure(const RR_INTRUSIVE_PTR<MessageElementNestedElementList>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<MessageElementNestedElementList> StubFactory::PackPodArray(const RR_INTRUSIVE_PTR<RRPodBaseArray>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<RRPodBaseArray> StubFactory::UnpackPodArray(const RR_INTRUSIVE_PTR<MessageElementNestedElementList>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<MessageElementNestedElementList> StubFactory::PackPodMultiDimArray(
    const RR_INTRUSIVE_PTR<RRPodBaseMultiDimArray>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<RRPodBaseMultiDimArray> StubFactory::UnpackPodMultiDimArray(
    const RR_INTRUSIVE_PTR<MessageElementNestedElementList>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<MessageElementNestedElementList> StubFactory::PackNamedArray(const RR_INTRUSIVE_PTR<RRNamedBaseArray>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<RRNamedBaseArray> StubFactory::UnpackNamedArray(
    const RR_INTRUSIVE_PTR<MessageElementNestedElementList>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<MessageElementNestedElementList> StubFactory::PackNamedMultiDimArray(
    const RR_INTRUSIVE_PTR<RRNamedBaseMultiDimArray>&)
{
    not_used();
}
RR_INTRUSIVE_PTR<RRNamedBaseMultiDimArray> StubFactory::UnpackNamedMultiDimArray(
    const RR_INTRUSIVE_PTR<MessageElementNestedElementList>&)
{
    not_used();
}

RR_SHARED_PTR<ServiceStub> StubFactory::CreateStub(boost::string_ref type, boost::string_ref path,
                                                   const RR_SHARED_PTR<ClientContext>& context)
{
    boost::tuple<boost::string_ref, boost::string_ref> res = SplitQualifiedName(type);
    std::string servicetype = res.get<0>().to_string();
    std::string objecttype = res.get<1>().to_string();
    if (servicetype != GetServiceName())
        return context->GetNode()->GetServiceType(servicetype)->CreateStub(type, path, context);
    for (const RR_SHARED_PTR<ServiceEntryDefinition>& e : def_->Objects)
        if (e->Name == objecttype)
        {
            RR_SHARED_PTR<DynamicStub> o = RR_MAKE_SHARED<DynamicStub>(path, e, context);
            o->RRInitStub(); // as generated factories do: wire and pipe clients
            return o;
        }
    throw ServiceException("unknown object type " + type.to_string());
}

RR_SHARED_PTR<ServiceSkel> StubFactory::CreateSkel(boost::string_ref, boost::string_ref,
                                                   const RR_SHARED_PTR<RRObject>&, const RR_SHARED_PTR<ServerContext>&)
{
    throw ServiceException("services are not supported yet by the Lua binding");
}

RR_SHARED_PTR<ServiceFactory> DynamicStubFactory::CreateServiceFactory(boost::string_ref def)
{
    return RR_MAKE_SHARED<StubFactory>(def.to_string());
}

std::vector<RR_SHARED_PTR<ServiceFactory> > DynamicStubFactory::CreateServiceFactories(
    const std::vector<std::string>& defs)
{
    std::vector<RR_SHARED_PTR<ServiceFactory> > out;
    for (const std::string& d : defs)
        out.push_back(RR_MAKE_SHARED<StubFactory>(d));
    return out;
}

// ------------------------------------------------------------------------------------------
// Client

namespace
{
// A request through the pump (blocking for the script, never for the queue).
RR_INTRUSIVE_PTR<MessageEntry> request(lua_State* L, const RR_SHARED_PTR<DynamicStub>& s,
                                       const RR_INTRUSIVE_PTR<MessageEntry>& req)
{
    auto c = std::make_shared<Completion<RR_INTRUSIVE_PTR<MessageEntry> > >(blocking_queue(L));
    s->AsyncProcessRequest(req, c->handler(), request_timeout(s->RRGetNode()));
    return c->wait(L);
}

RR_SHARED_PTR<DynamicStub> as_dynamic(const RR_SHARED_PTR<RRObject>& o, const std::string& what)
{
    RR_SHARED_PTR<DynamicStub> d = RR_DYNAMIC_POINTER_CAST<DynamicStub>(o);
    if (!d)
        throw InvalidOperationException(what + " is not a dynamic Lua stub");
    return d;
}

int ref_function(lua_State* L, const rl::Arg& fn, const char* what)
{
    if (lua_type(fn.L, fn.idx) != LUA_TFUNCTION)
        throw InvalidArgumentException(std::string(what) + ": expected a function");
    lua_pushvalue(L, fn.idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}
} // namespace

Client::Client(lua_State* L, RR_SHARED_PTR<DynamicStub> stub, std::shared_ptr<NodeEntry> node, bool root)
    : stub_(std::move(stub)), node_(std::move(node)), root_(root)
{
    StateContext::require(L)->track(stub_->attach(L));
}

RR_SHARED_PTR<DynamicStub> Client::stub()
{
    if (closed_)
        throw InvalidOperationException("client has been closed");
    if (node_->shutdown)
        throw InvalidOperationException("node has been shut down");
    return stub_;
}

rl::Ret Client::getProperty(lua_State* L, const std::string& name)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<PropertyDefinition> p = find_member<PropertyDefinition>(s->objtype, name, "property");
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_PropertyGetReq, name);
    RR_INTRUSIVE_PTR<MessageEntry> res = request(L, s, req);
    MarshalContext ctx = s->marshal_context();
    if (!lua_checkstack(L, 4))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, res->FindElement("value"), p->Type, ctx);
    return {1};
}

void Client::setProperty(const std::string& name, rl::Arg value)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<PropertyDefinition> p = find_member<PropertyDefinition>(s->objtype, name, "property");
    MarshalContext ctx = s->marshal_context();
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_PropertySetReq, name);
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(value.L, value.idx, p->Type, ctx);
    el->ElementName = "value";
    req->AddElement(el);
    request(value.L, s, req);
}

int Client::call_function(lua_State* L)
{
    if (lua_type(L, 2) != LUA_TSTRING)
        throw InvalidArgumentException("callFunction: the first argument must be the function name");
    std::string name = lua_tostring(L, 2);
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<FunctionDefinition> f = find_member<FunctionDefinition>(s->objtype, name, "function");
    if (f->IsGenerator())
        throw InvalidOperationException("function '" + name + "' is a generator; use callGenerator");
    int nargs = lua_gettop(L) - 2;
    if (nargs != static_cast<int>(f->Parameters.size()))
        throw InvalidArgumentException("function '" + name + "' takes " + std::to_string(f->Parameters.size()) +
                                       " arguments, got " + std::to_string(nargs));
    MarshalContext ctx = s->marshal_context();
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_FunctionCallReq, name);
    for (int i = 0; i < nargs; ++i)
    {
        RR_INTRUSIVE_PTR<MessageElement> el = pack_element(L, 3 + i, f->Parameters[i], ctx);
        el->ElementName = f->Parameters[i]->Name;
        req->AddElement(el);
    }
    int top = lua_gettop(L);
    RR_INTRUSIVE_PTR<MessageEntry> res = request(L, s, req);
    lua_settop(L, top);
    if (f->ReturnType->Type == DataTypes_void_t)
        return 0;
    if (!lua_checkstack(L, 4))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, res->FindElement("return"), f->ReturnType, ctx);
    return 1;
}

Client* Client::getObjRef(lua_State* L, const std::string& name)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    find_member<ObjRefDefinition>(s->objtype, name, "objref");
    auto c = std::make_shared<Completion<RR_SHARED_PTR<RRObject> > >(blocking_queue(L));
    s->AsyncFindObjRef(name, c->handler(), request_timeout(s->RRGetNode()));
    return new Client(L, as_dynamic(c->wait(L), "objref '" + name + "'"), node_, false);
}

Client* Client::getObjRef(lua_State* L, const std::string& name, rl::Arg index)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    find_member<ObjRefDefinition>(s->objtype, name, "objref");
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
    auto c = std::make_shared<Completion<RR_SHARED_PTR<RRObject> > >(blocking_queue(L));
    s->AsyncFindObjRef(name, ind, c->handler(), request_timeout(s->RRGetNode()));
    return new Client(L, as_dynamic(c->wait(L), "objref '" + name + "'"), node_, false);
}

std::string Client::getType() { return stub()->RRType(); }

bool Client::isRoot() { return root_; }

void Client::close(lua_State* L)
{
    if (closed_)
        return;
    if (!root_)
        throw InvalidOperationException("only the service root can be closed");
    closed_ = true;
    if (!node_->shutdown)
    {
        auto c = std::make_shared<Completion<bool> >(blocking_queue(L));
        std::shared_ptr<Completion<bool> > cc = c;
        node_->node->AsyncDisconnectService(stub_, [cc]() { cc->complete(true, RR_SHARED_PTR<RobotRaconteurException>()); });
        c->wait(L);
    }
    StateContext::require(L)->sweep(L);
}

long long Client::addEventListener(lua_State* L, const std::string& name, rl::Arg fn)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    find_member<EventDefinition>(s->objtype, name, "event");
    std::shared_ptr<LuaHandlers> h = s->attach(L);
    long long id = h->next_id++;
    int ref = ref_function(L, fn, "addEventListener");
    LuaHandlers::Listener l;
    l.id = id;
    l.ref = ref;
    l.since = h->event_seq.load();
    h->events[name].push_back(l);
    return id;
}

bool Client::removeEventListener(lua_State* L, long long id)
{
    std::shared_ptr<LuaHandlers> h = stub_->handlers();
    if (!h)
        return false;
    for (auto& e : h->events)
        for (auto it = e.second.begin(); it != e.second.end(); ++it)
            if (it->id == id)
            {
                luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
                e.second.erase(it);
                return true;
            }
    return false;
}

void Client::setCallback(lua_State* L, const std::string& name, rl::Arg fn)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    find_member<CallbackDefinition>(s->objtype, name, "callback");
    std::shared_ptr<LuaHandlers> h = s->attach(L);
    auto it = h->callbacks.find(name);
    if (it != h->callbacks.end())
    {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        h->callbacks.erase(it);
    }
    if (lua_isnil(fn.L, fn.idx))
        return;
    h->callbacks[name] = ref_function(L, fn, "setCallback");
}


// ------------------------------------------------------------------------------------------
// Async calls

namespace
{
void check_handler(lua_State* L, int idx)
{
    if (!lua_isnoneornil(L, idx) && lua_type(L, idx) != LUA_TFUNCTION)
        throw InvalidArgumentException("the first argument must be a handler function or nil");
}

std::string name_arg(lua_State* L, int idx, const char* what)
{
    if (lua_type(L, idx) != LUA_TSTRING)
        throw InvalidArgumentException(std::string(what) + ": the member name must be a string");
    return lua_tostring(L, idx);
}

// Start req; the op's value is conv(response) on the Lua thread. Pushes the op.
int start_request(lua_State* L, const RR_SHARED_PTR<DynamicStub>& s, const RR_INTRUSIVE_PTR<MessageEntry>& req,
                  std::function<void(lua_State*, const RR_INTRUSIVE_PTR<MessageEntry>&)> conv)
{
    std::shared_ptr<DispatchQueue> q = state_queue(L);
    std::weak_ptr<DispatchQueue> wq = q;
    int ref = push_new_op(L, 2);
    try
    {
        s->AsyncProcessRequest(
            req,
            [wq, ref, conv](const RR_INTRUSIVE_PTR<MessageEntry>& ret,
                            const RR_SHARED_PTR<RobotRaconteurException>& err) {
                post_op_result(wq.lock(), ref, [conv, ret](lua_State* L2) { conv(L2, ret); }, err);
            },
            request_timeout(s->RRGetNode()));
    }
    catch (...)
    {
        release_op(L, ref);
        throw;
    }
    return 1;
}
} // namespace

int Client::async_get_property(lua_State* L)
{
    check_handler(L, 2);
    std::string name = name_arg(L, 3, "asyncGetProperty");
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<PropertyDefinition> p = find_member<PropertyDefinition>(s->objtype, name, "property");
    MarshalContext ctx = s->marshal_context();
    RR_SHARED_PTR<TypeDefinition> type = p->Type;
    lua_settop(L, 3);
    return start_request(L, s, CreateMessageEntry(MessageEntryType_PropertyGetReq, name),
                         [ctx, type](lua_State* L2, const RR_INTRUSIVE_PTR<MessageEntry>& ret) {
                             unpack_element(L2, ret->FindElement("value"), type, ctx);
                         });
}

int Client::async_set_property(lua_State* L)
{
    check_handler(L, 2);
    std::string name = name_arg(L, 3, "asyncSetProperty");
    if (lua_gettop(L) != 4)
        throw InvalidArgumentException("asyncSetProperty(handler, name, value) takes 3 arguments");
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<PropertyDefinition> p = find_member<PropertyDefinition>(s->objtype, name, "property");
    MarshalContext ctx = s->marshal_context();
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_PropertySetReq, name);
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(L, 4, p->Type, ctx);
    el->ElementName = "value";
    req->AddElement(el);
    lua_settop(L, 3);
    return start_request(L, s, req,
                         [](lua_State* L2, const RR_INTRUSIVE_PTR<MessageEntry>&) { lua_pushnil(L2); });
}

int Client::async_call_function(lua_State* L)
{
    check_handler(L, 2);
    std::string name = name_arg(L, 3, "asyncCallFunction");
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<FunctionDefinition> f = find_member<FunctionDefinition>(s->objtype, name, "function");
    if (f->IsGenerator())
        throw InvalidOperationException("function '" + name + "' is a generator");
    int nargs = lua_gettop(L) - 3;
    if (nargs != static_cast<int>(f->Parameters.size()))
        throw InvalidArgumentException("function '" + name + "' takes " + std::to_string(f->Parameters.size()) +
                                       " arguments, got " + std::to_string(nargs));
    MarshalContext ctx = s->marshal_context();
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_FunctionCallReq, name);
    for (int i = 0; i < nargs; ++i)
    {
        RR_INTRUSIVE_PTR<MessageElement> el = pack_element(L, 4 + i, f->Parameters[i], ctx);
        el->ElementName = f->Parameters[i]->Name;
        req->AddElement(el);
    }
    lua_settop(L, 3);
    RR_SHARED_PTR<TypeDefinition> rt = f->ReturnType;
    return start_request(L, s, req, [ctx, rt](lua_State* L2, const RR_INTRUSIVE_PTR<MessageEntry>& ret) {
        if (rt->Type == DataTypes_void_t)
            lua_pushnil(L2);
        else
            unpack_element(L2, ret->FindElement("return"), rt, ctx);
    });
}

int async_connect_service(lua_State* L, const std::shared_ptr<NodeEntry>& node)
{
    check_handler(L, 2);
    if (lua_type(L, 3) != LUA_TSTRING)
        throw InvalidArgumentException("asyncConnectService(handler, url): url must be a string");
    std::string url = lua_tostring(L, 3);
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    lua_settop(L, 3);
    std::weak_ptr<DispatchQueue> wq = state_queue(L);
    int ref = push_new_op(L, 2);
    try
    {
        node->node->AsyncConnectService(
            url, "", RR_INTRUSIVE_PTR<RRMap<std::string, RRValue> >(), NULL, "",
            [wq, ref, node](const RR_SHARED_PTR<RRObject>& o, const RR_SHARED_PTR<RobotRaconteurException>& err) {
                post_op_result(wq.lock(), ref,
                               [o, node](lua_State* L2) {
                                   RR_SHARED_PTR<DynamicStub> s = as_dynamic(o, "the service root");
                                   push_client(L2, new Client(L2, s, node, true));
                               },
                               err);
            },
            request_timeout(node->node));
    }
    catch (...)
    {
        release_op(L, ref);
        throw;
    }
    return 1;
}

// ------------------------------------------------------------------------------------------
// Wires and pipes

WireConnection* Client::connectWire(lua_State* L, const std::string& name)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<LuaWireClient> w = s->wire(name);
    auto c = std::make_shared<Completion<RR_SHARED_PTR<WireConnectionBase> > >(blocking_queue(L));
    w->connect(c->handler(), request_timeout(s->RRGetNode()));
    RR_SHARED_PTR<LuaWireConnection> conn = RR_DYNAMIC_POINTER_CAST<LuaWireConnection>(c->wait(L));
    if (!conn)
        throw InvalidOperationException("unexpected wire connection type");
    return new WireConnection(L, conn, s);
}

PipeEndpoint* Client::connectPipe(lua_State* L, const std::string& name) { return connectPipe(L, name, -1); }

PipeEndpoint* Client::connectPipe(lua_State* L, const std::string& name, int index)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<LuaPipeClient> p = s->pipe(name);
    auto c = std::make_shared<Completion<RR_SHARED_PTR<PipeEndpointBase> > >(blocking_queue(L));
    p->connect(index, c->handler(), request_timeout(s->RRGetNode()));
    RR_SHARED_PTR<LuaPipeEndpoint> e = RR_DYNAMIC_POINTER_CAST<LuaPipeEndpoint>(c->wait(L));
    if (!e)
        throw InvalidOperationException("unexpected pipe endpoint type");
    return new PipeEndpoint(L, e, s);
}

namespace
{
rl::Ret peek_wire(lua_State* L, const RR_SHARED_PTR<DynamicStub>& s, const std::string& name, bool in)
{
    RR_SHARED_PTR<LuaWireClient> w = s->wire(name);
    auto c = std::make_shared<Completion<RR_INTRUSIVE_PTR<RRValue> > >(blocking_queue(L));
    std::shared_ptr<Completion<RR_INTRUSIVE_PTR<RRValue> > > cc = c;
    w->peek(
        in,
        [cc](const RR_INTRUSIVE_PTR<RRValue>& v, const TimeSpec&, const RR_SHARED_PTR<RobotRaconteurException>& e) {
            cc->complete(v, e);
        },
        request_timeout(s->RRGetNode()));
    RR_INTRUSIVE_PTR<MessageElement> el = rr_cast<MessageElement>(c->wait(L));
    if (!lua_checkstack(L, 8))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, el, w->type, s->marshal_context());
    return {1};
}
} // namespace

rl::Ret Client::peekWireInValue(lua_State* L, const std::string& name) { return peek_wire(L, stub(), name, true); }

rl::Ret Client::peekWireOutValue(lua_State* L, const std::string& name) { return peek_wire(L, stub(), name, false); }

void Client::pokeWireOutValue(const std::string& name, rl::Arg value)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    RR_SHARED_PTR<LuaWireClient> w = s->wire(name);
    RR_INTRUSIVE_PTR<MessageElement> el = pack_element(value.L, value.idx, w->type, s->marshal_context());
    el->ElementName = "value";
    auto c = std::make_shared<VoidCompletion>(blocking_queue(value.L));
    w->poke(el, c->handler(), request_timeout(s->RRGetNode()));
    c->wait(value.L);
}

Client* Node::connectService(lua_State* L, const std::string& url) { return connect_service(L, entry_, url); }

Client* connect_service(lua_State* L, const std::shared_ptr<NodeEntry>& node, const std::string& url)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    auto c = std::make_shared<Completion<RR_SHARED_PTR<RRObject> > >(blocking_queue(L));
    node->node->AsyncConnectService(url, "", RR_INTRUSIVE_PTR<RRMap<std::string, RRValue> >(), NULL, "",
                                    c->handler(), request_timeout(node->node));
    RR_SHARED_PTR<RRObject> o = c->wait(L);
    RR_SHARED_PTR<DynamicStub> s = RR_DYNAMIC_POINTER_CAST<DynamicStub>(o);
    if (!s)
    {
        node->node->DisconnectService(o);
        throw InvalidOperationException("the service's type is registered with generated code on this node; the Lua "
                                        "client needs it pulled dynamically");
    }
    return new Client(L, s, node, true);
}

} // namespace rl
