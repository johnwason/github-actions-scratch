// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Server side (M4). A Lua service object is a LuaServiceObject (an RRObject with its object
// type and a table of Lua handlers); RR serves it through LuaServiceSkel. Property and function
// requests arrive on RR threads, are queued to the owning state, and are answered after its
// pump runs the handler (the MEX model, CLAUDE.md "Server members and threading"). RR threads
// never wait for Lua here: a request that cannot be queued is answered with an error at once.
//
// Handlers: registerService(name, type, handlers) where handlers is a table with
//   getProperty(name) -> value, setProperty(name, value), callFunction(name, ...) -> value.
// A missing handler answers with an error. Lua errors are returned to the client.
#pragma once

#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"
#include "rrlua/marshal.hpp"
#include "rrlua/node.hpp"

#include <RobotRaconteur.h>

#include <boost/thread/mutex.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rl
{

class LuaServiceSkel;
class MemoryAccess;
class Memory;

typedef RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement> RawElement;
typedef std::shared_ptr<MarshalContext> MarshalContextHolder;

// One wire member of a Lua service object: a broadcaster (the latest value goes to every
// client) or a unicast receiver (the core keeps the latest value a client sent). The wire
// server exists only once a skel serves the object, so the latest out value is kept until then.
class LuaWireMember
{
  public:
    enum class Mode
    {
        none,
        broadcaster,
        receiver
    };
    void use(Mode m);
    void attach(const RR_SHARED_PTR<RobotRaconteur::WireServer<RawElement> >& server);
    void set_out_value(const RawElement& v);
    bool try_in_value(RawElement& v);
    void set_in_value_lifespan(int32_t ms);
    void shutdown();

  private:
    boost::mutex m_;
    Mode mode_ = Mode::none;
    RR_SHARED_PTR<RobotRaconteur::WireServer<RawElement> > server_;
    RR_SHARED_PTR<RobotRaconteur::WireBroadcaster<RawElement> > bcast_;
    RR_SHARED_PTR<RobotRaconteur::WireUnicastReceiver<RawElement> > recv_;
    RawElement pending_;
    int32_t lifespan_ = -1;
};

// One pipe member: the connected endpoints. sendPacket goes to all of them; received packets
// stay in each endpoint's own queue until Lua drains them.
class LuaPipeMember : public std::enable_shared_from_this<LuaPipeMember>
{
  public:
    void attach(const RR_SHARED_PTR<RobotRaconteur::PipeServer<RawElement> >& server);
    // Send to every connected endpoint; returns how many it was sent to.
    int send(const RawElement& v);
    // The next received packet from any endpoint (removed), or null.
    RawElement receive();
    int available();
    int endpoint_count();
    void shutdown();

  private:
    void connected(const RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> >& ep);
    std::vector<RR_SHARED_PTR<RobotRaconteur::PipeEndpoint<RawElement> > > live();
    boost::mutex m_;
    RR_SHARED_PTR<RobotRaconteur::PipeServer<RawElement> > server_;
    std::vector<RR_WEAK_PTR<RobotRaconteur::PipeEndpoint<RawElement> > > endpoints_;
};

class LuaServiceObject : public RobotRaconteur::RRObject
{
  public:
    LuaServiceObject(const RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition>& type,
                     std::weak_ptr<DispatchQueue> queue, std::shared_ptr<LuaHandlers> handlers);

    std::string RRType() RR_OVERRIDE { return type_name; }

    RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition> type;
    std::string type_name; // qualified
    std::weak_ptr<DispatchQueue> queue;
    std::shared_ptr<LuaHandlers> handlers; // callbacks["handlers"] = the handler table

    // Objref registry (any thread): children declared from Lua, served by GetSubObj.
    void set_child(const std::string& name, const std::string& index, const RR_SHARED_PTR<LuaServiceObject>& o);
    RR_SHARED_PTR<LuaServiceObject> child(const std::string& name, const std::string& index);

    // Wire and pipe members (created on first use, from Lua or from the skel).
    std::shared_ptr<LuaWireMember> wire(const std::string& name);
    std::shared_ptr<LuaPipeMember> pipe(const std::string& name);
    void shutdown_members();
    void set_memory(const std::string& name, const std::shared_ptr<MemoryAccess>& m);
    std::shared_ptr<MemoryAccess> memory(const std::string& name);

    // Skels serving this object (for events).
    void add_skel(const RR_SHARED_PTR<LuaServiceSkel>& s);
    std::vector<RR_SHARED_PTR<LuaServiceSkel> > skels();

  private:
    boost::mutex m_;
    std::map<std::string, RR_SHARED_PTR<LuaServiceObject> > children_;
    std::map<std::string, std::shared_ptr<LuaWireMember> > wires_;
    std::map<std::string, std::shared_ptr<LuaPipeMember> > pipes_;
    std::map<std::string, std::shared_ptr<MemoryAccess> > memories_;
    std::vector<RR_WEAK_PTR<LuaServiceSkel> > skels_;
};

class LuaServiceSkel : public RobotRaconteur::ServiceSkel
{
  public:
    void Init(boost::string_ref s, const RR_SHARED_PTR<RobotRaconteur::RRObject>& o,
              const RR_SHARED_PTR<RobotRaconteur::ServerContext>& c) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallGetProperty(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallSetProperty(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallFunction(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::RRObject> GetSubObj(boost::string_ref name, boost::string_ref ind) RR_OVERRIDE;
    void ReleaseCastObject() RR_OVERRIDE;
    void InitWireServers(const RR_SHARED_PTR<RobotRaconteur::RRObject>& o) RR_OVERRIDE;
    void InitPipeServers(const RR_SHARED_PTR<RobotRaconteur::RRObject>& o) RR_OVERRIDE;
    void DispatchWireMessage(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m, uint32_t e) RR_OVERRIDE;
    void DispatchPipeMessage(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m, uint32_t e) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallWireFunction(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m,
                                                                    uint32_t e) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallPipeFunction(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m,
                                                                    uint32_t e) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallMemoryFunction(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m, const RR_SHARED_PTR<RobotRaconteur::Endpoint>& e) RR_OVERRIDE;
    std::string GetObjectType() RR_OVERRIDE;

    RR_SHARED_PTR<LuaServiceObject> object();
    // Register a generator server made by make(index); returns the index (any thread).
    int32_t add_generator(const std::function<RR_SHARED_PTR<RobotRaconteur::GeneratorServerBase>(int32_t)>& make);
    void send_event(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) { SendEvent(m); }

    // Answer a queued request (any thread). Exactly one of value and err is set (value may be
    // null for a set or a void function).
    enum class Kind
    {
        get,
        set,
        function
    };
    static void answer(Kind kind, const RR_WEAK_PTR<RobotRaconteur::ServiceSkel>& skel,
                       const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& value,
                       const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& err,
                       const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m,
                       const RR_SHARED_PTR<RobotRaconteur::ServerEndpoint>& ep);

  private:
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> queue_request(Kind kind,
                                                                 const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m);
    RR_WEAK_PTR<LuaServiceObject> obj_;
    std::map<std::string, RR_SHARED_PTR<RobotRaconteur::WireServer<RawElement> > > wire_servers_;
    std::map<std::string, RR_SHARED_PTR<RobotRaconteur::PipeServer<RawElement> > > pipe_servers_;
};

class ServiceWire;
class ServicePipe;

// Lua handle to a registered service, or to an object in its objref registry (SWIG).
class Service
{
  public:
    Service(std::shared_ptr<NodeEntry> node, std::string name, RR_SHARED_PTR<LuaServiceObject> obj, bool root);

    std::string getName();
    std::string getType();
    // svc:fireEvent(name, ...) (native, stack 1 = self): send the event to every client.
    int fire_event(lua_State* L);
    // Declare a child object served at objref `name` (with an index for indexed objrefs:
    // an integer or a string). Returns its handle.
    Service* addObjRef(lua_State* L, const std::string& name, const std::string& type, rl::Arg handlers);
    Service* addObjRef(lua_State* L, const std::string& name, rl::Arg index, const std::string& type,
                       rl::Arg handlers);
    // Wire and pipe members of this object.
    ServiceWire* wireBroadcaster(const std::string& name);
    ServiceWire* wireReceiver(const std::string& name);
    ServicePipe* pipe(const std::string& name);
    // A memory served natively from a copy of `initial` (an rr.array; its dims for multidim).
    Memory* memory(lua_State* L, const std::string& name, rl::Arg initial);
    // Replace the service's attributes (root only), sent to clients in discovery and at
    // connect: {name = string | rr.array | rr.varvalue(type, number)}.
    void setAttributes(lua_State* L, rl::Arg attributes);
    // Close the service (root only). Idempotent.
    void close();

  private:
    Service* add_child(lua_State* L, const std::string& name, const std::string& index, const std::string& type,
                       rl::Arg handlers);
    std::shared_ptr<NodeEntry> node_;
    std::string name_;
    RR_SHARED_PTR<LuaServiceObject> obj_;
    bool root_;
    bool closed_ = false;
};

// Lua handle to a service wire member (SWIG): a broadcaster or a receiver.
class ServiceWire
{
  public:
    ServiceWire(std::shared_ptr<LuaWireMember> w, RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type,
                MarshalContextHolder ctx, bool broadcaster);
    void setOutValue(rl::Arg value);       // broadcaster
    rl::Ret getInValue(lua_State* L);      // receiver; raises if no value
    rl::Ret tryGetInValue(lua_State* L);   // receiver; ok, value
    void setInValueLifespan(double seconds);

  private:
    std::shared_ptr<LuaWireMember> w_;
    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type_;
    MarshalContextHolder ctx_;
    bool broadcaster_;
};

// Lua handle to a service pipe member (SWIG).
class ServicePipe
{
  public:
    ServicePipe(std::shared_ptr<LuaPipeMember> p, RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type,
                MarshalContextHolder ctx);
    int sendPacket(rl::Arg value);         // to every connected client; returns the count
    rl::Ret receivePacket(lua_State* L);   // raises if none
    rl::Ret tryReceivePacket(lua_State* L); // ok, value
    int available();
    int endpointCount();

  private:
    std::shared_ptr<LuaPipeMember> p_;
    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type_;
    MarshalContextHolder ctx_;
};

// node:registerServiceType(text) -> the service definition name
std::string register_service_type(const std::shared_ptr<NodeEntry>& node, const std::string& def);
// node:startTcpServer(port) -> the listening port
int start_tcp_server(const std::shared_ptr<NodeEntry>& node, int port);
// node:registerService(name, type, handlers)
Service* register_service(lua_State* L, const std::shared_ptr<NodeEntry>& node, const std::string& name,
                          const std::string& type, rl::Arg handlers, rl::Arg options);

} // namespace rl
