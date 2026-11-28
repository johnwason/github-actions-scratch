// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/generator.hpp"

#include "rrlua/async.hpp"
#include "rrlua/lua.hpp"
#include "rrlua/client.hpp"
#include "rrlua/server.hpp"

#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
const char kStopKey = 0; // registry key of the rr.StopIteration sentinel

bool is_stop(const RR_SHARED_PTR<RobotRaconteurException>& e)
{
    return e && (e->ErrorCode == MessageErrorType_StopIteration || e->Error == "RobotRaconteur.StopIteration");
}

void need_stack(lua_State* L)
{
    if (!lua_checkstack(L, 16))
        throw std::runtime_error("Lua stack overflow");
}
} // namespace

// ------------------------------------------------------------------------------------------
// Types and the sentinel

void generator_types(const RR_SHARED_PTR<FunctionDefinition>& f, RR_SHARED_PTR<TypeDefinition>& return_type,
                     RR_SHARED_PTR<TypeDefinition>& param_type, std::vector<RR_SHARED_PTR<TypeDefinition> >& call_params)
{
    call_params.clear();
    param_type.reset();
    return_type.reset();
    if (!f->Parameters.empty() && f->Parameters.back()->ContainerType == DataTypes_ContainerTypes_generator)
    {
        call_params.assign(f->Parameters.begin(), f->Parameters.end() - 1);
        param_type = RR_MAKE_SHARED<TypeDefinition>();
        f->Parameters.back()->CopyTo(*param_type);
        param_type->RemoveContainers();
    }
    else
        call_params = f->Parameters;
    if (f->ReturnType->Type != DataTypes_void_t)
    {
        return_type = RR_MAKE_SHARED<TypeDefinition>();
        f->ReturnType->CopyTo(*return_type);
        return_type->RemoveContainers();
    }
}

void push_stop_iteration(lua_State* L)
{
    lua_pushlightuserdata(L, const_cast<char*>(&kStopKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, const_cast<char*>(&kStopKey));
        lua_pushvalue(L, -2);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }
}

bool is_stop_iteration(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    lua_pushlightuserdata(L, const_cast<char*>(&kStopKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    bool same = !lua_isnil(L, -1) && lua_rawequal(L, -1, idx);
    lua_pop(L, 1);
    return same;
}

// ------------------------------------------------------------------------------------------
// Client

LuaGeneratorClient::LuaGeneratorClient(const std::string& name, int32_t id, const RR_SHARED_PTR<ServiceStub>& stub)
    : GeneratorClientBase(name, id, stub)
{}

void LuaGeneratorClient::next(
    const RR_INTRUSIVE_PTR<MessageElement>& v,
    boost::function<void(const RR_INTRUSIVE_PTR<MessageElement>&, const RR_SHARED_PTR<RobotRaconteurException>&)>
        handler,
    int32_t timeout)
{
    AsyncNextBase(
        v,
        [handler](const RR_INTRUSIVE_PTR<MessageElement>& r, const RR_SHARED_PTR<RobotRaconteurException>& e,
                  const RR_SHARED_PTR<RobotRaconteurNode>&) { handler(r, e); },
        timeout);
}

Generator::Generator(std::shared_ptr<LuaGeneratorClient> g, RR_SHARED_PTR<DynamicStub> stub,
                     RR_SHARED_PTR<TypeDefinition> return_type, RR_SHARED_PTR<TypeDefinition> param_type)
    : g_(std::move(g)), stub_(std::move(stub)), rtype_(std::move(return_type)), ptype_(std::move(param_type))
{}

bool Generator::do_next(lua_State* L, int value_idx, bool stop_ok)
{
    if (done_)
    {
        if (stop_ok)
            return false;
        throw StopIterationException("the generator has finished");
    }
    MarshalContext ctx = stub_->marshal_context();
    RR_INTRUSIVE_PTR<MessageElement> param;
    if (ptype_)
    {
        if (value_idx == 0)
            throw InvalidArgumentException("this generator's next takes a parameter");
        param = pack_element(L, value_idx, ptype_, ctx);
        param->ElementName = "parameter";
    }
    else if (value_idx != 0)
        throw InvalidArgumentException("this generator's next takes no parameter");
    // Completion over the element and the error, to tell StopIteration apart.
    auto c = std::make_shared<Completion<RR_INTRUSIVE_PTR<MessageElement> > >(blocking_queue(L));
    std::shared_ptr<Completion<RR_INTRUSIVE_PTR<MessageElement> > > cc = c;
    auto stopped = std::make_shared<bool>(false);
    g_->next(
        param,
        [cc, stopped](const RR_INTRUSIVE_PTR<MessageElement>& r, const RR_SHARED_PTR<RobotRaconteurException>& e) {
            if (is_stop(e))
            {
                *stopped = true;
                cc->complete(RR_INTRUSIVE_PTR<MessageElement>(), RR_SHARED_PTR<RobotRaconteurException>());
            }
            else
                cc->complete(r, e);
        },
        request_timeout(stub_->RRGetNode()));
    RR_INTRUSIVE_PTR<MessageElement> r = c->wait(L);
    if (*stopped)
    {
        done_ = true;
        if (stop_ok)
            return false;
        throw StopIterationException("the generator has finished");
    }
    need_stack(L);
    if (rtype_)
        unpack_element(L, r, rtype_, ctx);
    else
        lua_pushnil(L);
    return true;
}

rl::Ret Generator::next(lua_State* L)
{
    do_next(L, 0, false);
    return {1};
}

rl::Ret Generator::next(lua_State* L, rl::Arg value)
{
    do_next(L, value.idx, false);
    return {1};
}

rl::Ret Generator::tryNext(lua_State* L)
{
    if (!do_next(L, 0, true))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    lua_insert(L, -2);
    return {2};
}

rl::Ret Generator::tryNext(lua_State* L, rl::Arg value)
{
    if (!do_next(L, value.idx, true))
    {
        lua_pushboolean(L, 0);
        return {1};
    }
    lua_pushboolean(L, 1);
    lua_insert(L, -2);
    return {2};
}

rl::Ret Generator::nextAll(lua_State* L)
{
    if (ptype_)
        throw InvalidOperationException("nextAll needs a generator without a parameter");
    need_stack(L);
    lua_newtable(L);
    int t = lua_gettop(L);
    int n = 0;
    while (do_next(L, 0, true))
        lua_rawseti(L, t, ++n);
    return {1};
}

void Generator::close(lua_State* L)
{
    if (done_)
        return;
    done_ = true;
    auto c = std::make_shared<VoidCompletion>(blocking_queue(L));
    g_->AsyncClose(c->handler(), request_timeout(stub_->RRGetNode()));
    try
    {
        c->wait(L);
    }
    catch (const StopIterationException&)
    {
    }
}

void Generator::abort(lua_State* L)
{
    if (done_)
        return;
    done_ = true;
    auto c = std::make_shared<VoidCompletion>(blocking_queue(L));
    g_->AsyncAbort(c->handler(), request_timeout(stub_->RRGetNode()));
    try
    {
        c->wait(L);
    }
    catch (const OperationAbortedException&)
    {
    }
}

Generator* call_generator(lua_State* L, const RR_SHARED_PTR<DynamicStub>& s)
{
    if (lua_type(L, 2) != LUA_TSTRING)
        throw InvalidArgumentException("callGenerator: the first argument must be the function name");
    std::string name = lua_tostring(L, 2);
    RR_SHARED_PTR<FunctionDefinition> f;
    for (const RR_SHARED_PTR<MemberDefinition>& m : s->objtype->Members)
        if (m->Name == name)
            f = RR_DYNAMIC_POINTER_CAST<FunctionDefinition>(m);
    if (!f)
        throw MemberNotFoundException("function '" + name + "' not found in " + s->objtype->ResolveQualifiedName());
    if (!f->IsGenerator())
        throw InvalidOperationException("function '" + name + "' is not a generator; use callFunction");
    RR_SHARED_PTR<TypeDefinition> rtype, ptype;
    std::vector<RR_SHARED_PTR<TypeDefinition> > params;
    generator_types(f, rtype, ptype, params);
    int nargs = lua_gettop(L) - 2;
    if (nargs != static_cast<int>(params.size()))
        throw InvalidArgumentException("generator '" + name + "' takes " + std::to_string(params.size()) +
                                       " arguments, got " + std::to_string(nargs));
    MarshalContext ctx = s->marshal_context();
    RR_INTRUSIVE_PTR<MessageEntry> req = CreateMessageEntry(MessageEntryType_FunctionCallReq, name);
    for (int i = 0; i < nargs; ++i)
    {
        RR_INTRUSIVE_PTR<MessageElement> el = pack_element(L, 3 + i, params[i], ctx);
        el->ElementName = params[i]->Name;
        req->AddElement(el);
    }
    auto c = std::make_shared<Completion<RR_INTRUSIVE_PTR<MessageEntry> > >(blocking_queue(L));
    s->AsyncProcessRequest(req, c->handler(), request_timeout(s->RRGetNode()));
    RR_INTRUSIVE_PTR<MessageEntry> res = c->wait(L);
    int32_t index = RRArrayToScalar(res->FindElement("index")->CastData<RRArray<int32_t> >());
    std::shared_ptr<LuaGeneratorClient> g = std::make_shared<LuaGeneratorClient>(name, index, s);
    return new Generator(g, s, rtype, ptype);
}

// ------------------------------------------------------------------------------------------
// Server

namespace
{
class LuaGeneratorServer;

class GeneratorTask : public Task
{
  public:
    GeneratorTask(const RR_SHARED_PTR<LuaGeneratorServer>& g, const RR_INTRUSIVE_PTR<MessageEntry>& m,
                  const RR_SHARED_PTR<ServerEndpoint>& ep)
        : g_(g), m_(m), ep_(ep)
    {}
    ~GeneratorTask() override;
    void run(lua_State* L) override;

  private:
    RR_WEAK_PTR<LuaGeneratorServer> g_;
    RR_INTRUSIVE_PTR<MessageEntry> m_;
    RR_SHARED_PTR<ServerEndpoint> ep_;
    bool answered_ = false;
};

class LuaGeneratorServer : public GeneratorServerBase, public boost::enable_shared_from_this<LuaGeneratorServer>
{
  public:
    LuaGeneratorServer(const std::string& name, int32_t index, const RR_SHARED_PTR<ServiceSkel>& skel,
                       const RR_SHARED_PTR<ServerEndpoint>& ep, std::weak_ptr<DispatchQueue> q,
                       std::shared_ptr<LuaHandlers> h, RR_SHARED_PTR<TypeDefinition> rtype,
                       RR_SHARED_PTR<TypeDefinition> ptype, MarshalContext ctx)
        : GeneratorServerBase(name, index, skel, ep), q_(std::move(q)), h_(std::move(h)), rtype_(std::move(rtype)),
          ptype_(std::move(ptype)), ctx_(std::move(ctx))
    {}

    void CallNext(const RR_INTRUSIVE_PTR<MessageEntry>& m) RR_OVERRIDE
    {
        std::shared_ptr<DispatchQueue> q = q_.lock();
        RR_SHARED_PTR<ServerEndpoint> ep = ServerEndpoint::GetCurrentEndpoint();
        if (!q)
        {
            reply(RR_INTRUSIVE_PTR<MessageElement>(),
                  RR_MAKE_SHARED<OperationAbortedException>("the Lua service's state has closed"), m, ep);
            return;
        }
        if (q->full())
        {
            q->note_refused();
            reply(RR_INTRUSIVE_PTR<MessageElement>(),
                  RR_MAKE_SHARED<ServiceException>("the Lua service is busy: its request queue is full"), m, ep);
            return;
        }
        q->post(std::unique_ptr<Task>(new GeneratorTask(shared_from_this(), m, ep)));
    }

    // Answer a Next, Close or Abort request.
    void reply(const RR_INTRUSIVE_PTR<MessageElement>& v, const RR_SHARED_PTR<RobotRaconteurException>& err,
               const RR_INTRUSIVE_PTR<MessageEntry>& m, const RR_SHARED_PTR<ServerEndpoint>& ep)
    {
        if (err || m->Error != MessageErrorType_None)
            h_->closed.store(true); // finished: release the Lua generator on the next sweep
        if (m->Error != MessageErrorType_None)
            CallNext1<int32_t>(0, err, index, skel, m, ep);
        else
            EndAsyncCallNext(skel, v, err, index, m, ep);
    }

    // Lua thread: run next (or close/abort) and answer.
    void run(lua_State* L, const RR_INTRUSIVE_PTR<MessageEntry>& m, const RR_SHARED_PTR<ServerEndpoint>& ep)
    {
        RR_INTRUSIVE_PTR<MessageElement> out;
        RR_SHARED_PTR<RobotRaconteurException> err;
        int top = lua_gettop(L);
        try
        {
            out = invoke(L, m);
        }
        catch (const RobotRaconteurException& e)
        {
            err = RR_MAKE_SHARED<RobotRaconteurException>(e);
        }
        catch (const std::exception& e)
        {
            err = RR_MAKE_SHARED<OperationFailedException>(e.what());
        }
        lua_settop(L, top);
        reply(out, err, m, ep);
    }

  private:
    RR_INTRUSIVE_PTR<MessageElement> invoke(lua_State* L, const RR_INTRUSIVE_PTR<MessageEntry>& m)
    {
        need_stack(L);
        auto it = h_->callbacks.find("generator");
        if (h_->closed.load() || it == h_->callbacks.end())
            throw InvalidOperationException("the generator has finished");
        bool is_close = m->Error == MessageErrorType_StopIteration;
        bool is_abort = m->Error != MessageErrorType_None && !is_close;
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second);
        int gen = lua_gettop(L);
        if (lua_istable(L, gen))
        {
            lua_pushstring(L, is_close ? "close" : is_abort ? "abort" : "next");
            lua_rawget(L, gen);
        }
        else
            lua_pushvalue(L, gen); // a function: next only
        if (is_close || is_abort)
        {
            if (lua_isfunction(L, -1) && lua_istable(L, gen))
            {
                lua_pushvalue(L, gen); // self
                call_lua(L, 1, 0);
            }
            return RR_INTRUSIVE_PTR<MessageElement>();
        }
        if (!lua_isfunction(L, -1))
            throw InvalidOperationException("the Lua generator has no next function");
        int nargs = 0;
        if (lua_istable(L, gen))
        {
            lua_pushvalue(L, gen); // self
            ++nargs;
        }
        if (ptype_)
        {
            RR_INTRUSIVE_PTR<MessageElement> p;
            m->TryFindElement("parameter", p);
            unpack_element(L, p, ptype_, ctx_);
            ++nargs;
        }
        if (lua_pcall(L, nargs, 1, 0) != 0)
        {
            if (is_stop_iteration(L, -1))
                throw StopIterationException("");
            std::string msg = lua_error_text(L, -1);
            throw LuaError(msg);
        }
        if (!rtype_)
            return RR_INTRUSIVE_PTR<MessageElement>();
        RR_INTRUSIVE_PTR<MessageElement> r = pack_element(L, -1, rtype_, ctx_);
        r->ElementName = "return";
        return r;
    }

    std::weak_ptr<DispatchQueue> q_;
    std::shared_ptr<LuaHandlers> h_;
    RR_SHARED_PTR<TypeDefinition> rtype_;
    RR_SHARED_PTR<TypeDefinition> ptype_;
    MarshalContext ctx_;
};

GeneratorTask::~GeneratorTask()
{
    if (answered_)
        return;
    try
    {
        if (RR_SHARED_PTR<LuaGeneratorServer> g = g_.lock())
            g->reply(RR_INTRUSIVE_PTR<MessageElement>(),
                     RR_MAKE_SHARED<OperationAbortedException>("the Lua service's state has closed"), m_, ep_);
    }
    catch (...)
    {
    }
}

void GeneratorTask::run(lua_State* L)
{
    answered_ = true;
    if (RR_SHARED_PTR<LuaGeneratorServer> g = g_.lock())
        g->run(L, m_, ep_);
}
} // namespace

int32_t register_lua_generator(lua_State* L, const RR_SHARED_PTR<ServiceSkel>& skel, const std::string& name,
                               const RR_SHARED_PTR<FunctionDefinition>& f, const MarshalContext& ctx,
                               const RR_SHARED_PTR<ServerEndpoint>& ep)
{
    if (!lua_istable(L, -1) && !lua_isfunction(L, -1))
        throw InvalidOperationException("generator function '" + name +
                                        "' must return a function (next) or a table {next=, close=, abort=}");
    RR_SHARED_PTR<LuaServiceSkel> lskel = RR_DYNAMIC_POINTER_CAST<LuaServiceSkel>(skel);
    if (!lskel)
        throw InvalidOperationException("not a Lua service");
    RR_SHARED_PTR<TypeDefinition> rtype, ptype;
    std::vector<RR_SHARED_PTR<TypeDefinition> > params;
    generator_types(f, rtype, ptype, params);
    StateContext* sc = StateContext::require(L);
    std::shared_ptr<LuaHandlers> h = std::make_shared<LuaHandlers>();
    lua_pushvalue(L, -1);
    h->callbacks["generator"] = luaL_ref(L, LUA_REGISTRYINDEX);
    sc->track(h);
    std::weak_ptr<DispatchQueue> q = sc->queue();
    return lskel->add_generator([&](int32_t index) {
        RR_SHARED_PTR<LuaGeneratorServer> g =
            RR_MAKE_SHARED<LuaGeneratorServer>(name, index, skel, ep, q, h, rtype, ptype, ctx);
        h->owner = boost::weak_ptr<void>(g);
        return RR_SHARED_PTR<GeneratorServerBase>(g);
    });
}

} // namespace rl
