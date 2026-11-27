// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// rr.array: a typed numeric array (any RR numeric type, bool, cdouble, csingle) with
// dimensions, stored inline in a Lua userdata so the Lua GC accounts for its memory.
// Elements are column-major, like RR multidimarray. Marshalling copies between this storage
// and RRArray/RRMultiDimArray with memcpy, never through Lua tables.
//
// Layout: ArrayHeader, then ndims int64 dims, then padding to 16 bytes, then the elements.
#pragma once

#include "rrlua/lua_api.hpp"

#include <RobotRaconteur/DataTypes.h>

#include <cstddef>
#include <cstdint>

namespace rl
{

struct ArrayHeader
{
    std::int32_t type;  // RobotRaconteur::DataTypes
    std::int32_t ndims; // >= 1
    std::int64_t numel;
};

constexpr const char* kArrayMeta = "robotraconteur.array";
constexpr int kMaxArrayDims = 16;

// Element type support. Returns 0 for a type that rr.array cannot hold.
std::size_t array_elem_size(RobotRaconteur::DataTypes t);
// "double", "int32", "cdouble", ... or nullptr.
const char* array_type_name(RobotRaconteur::DataTypes t);
// Parse an element type name; returns false if unknown.
bool array_type_from_name(const char* name, RobotRaconteur::DataTypes& t);

inline std::int64_t* array_dims(ArrayHeader* a) { return reinterpret_cast<std::int64_t*>(a + 1); }
inline const std::int64_t* array_dims(const ArrayHeader* a)
{
    return reinterpret_cast<const std::int64_t*>(a + 1);
}
inline std::size_t array_data_offset(std::int32_t ndims)
{
    std::size_t off = sizeof(ArrayHeader) + sizeof(std::int64_t) * static_cast<std::size_t>(ndims);
    return (off + 15) & ~static_cast<std::size_t>(15);
}
inline void* array_data(ArrayHeader* a) { return reinterpret_cast<char*>(a) + array_data_offset(a->ndims); }
inline const void* array_data(const ArrayHeader* a)
{
    return reinterpret_cast<const char*>(a) + array_data_offset(a->ndims);
}

// The array at idx, or nullptr.
ArrayHeader* test_array(lua_State* L, int idx);

// The element count of an array with these dims, checked against what push_array accepts
// (supported type, 1..kMaxArrayDims dims, each and the product at most 2^31-1). Throws
// std::invalid_argument instead of raising a Lua error, so C++ code can check dims from the wire
// or a service definition before calling push_array.
std::int64_t array_numel_checked(RobotRaconteur::DataTypes type, const std::int64_t* dims, int ndims);

// Push a new zero-filled array. Raises a Lua error on bad arguments or allocation failure;
// call with no non-trivial C++ object live in the calling frames.
ArrayHeader* push_array(lua_State* L, RobotRaconteur::DataTypes type, const std::int64_t* dims, int ndims);

// Push element k (0-based) of a as a Lua value: number, integer, boolean, {real=,imag=}, or a
// 1-element box for 64-bit integers Lua cannot represent exactly.
void push_array_element(lua_State* L, const ArrayHeader* a, std::int64_t k);

// Convert the Lua value at idx to element k of a. Returns nullptr on success, or a static
// error message (no Lua error raised; the caller decides how to report it).
const char* set_array_element(lua_State* L, ArrayHeader* a, std::int64_t k, int idx);

// The same conversions on raw element memory of type t (for the marshaller).
void push_element_raw(lua_State* L, RobotRaconteur::DataTypes t, const void* p);
const char* set_element_raw(lua_State* L, RobotRaconteur::DataTypes t, void* p, int idx);

} // namespace rl

// Module functions (%native): rr.array, rr.multidim, rr.isarray.
int rl_array_new(lua_State* L);
int rl_multidim_new(lua_State* L);
int rl_isarray(lua_State* L);
int rl_int64_new(lua_State* L);
int rl_uint64_new(lua_State* L);
