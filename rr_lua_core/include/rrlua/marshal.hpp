// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Marshalling between Lua values and RR MessageElements, driven by service definitions at
// runtime (CLAUDE.md D2, "Type mapping"). Runs on the Lua thread with live C++ objects, so it
// uses raw table access only, checks the stack with lua_checkstack, and reports every error
// as a C++ exception (RobotRaconteur::DataTypeException) for the caller's guard to raise.
#pragma once

#include "rrlua/lua_api.hpp"

#include <RobotRaconteur.h>

#include <map>
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

// The default value of a field of this type, as Python's NewStructure gives it: 0 (false for
// bool, {real=0, imag=0} for complex), "" for strings, a zero-filled rr.array (empty if
// variable length), {} for lists and maps, a table of defaults for a pod or namedarray (lists
// of them for arrays, with `dims` for multidim), nil for a nested struct or varvalue, 0 for an
// enum. Pushes exactly one value.
void push_default(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type, const MarshalContext& ctx);
// A table of every field of a struct, pod or namedarray at its default.
void push_default_entry(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition>& def,
                        const MarshalContext& ctx);
// The constants of a service definition, like Python's RRN.GetConstants: service constants by
// name, a table of constants per object or struct that has any, and every enum as
// {name = value}. Struct constants are tables of the constants they refer to.
void push_constants(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::ServiceDefinition>& def);

// Service attributes (ServerContext::SetAttributes, ServiceInfo2::Attributes): a Lua table
// {name = value} where each value follows the varvalue rules (a string, an rr.array, or
// rr.varvalue("int32", 42) for a number) and must be a string or a numeric array, as RR core
// asks for concise attributes with no user defined types. Keys must be strings.
std::map<std::string, RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> > pack_attributes(lua_State* L, int idx);
// Push attributes as a table: strings as strings, numbers as rr.array (as for any received
// varvalue). Values of other RR types (lists, maps, structs) are left out.
void push_attributes(lua_State* L, const std::map<std::string, RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> >& attrs);

// rr.null: the sentinel for null entries in lists and maps.
void push_null(lua_State* L);
bool is_null(lua_State* L, int idx);

} // namespace rl

// Module functions (%native): rr.varvalue(type, value), rr.null is set by the module init.
int rl_varvalue_new(lua_State* L);
// Test support: rr._roundtrip(robdef_texts, type_string, value) packs value as the type and
// unpacks the result, resolving named types from the given service definition texts.
int rl_test_roundtrip(lua_State* L);
