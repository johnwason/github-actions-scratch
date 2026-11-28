// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Client side (M2.4, M3): stubs created at runtime from pulled service definitions (CLAUDE.md
// D2), and the Lua handle to a connected object (SWIG, D1).
//
// Threading (D6): RR threads deliver events and callback requests to the stub, which posts a
// task to the queue of the lua_State that wraps it. Lua functions run only in those tasks.
// Blocking calls are async calls plus the pump (rrlua/async.hpp), so a service may call back
// into the client while a call is in progress.
#pragma once

#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"
#include "rrlua/marshal.hpp"
#include "rrlua/node.hpp"
#include "rrlua/streaming.hpp"

#include <RobotRaconteur.h>

#include <boost/thread/mutex.hpp>

#include <map>
#include <memory>
#include <string>

namespace rl
{

class Generator;
class Memory;

// A ServiceStub for any object type, driven by its ServiceEntryDefinition.
class DynamicStub : public virtual RobotRaconteur::ServiceStub
{
  public:
    DynamicStub(boost::string_ref path, const RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition>& type,
                const RR_SHARED_PTR<RobotRaconteur::ClientContext>& c);

    RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition> objtype;

    MarshalContext marshal_context();

    void DispatchEvent(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallbackCall(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    std::string RRType() RR_OVERRIDE;
    void RRInitStub() RR_OVERRIDE;
    void RRClose() RR_OVERRIDE;
    void DispatchPipeMessage(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    void DispatchWireMessage(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::PipeClientBase> RRGetPipeClient(boost::string_ref membername) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::WireClientBase> RRGetWireClient(boost::string_ref membername) RR_OVERRIDE;

    RR_SHARED_PTR<LuaWireClient> wire(const std::string& name);
    RR_SHARED_PTR<LuaPipeClient> pipe(const std::string& name);

    // Lua thread: route this stub's events and callbacks to the state that owns queue. The
    // first state to wrap a stub keeps it. Returns the stub's handlers.
    std::shared_ptr<LuaHandlers> attach(lua_State* L);
    // Lua thread: the handlers, or null if never attached.
    std::shared_ptr<LuaHandlers> handlers();

  private:
    std::shared_ptr<DispatchQueue> queue();

    boost::mutex m_;
    std::weak_ptr<DispatchQueue> queue_;
    std::shared_ptr<LuaHandlers> handlers_;
    // Filled by RRInitStub before the stub is shared, read-only afterwards.
    std::map<std::string, RR_SHARED_PTR<LuaWireClient> > wires_;
    std::map<std::string, RR_SHARED_PTR<LuaPipeClient> > pipes_;
};

// Creates DynamicStubs for one service definition.
class StubFactory : public virtual RobotRaconteur::ServiceFactory
{
  public:
    explicit StubFactory(const std::string& defstring);

    std::string GetServiceName() RR_OVERRIDE;
    std::string DefString() RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> ServiceDef() RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::StructureStub> FindStructureStub(boost::string_ref s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackStructure(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRStructure>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> UnpackStructure(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray> UnpackPodArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray> UnpackPodMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray> UnpackNamedArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray> UnpackNamedMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceStub> CreateStub(
        boost::string_ref objecttype, boost::string_ref path,
        const RR_SHARED_PTR<RobotRaconteur::ClientContext>& context) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceSkel> CreateSkel(
        boost::string_ref objecttype, boost::string_ref path, const RR_SHARED_PTR<RobotRaconteur::RRObject>& obj,
        const RR_SHARED_PTR<RobotRaconteur::ServerContext>& context) RR_OVERRIDE;
    void DownCastAndThrowException(RobotRaconteur::RobotRaconteurException& exp) RR_OVERRIDE { throw exp; }
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException> DownCastException(
        const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& exp) RR_OVERRIDE
    {
        return exp;
    }

  private:
    std::string defstring_;
    RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> def_;
};

class DynamicStubFactory : public virtual RobotRaconteur::DynamicServiceFactory
{
  public:
    RR_SHARED_PTR<RobotRaconteur::ServiceFactory> CreateServiceFactory(boost::string_ref def) RR_OVERRIDE;
    std::vector<RR_SHARED_PTR<RobotRaconteur::ServiceFactory> > CreateServiceFactories(
        const std::vector<std::string>& defs) RR_OVERRIDE;
};

// Lua handle to a connected object (a service root or an objref).
class Client
{
  public:
    // Lua thread: wraps stub and attaches it to L's state.
    Client(lua_State* L, RR_SHARED_PTR<DynamicStub> stub, std::shared_ptr<NodeEntry> node, bool root);

    rl::Ret getProperty(lua_State* L, const std::string& name);
    void setProperty(const std::string& name, rl::Arg value);
    Client* getObjRef(lua_State* L, const std::string& name);
    Client* getObjRef(lua_State* L, const std::string& name, rl::Arg index);
    std::string getType();
    bool isRoot();
    // The values of an enum from the service's definitions: {name = value, ...}.
    rl::Ret getEnum(lua_State* L, const std::string& qualified_name);
    // A struct, pod or namedarray with every field at its default (like Python's NewStructure).
    // A short name is relative to this object's service.
    rl::Ret newStructure(lua_State* L, const std::string& type_name);
    // The constants of this object's service definition, or of the named one (like Python's
    // RRN.GetConstants).
    rl::Ret getConstants(lua_State* L);
    rl::Ret getConstants(lua_State* L, const std::string& service_name);
    // The attributes of the connected service (as RRN.GetServiceAttributes): a table of
    // strings and rr.arrays.
    rl::Ret getServiceAttributes(lua_State* L);
    // Disconnect (service root only). Idempotent.
    void close(lua_State* L);

    // obj:callFunction(name, ...) (registered as a native method): stack 1 = self.
    int call_function(lua_State* L);
    // obj:callGenerator(name, ...) (native): pushes a Generator handle.
    Generator* call_generator(lua_State* L);

    // Async (natives; stack 1 = self, 2 = handler or nil). Each pushes an op (rrlua/ops.hpp);
    // the handler runs as handler(value, err) in the pump.
    int async_get_property(lua_State* L);  // obj:asyncGetProperty(handler, name)
    int async_set_property(lua_State* L);  // obj:asyncSetProperty(handler, name, value)
    int async_call_function(lua_State* L); // obj:asyncCallFunction(handler, name, ...)

    // Memories (each read or write is one RR request).
    Memory* getMemory(const std::string& name);

    // Wires and pipes (waiting with the pump).
    WireConnection* connectWire(lua_State* L, const std::string& name);
    PipeEndpoint* connectPipe(lua_State* L, const std::string& name);
    PipeEndpoint* connectPipe(lua_State* L, const std::string& name, int index);
    rl::Ret peekWireInValue(lua_State* L, const std::string& name);
    rl::Ret peekWireOutValue(lua_State* L, const std::string& name);
    void pokeWireOutValue(const std::string& name, rl::Arg value);

    // Events: fn(...) runs during rr.step (or while a blocking call waits) with the event's
    // arguments. Returns an id for removeEventListener.
    long long addEventListener(lua_State* L, const std::string& name, rl::Arg fn);
    bool removeEventListener(lua_State* L, long long id);
    // Callbacks: fn(...) runs during rr.step (or while a blocking call waits); its return
    // value, or its error, goes back to the service. nil clears the callback.
    void setCallback(lua_State* L, const std::string& name, rl::Arg fn);

  private:
    RR_SHARED_PTR<DynamicStub> stub();
    RR_SHARED_PTR<DynamicStub> stub_;
    std::shared_ptr<NodeEntry> node_;
    bool root_;
    bool closed_ = false;
};

// Push a Client as its SWIG object, taking ownership (defined by the module's SWIG code).
void push_client(lua_State* L, Client* c);

// node:asyncConnectService(handler, url) implementation: pushes an op whose value is the client.
int async_connect_service(lua_State* L, const std::shared_ptr<NodeEntry>& node);

// Connect options: {username = "...", credentials = {password = "...", ...}} (strings and
// numbers), at stack index idx (0 = none).
struct ConnectOptions
{
    std::string username;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRMap<std::string, RobotRaconteur::RRValue> > credentials;
};
ConnectOptions connect_options(lua_State* L, int idx);

// node:connectService(url[, options]) implementation.
Client* connect_service(lua_State* L, const std::shared_ptr<NodeEntry>& node, const std::string& url,
                        const ConnectOptions& opts = ConnectOptions());

} // namespace rl
