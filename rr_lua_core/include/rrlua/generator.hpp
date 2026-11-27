// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Generators (M5). Client: `obj:callGenerator(name, ...)` returns a Generator handle whose
// next/close/abort wait with the pump. Server: a `callFunction` handler for a generator member
// returns a Lua generator, either a function (next) or a table {next=, close=, abort=}; each
// Next request is queued and answered after the pump runs it, like any other request. A Lua
// generator ends the iteration with `error(rr.StopIteration)`.
#pragma once

#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"
#include "rrlua/marshal.hpp"

#include <RobotRaconteur.h>

#include <memory>
#include <string>

namespace rl
{

class DynamicStub;

class LuaGeneratorClient : public RobotRaconteur::GeneratorClientBase
{
  public:
    LuaGeneratorClient(const std::string& name, int32_t id, const RR_SHARED_PTR<RobotRaconteur::ServiceStub>& stub);
    void next(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v,
              boost::function<void(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>&,
                                   const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)>
                  handler,
              int32_t timeout);
};

// Lua handle to a client generator (SWIG).
class Generator
{
  public:
    Generator(std::shared_ptr<LuaGeneratorClient> g, RR_SHARED_PTR<DynamicStub> stub,
              RR_SHARED_PTR<RobotRaconteur::TypeDefinition> return_type,
              RR_SHARED_PTR<RobotRaconteur::TypeDefinition> param_type);

    rl::Ret next(lua_State* L);                 // raises RobotRaconteur.StopIteration at the end
    rl::Ret next(lua_State* L, rl::Arg value);
    rl::Ret tryNext(lua_State* L);              // true, value or false at the end
    rl::Ret tryNext(lua_State* L, rl::Arg value);
    rl::Ret nextAll(lua_State* L);              // list of values (generators without a parameter)
    void close(lua_State* L);
    void abort(lua_State* L);

  private:
    // Returns false at StopIteration (if stop_ok), else pushes one value.
    bool do_next(lua_State* L, int value_idx, bool stop_ok);
    std::shared_ptr<LuaGeneratorClient> g_;
    RR_SHARED_PTR<DynamicStub> stub_;
    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> rtype_;
    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> ptype_;
    bool done_ = false;
};

// obj:callGenerator(name, ...) implementation (native; stack 1 = self, 2 = name).
Generator* call_generator(lua_State* L, const RR_SHARED_PTR<DynamicStub>& stub);

// Return and parameter types of a generator function (parameter null if none).
void generator_types(const RR_SHARED_PTR<RobotRaconteur::FunctionDefinition>& f,
                     RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& return_type,
                     RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& param_type,
                     std::vector<RR_SHARED_PTR<RobotRaconteur::TypeDefinition> >& call_params);

// rr.StopIteration: the sentinel a Lua generator raises to end the iteration.
void push_stop_iteration(lua_State* L);
bool is_stop_iteration(lua_State* L, int idx);

// Server: the generator a Lua handler returned (at the top of the stack), registered with the
// skel serving the request; returns its index. Lua thread.
int32_t register_lua_generator(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::ServiceSkel>& skel,
                               const std::string& name, const RR_SHARED_PTR<RobotRaconteur::FunctionDefinition>& f,
                               const MarshalContext& ctx, const RR_SHARED_PTR<RobotRaconteur::ServerEndpoint>& ep);

} // namespace rl
