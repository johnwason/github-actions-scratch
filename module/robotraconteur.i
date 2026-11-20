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
%}

namespace rl
{
class Client;
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
    Client* connectService(const std::string& url);
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
    Client* getObjRef(const std::string& name);
    Client* getObjRef(const std::string& name, rl::Arg index);
    std::string getType();
    bool isRoot();
    void close();
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
// obj:callFunction(name, ...): a varargs method, added to the Client class in %init.
static int rl_client_call_function(lua_State* L)
{
    rl::Client* c = nullptr;
    if (!SWIG_IsOK(SWIG_ConvertPtr(L, 1, (void**)&c, SWIGTYPE_p_rl__Client, 0)) || !c)
        return luaL_error(L, "callFunction: expected a robotraconteur client as self (use obj:callFunction)");
    return rl::guard(L, [L, c]() { return c->call_function(L); });
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
    /* Add callFunction to the Client class's method table. */
    swig_lua_class* rl_client_class = (swig_lua_class*)SWIGTYPE_p_rl__Client->clientdata;
    if (!rl_client_class)
      luaL_error(L, "robotraconteur: Client class not registered");
    SWIG_Lua_get_class_metatable(L, rl_client_class->fqname);
    if (!lua_istable(L, -1))
      luaL_error(L, "robotraconteur: Client class metatable not found");
    {
      lua_pushliteral(L, ".fn");
      lua_rawget(L, -2);
      if (lua_istable(L, -1)) {
        lua_pushliteral(L, "callFunction");
        lua_pushcfunction(L, rl_client_call_function);
        lua_rawset(L, -3);
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
%}
