// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/client.hpp"

#include "rrlua/array.hpp"

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

void DynamicStub::DispatchEvent(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    RR_UNUSED(m); // events arrive in M3
}

RR_INTRUSIVE_PTR<MessageEntry> DynamicStub::CallbackCall(const RR_INTRUSIVE_PTR<MessageEntry>& m)
{
    RR_UNUSED(m);
    throw MemberNotFoundException("callbacks are not supported yet by the Lua client");
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
            return RR_MAKE_SHARED<DynamicStub>(path, e, context);
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

Client::Client(RR_SHARED_PTR<DynamicStub> stub, std::shared_ptr<NodeEntry> node, bool root)
    : stub_(std::move(stub)), node_(std::move(node)), root_(root)
{}

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
    RR_INTRUSIVE_PTR<MessageEntry> res = s->ProcessRequest(req);
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
    s->ProcessRequest(req);
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
    RR_INTRUSIVE_PTR<MessageEntry> res = s->ProcessRequest(req);
    if (f->ReturnType->Type == DataTypes_void_t)
        return 0;
    if (!lua_checkstack(L, 4))
        throw std::runtime_error("Lua stack overflow");
    unpack_element(L, res->FindElement("return"), f->ReturnType, ctx);
    return 1;
}

Client* Client::getObjRef(const std::string& name)
{
    RR_SHARED_PTR<DynamicStub> s = stub();
    find_member<ObjRefDefinition>(s->objtype, name, "objref");
    RR_SHARED_PTR<DynamicStub> o = RR_DYNAMIC_POINTER_CAST<DynamicStub>(s->FindObjRef(name));
    if (!o)
        throw InvalidOperationException("objref '" + name + "' is not a dynamic Lua stub");
    return new Client(o, node_, false);
}

Client* Client::getObjRef(const std::string& name, rl::Arg index)
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
    RR_SHARED_PTR<DynamicStub> o = RR_DYNAMIC_POINTER_CAST<DynamicStub>(s->FindObjRef(name, ind));
    if (!o)
        throw InvalidOperationException("objref '" + name + "' is not a dynamic Lua stub");
    return new Client(o, node_, false);
}

std::string Client::getType() { return stub()->RRType(); }

bool Client::isRoot() { return root_; }

void Client::close()
{
    if (closed_)
        return;
    if (!root_)
        throw InvalidOperationException("only the service root can be closed");
    closed_ = true;
    if (!node_->shutdown)
        node_->node->DisconnectService(stub_);
}

Client* Node::connectService(const std::string& url) { return connect_service(entry_, url); }

Client* connect_service(const std::shared_ptr<NodeEntry>& node, const std::string& url)
{
    if (node->shutdown)
        throw InvalidOperationException("node has been shut down");
    RR_SHARED_PTR<RRObject> o = node->node->ConnectService(url);
    RR_SHARED_PTR<DynamicStub> s = RR_DYNAMIC_POINTER_CAST<DynamicStub>(o);
    if (!s)
    {
        node->node->DisconnectService(o);
        throw InvalidOperationException("the service's type is registered with generated code on this node; the Lua "
                                        "client needs it pulled dynamically");
    }
    return new Client(s, node, true);
}

} // namespace rl
