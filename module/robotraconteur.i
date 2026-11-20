// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
// require("robotraconteur")
%module robotraconteur

%include "rrlua_common.i"
%include <std_string.i>

%{
#include "rrlua/module.hpp"
#include "rrlua/array.hpp"
#include "rrlua/marshal.hpp"
%}

namespace rl
{
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
%native(varvalue) int rl_varvalue_new(lua_State* L);
%native(_roundtrip) int rl_test_roundtrip(lua_State* L);

%{
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
  }
%}
