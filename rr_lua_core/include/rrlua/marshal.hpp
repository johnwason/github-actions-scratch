// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Marshalling between Lua values and RR MessageElements, driven by service definitions at
// runtime (CLAUDE.md D2, "Type mapping"). Runs on the Lua thread with live C++ objects, so it
// uses raw table access only, checks the stack with lua_checkstack, and reports every error
// as a C++ exception (RobotRaconteur::DataTypeException) for the caller's guard to raise.
#pragma once

#include "rrlua/lua_api.hpp"

#include <RobotRaconteur.h>

#include <string>
#include <vector>

namespace rl
{

// Where named types (structs, pods, namedarrays, enums) are looked up.
struct MarshalContext
{
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> node;
    RR_SHARED_PTR<RobotRaconteur::RRObject> client; // a client stub: its pulled types
    std::vector<RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> > defs;

    RR_SHARED_PTR<RobotRaconteur::NamedTypeDefinition> resolve(
        const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type) const;
    // Resolve a qualified type name received on the wire.
    RR_SHARED_PTR<RobotRaconteur::NamedTypeDefinition> resolve_name(const std::string& qualified) const;
};

// Pack the Lua value at idx as type. The element's name is type->Name.
RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement> pack_element(
    lua_State* L, int idx, const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type, const MarshalContext& ctx);

// Push the Lua value of element, declared as type. Pushes exactly one value.
void unpack_element(lua_State* L, const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& element,
                    const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type, const MarshalContext& ctx);

// rr.null: the sentinel for null entries in lists and maps.
void push_null(lua_State* L);
bool is_null(lua_State* L, int idx);

} // namespace rl

// Module functions (%native): rr.varvalue(type, value), rr.null is set by the module init.
int rl_varvalue_new(lua_State* L);
// Test support: rr._roundtrip(robdef_texts, type_string, value) packs value as the type and
// unpacks the result, resolving named types from the given service definition texts.
int rl_test_roundtrip(lua_State* L);
