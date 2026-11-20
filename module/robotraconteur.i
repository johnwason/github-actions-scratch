// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
// require("robotraconteur")
%module robotraconteur

%include "rrlua_common.i"
%include <std_string.i>

%{
#include "rrlua/module.hpp"
#include "rrlua/array.hpp"
#include "rrlua/marshal.hpp"
#include "rrlua/client.hpp"
#include "rrlua/ops.hpp"
#include "rrlua/streaming.hpp"
%}

namespace rl
{
class Client;
class WireConnection;
class PipeEndpoint;
%nodefaultctor Node;
%nodefaultdtor Node;
class Node
{
  public:
    ~Node();
    std::string getNodeName();
    std::string getNodeID();
    bool isOwned();
    bool isShutdown();
    void shutdown();
    %newobject connectService;
    Client* connectService(lua_State* L, const std::string& url);
};

%nodefaultctor Client;
%nodefaultdtor Client;
class Client
{
  public:
    ~Client();
    rl::Ret getProperty(lua_State* L, const std::string& name);
    void setProperty(const std::string& name, rl::Arg value);
    %newobject getObjRef;
    Client* getObjRef(lua_State* L, const std::string& name);
    Client* getObjRef(lua_State* L, const std::string& name, rl::Arg index);
    std::string getType();
    bool isRoot();
    void close(lua_State* L);
    long long addEventListener(lua_State* L, const std::string& name, rl::Arg fn);
    bool removeEventListener(lua_State* L, long long id);
    void setCallback(lua_State* L, const std::string& name, rl::Arg fn);
    %newobject connectWire;
    WireConnection* connectWire(lua_State* L, const std::string& name);
    %newobject connectPipe;
    PipeEndpoint* connectPipe(lua_State* L, const std::string& name);
    PipeEndpoint* connectPipe(lua_State* L, const std::string& name, int index);
    rl::Ret peekWireInValue(lua_State* L, const std::string& name);
    rl::Ret peekWireOutValue(lua_State* L, const std::string& name);
    void pokeWireOutValue(const std::string& name, rl::Arg value);
};

%nodefaultctor WireConnection;
%nodefaultdtor WireConnection;
class WireConnection
{
  public:
    ~WireConnection();
    rl::Ret getInValue(lua_State* L);
    rl::Ret tryGetInValue(lua_State* L);
    rl::Ret getOutValue(lua_State* L);
    rl::Ret tryGetOutValue(lua_State* L);
    void setOutValue(rl::Arg value);
    bool inValueValid();
    bool outValueValid();
    double getInValueLifespan();
    void setInValueLifespan(double seconds);
    double getOutValueLifespan();
    void setOutValueLifespan(double seconds);
    std::string direction();
    long long addValueChangedListener(lua_State* L, rl::Arg fn);
    bool removeValueChangedListener(lua_State* L, long long id);
    void close(lua_State* L);
};

%nodefaultctor PipeEndpoint;
%nodefaultdtor PipeEndpoint;
class PipeEndpoint
{
  public:
    ~PipeEndpoint();
    double sendPacket(rl::Arg value);
    rl::Ret receivePacket(lua_State* L);
    rl::Ret tryReceivePacket(lua_State* L);
    rl::Ret peekNextPacket(lua_State* L);
    int available();
    int index();
    bool isUnreliable();
    std::string direction();
    bool getRequestPacketAck();
    void setRequestPacketAck(bool ack);
    long long addPacketReceivedListener(lua_State* L, rl::Arg fn);
    bool removePacketReceivedListener(lua_State* L, long long id);
    void close(lua_State* L);
};

%newobject createNode;
%newobject defaultNode;
Node* createNode(lua_State* L);
Node* createNode(lua_State* L, rl::Arg opts);
Node* defaultNode(lua_State* L);

void _testPostFromThread(lua_State* L, int n);
double _testCounter(lua_State* L);
int _queueSize(lua_State* L);
}

%native(step) int rl_step(lua_State* L);
%native(sleep) int rl_sleep(lua_State* L);
%native(array) int rl_array_new(lua_State* L);
%native(multidim) int rl_multidim_new(lua_State* L);
%native(isarray) int rl_isarray(lua_State* L);
%native(int64) int rl_int64_new(lua_State* L);
%native(uint64) int rl_uint64_new(lua_State* L);
%native(varvalue) int rl_varvalue_new(lua_State* L);
%native(_roundtrip) int rl_test_roundtrip(lua_State* L);
%native(_testVector) int rl_test_vector(lua_State* L);
%{
int rl_test_vector(lua_State* L);
%}

%{
// Varargs methods (natives), added to the class method tables in %init.
static rl::Client* rl_self_client(lua_State* L, const char* what)
{
    rl::Client* c = nullptr;
    if (!SWIG_IsOK(SWIG_ConvertPtr(L, 1, (void**)&c, SWIGTYPE_p_rl__Client, 0)) || !c)
        luaL_error(L, "%s: expected a robotraconteur client as self (use obj:%s)", what, what);
    return c;
}

static int rl_client_call_function(lua_State* L)
{
    rl::Client* c = rl_self_client(L, "callFunction");
    return rl::guard(L, [L, c]() { return c->call_function(L); });
}
static int rl_client_async_get_property(lua_State* L)
{
    rl::Client* c = rl_self_client(L, "asyncGetProperty");
    return rl::guard(L, [L, c]() { return c->async_get_property(L); });
}
static int rl_client_async_set_property(lua_State* L)
{
    rl::Client* c = rl_self_client(L, "asyncSetProperty");
    return rl::guard(L, [L, c]() { return c->async_set_property(L); });
}
static int rl_client_async_call_function(lua_State* L)
{
    rl::Client* c = rl_self_client(L, "asyncCallFunction");
    return rl::guard(L, [L, c]() { return c->async_call_function(L); });
}
static int rl_node_async_connect_service(lua_State* L)
{
    rl::Node* n = nullptr;
    if (!SWIG_IsOK(SWIG_ConvertPtr(L, 1, (void**)&n, SWIGTYPE_p_rl__Node, 0)) || !n)
        return luaL_error(L, "asyncConnectService: expected a robotraconteur node as self");
    return rl::guard(L, [L, n]() { return rl::async_connect_service(L, n->entry()); });
}

// Add natives to a SWIG class's method table.
static void rl_add_methods(lua_State* L, swig_type_info* type, const luaL_Reg* methods)
{
    swig_lua_class* cls = (swig_lua_class*)type->clientdata;
    if (!cls)
        luaL_error(L, "robotraconteur: class not registered");
    SWIG_Lua_get_class_metatable(L, cls->fqname);
    if (!lua_istable(L, -1))
        luaL_error(L, "robotraconteur: class metatable not found");
    lua_pushliteral(L, ".fn");
    lua_rawget(L, -2);
    if (lua_istable(L, -1))
        for (; methods->name; ++methods)
        {
            lua_pushstring(L, methods->name);
            lua_pushcfunction(L, methods->func);
            lua_rawset(L, -3);
        }
    lua_pop(L, 2);
}

namespace rl
{
void push_borrowed_node(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node)
{
    StateContext* ctx = StateContext::get(L);
    if (!ctx)
        luaL_error(L, "robotraconteur: module is not open in this lua_State");
    Node* h = new Node(ctx->attach_borrowed(node));
    SWIG_NewPointerObj(L, h, SWIGTYPE_p_rl__Node, 1);
}

void push_client(lua_State* L, Client* c) { SWIG_NewPointerObj(L, c, SWIGTYPE_p_rl__Client, 1); }
} // namespace rl
%}

%init %{
  {
    bool rl_ok = true;
    try {
      rl::StateContext::ensure(L);
    } catch (...) {
      rl_ok = false;
    }
    if (!rl_ok)
      luaL_error(L, "robotraconteur: could not create the per-state context");
    /* rr.null; the module table is on top of the stack here. Raw set: SWIG's module
       metatable intercepts ordinary assignment. */
    lua_pushliteral(L, "null");
    rl::push_null(L);
    lua_rawset(L, -3);
    {
      static const luaL_Reg rl_client_methods[] = {
        {"callFunction", rl_client_call_function},
        {"asyncGetProperty", rl_client_async_get_property},
        {"asyncSetProperty", rl_client_async_set_property},
        {"asyncCallFunction", rl_client_async_call_function},
        {nullptr, nullptr}};
      static const luaL_Reg rl_node_methods[] = {
        {"asyncConnectService", rl_node_async_connect_service},
        {nullptr, nullptr}};
      rl_add_methods(L, SWIGTYPE_p_rl__Client, rl_client_methods);
      rl_add_methods(L, SWIGTYPE_p_rl__Node, rl_node_methods);
    }
    /* rr.await and the op metatable; the module table is on top of the stack. */
    rl::open_ops(L, -1);
  }
%}
