// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// rr.array (see rrlua/array.hpp). These are plain lua_CFunctions that keep no non-trivial
// C++ object alive, so they raise Lua errors directly with luaL_error.
//
// Conversions are exact (as in the TypeScript client): a value that does not fit the element
// type (fractional for an integer type, out of range, NaN for an integer type) is rejected,
// never rounded, wrapped or truncated.

#include "rrlua/array.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using RobotRaconteur::DataTypes;

namespace rl
{
namespace
{

struct TypeInfo
{
    DataTypes type;
    const char* name;
    std::size_t size;
};

const TypeInfo kTypes[] = {
    {RobotRaconteur::DataTypes_double_t, "double", 8},   {RobotRaconteur::DataTypes_single_t, "single", 4},
    {RobotRaconteur::DataTypes_int8_t, "int8", 1},       {RobotRaconteur::DataTypes_uint8_t, "uint8", 1},
    {RobotRaconteur::DataTypes_int16_t, "int16", 2},     {RobotRaconteur::DataTypes_uint16_t, "uint16", 2},
    {RobotRaconteur::DataTypes_int32_t, "int32", 4},     {RobotRaconteur::DataTypes_uint32_t, "uint32", 4},
    {RobotRaconteur::DataTypes_int64_t, "int64", 8},     {RobotRaconteur::DataTypes_uint64_t, "uint64", 8},
    {RobotRaconteur::DataTypes_bool_t, "bool", 1},       {RobotRaconteur::DataTypes_cdouble_t, "cdouble", 16},
    {RobotRaconteur::DataTypes_csingle_t, "csingle", 8},
};

const TypeInfo* info(DataTypes t)
{
    for (const TypeInfo& i : kTypes)
        if (i.type == t)
            return &i;
    return nullptr;
}

#if LUA_VERSION_NUM >= 502
size_t raw_len(lua_State* L, int idx) { return lua_rawlen(L, idx); }
#else
size_t raw_len(lua_State* L, int idx) { return lua_objlen(L, idx); }
#endif

int abs_index(lua_State* L, int idx) { return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx : lua_gettop(L) + idx + 1; }

bool is_integer_type(DataTypes t)
{
    switch (t)
    {
    case RobotRaconteur::DataTypes_int8_t:
    case RobotRaconteur::DataTypes_uint8_t:
    case RobotRaconteur::DataTypes_int16_t:
    case RobotRaconteur::DataTypes_uint16_t:
    case RobotRaconteur::DataTypes_int32_t:
    case RobotRaconteur::DataTypes_uint32_t:
    case RobotRaconteur::DataTypes_int64_t:
    case RobotRaconteur::DataTypes_uint64_t:
        return true;
    default:
        return false;
    }
}

bool is_complex_type(DataTypes t)
{
    return t == RobotRaconteur::DataTypes_cdouble_t || t == RobotRaconteur::DataTypes_csingle_t;
}

// An integer value read from Lua: either signed or unsigned 64-bit, exactly.
struct IntValue
{
    bool negative; // value < 0, stored in s
    std::int64_t s;
    std::uint64_t u; // value >= 0
};

// Read element 0 of a 1-element integer array (a box).
bool read_box(const ArrayHeader* a, IntValue& v)
{
    if (a->numel != 1 || !is_integer_type(static_cast<DataTypes>(a->type)))
        return false;
    const void* d = array_data(a);
    switch (static_cast<DataTypes>(a->type))
    {
#define RL_BOX(T, C)                                                                                                   \
    case RobotRaconteur::T: {                                                                                          \
        C x;                                                                                                           \
        std::memcpy(&x, d, sizeof x);                                                                                  \
        if (x < 0)                                                                                                     \
        {                                                                                                              \
            v.negative = true;                                                                                         \
            v.s = static_cast<std::int64_t>(x);                                                                        \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            v.negative = false;                                                                                        \
            v.u = static_cast<std::uint64_t>(x);                                                                       \
        }                                                                                                              \
        return true;                                                                                                   \
    }
        RL_BOX(DataTypes_int8_t, std::int8_t)
        RL_BOX(DataTypes_int16_t, std::int16_t)
        RL_BOX(DataTypes_int32_t, std::int32_t)
        RL_BOX(DataTypes_int64_t, std::int64_t)
#undef RL_BOX
#define RL_UBOX(T, C)                                                                                                  \
    case RobotRaconteur::T: {                                                                                          \
        C x;                                                                                                           \
        std::memcpy(&x, d, sizeof x);                                                                                  \
        v.negative = false;                                                                                            \
        v.u = static_cast<std::uint64_t>(x);                                                                           \
        return true;                                                                                                   \
    }
        RL_UBOX(DataTypes_uint8_t, std::uint8_t)
        RL_UBOX(DataTypes_uint16_t, std::uint16_t)
        RL_UBOX(DataTypes_uint32_t, std::uint32_t)
        RL_UBOX(DataTypes_uint64_t, std::uint64_t)
#undef RL_UBOX
    default:
        return false;
    }
}

// Read an exact integer from a Lua number or a box. Returns false if it is not one.
bool read_int(lua_State* L, int idx, IntValue& v)
{
    if (lua_type(L, idx) == LUA_TNUMBER)
    {
#if LUA_VERSION_NUM >= 503
        if (lua_isinteger(L, idx))
        {
            lua_Integer i = lua_tointeger(L, idx);
            v.negative = i < 0;
            v.s = i;
            v.u = static_cast<std::uint64_t>(i);
            return true;
        }
#endif
        double d = lua_tonumber(L, idx);
        if (!(d == std::floor(d)) || !(d >= -9223372036854775808.0) || !(d < 18446744073709551616.0))
            return false;
        if (d < 0)
        {
            v.negative = true;
            v.s = static_cast<std::int64_t>(d);
        }
        else
        {
            v.negative = false;
            v.u = static_cast<std::uint64_t>(d);
        }
        return true;
    }
    if (const ArrayHeader* box = test_array(L, idx))
        return read_box(box, v);
    return false;
}

// Range check and store an integer into an element of integer type T.
template <typename T>
const char* store_int(void* dst, const IntValue& v)
{
    T x;
    if (v.negative)
    {
        if (!std::numeric_limits<T>::is_signed || v.s < static_cast<std::int64_t>(std::numeric_limits<T>::min()))
            return "value out of range for the element type";
        x = static_cast<T>(v.s);
    }
    else
    {
        if (v.u > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
            return "value out of range for the element type";
        x = static_cast<T>(v.u);
    }
    std::memcpy(dst, &x, sizeof x);
    return nullptr;
}

// Read {real=, imag=} or a number into a complex value.
bool read_complex(lua_State* L, int idx, double& re, double& im)
{
    if (lua_type(L, idx) == LUA_TNUMBER)
    {
        re = lua_tonumber(L, idx);
        im = 0;
        return true;
    }
    if (!lua_istable(L, idx))
        return false;
    idx = abs_index(L, idx);
    lua_pushliteral(L, "real");
    lua_rawget(L, idx);
    lua_pushliteral(L, "imag");
    lua_rawget(L, idx);
    bool ok = lua_type(L, -2) == LUA_TNUMBER && lua_type(L, -1) == LUA_TNUMBER;
    if (ok)
    {
        re = lua_tonumber(L, -2);
        im = lua_tonumber(L, -1);
    }
    lua_pop(L, 2);
    return ok;
}

void push_int64(lua_State* L, std::int64_t v)
{
#if LUA_VERSION_NUM >= 503
    lua_pushinteger(L, static_cast<lua_Integer>(v));
#else
    if (v >= -(1LL << 53) && v <= (1LL << 53))
    {
        lua_pushnumber(L, static_cast<lua_Number>(v));
        return;
    }
    std::int64_t one = 1;
    ArrayHeader* box = push_array(L, RobotRaconteur::DataTypes_int64_t, &one, 1);
    std::memcpy(array_data(box), &v, sizeof v);
#endif
}

void push_uint64(lua_State* L, std::uint64_t v)
{
#if LUA_VERSION_NUM >= 503
    if (v <= static_cast<std::uint64_t>(std::numeric_limits<lua_Integer>::max()))
    {
        lua_pushinteger(L, static_cast<lua_Integer>(v));
        return;
    }
#else
    if (v <= (1ULL << 53))
    {
        lua_pushnumber(L, static_cast<lua_Number>(v));
        return;
    }
#endif
    std::int64_t one = 1;
    ArrayHeader* box = push_array(L, RobotRaconteur::DataTypes_uint64_t, &one, 1);
    std::memcpy(array_data(box), &v, sizeof v);
}

void push_small_int(lua_State* L, std::int64_t v)
{
#if LUA_VERSION_NUM >= 503
    lua_pushinteger(L, static_cast<lua_Integer>(v));
#else
    lua_pushnumber(L, static_cast<lua_Number>(v));
#endif
}

ArrayHeader* check_array(lua_State* L, int idx)
{
    ArrayHeader* a = test_array(L, idx);
    if (!a)
        luaL_argerror(L, idx, lua_pushfstring(L, "rr.array expected, got %s", luaL_typename(L, idx)));
    return a;
}

DataTypes check_type(lua_State* L, int idx)
{
    const char* name = luaL_checkstring(L, idx);
    DataTypes t;
    if (!array_type_from_name(name, t))
        luaL_argerror(L, idx, lua_pushfstring(L, "unknown element type '%s'", name));
    return t;
}

std::int64_t check_count(lua_State* L, int idx, const char* what)
{
    lua_Number n = luaL_checknumber(L, idx);
    if (!(n >= 0) || n != std::floor(n) || n > 2147483647.0)
        luaL_argerror(L, idx, lua_pushfstring(L, "%s must be a non-negative integer", what));
    return static_cast<std::int64_t>(n);
}

// 1-based index in [1, n] -> 0-based.
std::int64_t check_index(lua_State* L, int idx, std::int64_t n)
{
    lua_Number v = luaL_checknumber(L, idx);
    if (v != std::floor(v) || v < 1 || v > static_cast<lua_Number>(n))
        luaL_argerror(L, idx, lua_pushfstring(L, "index %f out of range 1..%d", (double)v, (int)n));
    return static_cast<std::int64_t>(v) - 1;
}

// Fill elements of a from the flat sequence (table or array) at idx.
void fill_from(lua_State* L, ArrayHeader* a, int idx)
{
    idx = abs_index(L, idx);
    if (ArrayHeader* src = test_array(L, idx))
    {
        if (src->numel != a->numel)
            luaL_error(L, "rr.array: source has %d elements, expected %d", (int)src->numel, (int)a->numel);
        if (src->type == a->type)
        {
            std::memcpy(array_data(a), array_data(src), array_elem_size(static_cast<DataTypes>(a->type)) * a->numel);
            return;
        }
        for (std::int64_t k = 0; k < a->numel; ++k)
        {
            push_array_element(L, src, k);
            const char* err = set_array_element(L, a, k, -1);
            if (err)
                luaL_error(L, "rr.array: element %d: %s", (int)(k + 1), err);
            lua_pop(L, 1);
        }
        return;
    }
    if (!lua_istable(L, idx))
        luaL_error(L, "rr.array: values must be a table or rr.array, got %s", luaL_typename(L, idx));
    if (static_cast<std::int64_t>(raw_len(L, idx)) != a->numel)
        luaL_error(L, "rr.array: %d values given, expected %d", (int)raw_len(L, idx), (int)a->numel);
    for (std::int64_t k = 0; k < a->numel; ++k)
    {
        lua_rawgeti(L, idx, static_cast<int>(k + 1));
        const char* err = set_array_element(L, a, k, -1);
        if (err)
            luaL_error(L, "rr.array: element %d: %s", (int)(k + 1), err);
        lua_pop(L, 1);
    }
}

// Read a dims table {d1, d2, ...} into out; returns ndims.
int read_dims(lua_State* L, int idx, std::int64_t* out)
{
    if (!lua_istable(L, idx))
        luaL_argerror(L, idx, "dims must be a table of non-negative integers");
    int n = static_cast<int>(raw_len(L, idx));
    if (n < 1 || n > kMaxArrayDims)
        luaL_argerror(L, idx, lua_pushfstring(L, "dims must have 1 to %d entries", kMaxArrayDims));
    for (int i = 0; i < n; ++i)
    {
        lua_rawgeti(L, idx, i + 1);
        lua_Number d = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : -1;
        lua_pop(L, 1);
        if (!(d >= 0) || d != std::floor(d) || d > 2147483647.0)
            luaL_argerror(L, idx, "dims must be a table of non-negative integers");
        out[i] = static_cast<std::int64_t>(d);
    }
    return n;
}

// ------------------------------------------------------------------------------------------
// Methods

int m_type(lua_State* L)
{
    lua_pushstring(L, array_type_name(static_cast<DataTypes>(check_array(L, 1)->type)));
    return 1;
}

int m_numel(lua_State* L)
{
    push_small_int(L, check_array(L, 1)->numel);
    return 1;
}

int m_dims(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    lua_createtable(L, a->ndims, 0);
    for (int i = 0; i < a->ndims; ++i)
    {
        push_small_int(L, array_dims(a)[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// Offset of element (i1, i2, ...) given at stack positions first..first+ndims-1, or of the
// linear index when one index is given.
std::int64_t element_offset(lua_State* L, ArrayHeader* a, int first, int nidx)
{
    if (nidx == 1)
        return check_index(L, first, a->numel);
    if (nidx != a->ndims)
        luaL_error(L, "rr.array: %d indices given for a %d-dimensional array", nidx, (int)a->ndims);
    std::int64_t off = 0, stride = 1;
    for (int d = 0; d < a->ndims; ++d)
    {
        off += check_index(L, first + d, array_dims(a)[d]) * stride;
        stride *= array_dims(a)[d];
    }
    return off;
}

int m_get(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    int n = lua_gettop(L) - 1;
    if (n < 1)
        return luaL_error(L, "rr.array: get needs at least one index");
    push_array_element(L, a, element_offset(L, a, 2, n));
    return 1;
}

int m_set(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    int n = lua_gettop(L) - 1;
    if (n < 2)
        return luaL_error(L, "rr.array: set needs an index and a value");
    std::int64_t k = element_offset(L, a, 2, n - 1);
    const char* err = set_array_element(L, a, k, n + 1);
    if (err)
        return luaL_error(L, "rr.array: %s", err);
    return 0;
}

int m_totable(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    lua_createtable(L, static_cast<int>(a->numel), 0);
    for (std::int64_t k = 0; k < a->numel; ++k)
    {
        push_array_element(L, a, k);
        lua_rawseti(L, -2, static_cast<int>(k + 1));
    }
    return 1;
}

int m_copy(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    ArrayHeader* b = push_array(L, static_cast<DataTypes>(a->type), array_dims(a), a->ndims);
    std::memcpy(array_data(b), array_data(a), array_elem_size(static_cast<DataTypes>(a->type)) * a->numel);
    return 1;
}

int m_reshape(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    std::int64_t dims[kMaxArrayDims];
    int nd = read_dims(L, 2, dims);
    std::int64_t numel = 1;
    for (int i = 0; i < nd; ++i)
        numel *= dims[i];
    if (numel != a->numel)
        return luaL_error(L, "rr.array: reshape to %d elements from %d", (int)numel, (int)a->numel);
    ArrayHeader* b = push_array(L, static_cast<DataTypes>(a->type), dims, nd);
    std::memcpy(array_data(b), array_data(a), array_elem_size(static_cast<DataTypes>(a->type)) * a->numel);
    return 1;
}

int mm_index(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        push_array_element(L, a, check_index(L, 2, a->numel));
        return 1;
    }
    lua_pushvalue(L, 2);
    lua_rawget(L, lua_upvalueindex(1));
    return 1;
}

int mm_newindex(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    if (lua_type(L, 2) != LUA_TNUMBER)
        return luaL_error(L, "rr.array: cannot set a %s key", luaL_typename(L, 2));
    std::int64_t k = check_index(L, 2, a->numel);
    const char* err = set_array_element(L, a, k, 3);
    if (err)
        return luaL_error(L, "rr.array: element %d: %s", (int)(k + 1), err);
    return 0;
}

int mm_len(lua_State* L)
{
    push_small_int(L, check_array(L, 1)->numel);
    return 1;
}

int mm_eq(lua_State* L)
{
    ArrayHeader* a = test_array(L, 1);
    ArrayHeader* b = test_array(L, 2);
    bool eq = a && b && a->type == b->type && a->ndims == b->ndims && a->numel == b->numel &&
              std::memcmp(array_dims(a), array_dims(b), sizeof(std::int64_t) * a->ndims) == 0 &&
              std::memcmp(array_data(a), array_data(b),
                          array_elem_size(static_cast<DataTypes>(a->type)) * a->numel) == 0;
    lua_pushboolean(L, eq);
    return 1;
}

int mm_tostring(lua_State* L)
{
    ArrayHeader* a = check_array(L, 1);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    char tmp[64];
    std::snprintf(tmp, sizeof tmp, "rr.array %s[", array_type_name(static_cast<DataTypes>(a->type)));
    luaL_addstring(&b, tmp);
    for (int i = 0; i < a->ndims; ++i)
    {
        std::snprintf(tmp, sizeof tmp, i ? ",%lld" : "%lld", static_cast<long long>(array_dims(a)[i]));
        luaL_addstring(&b, tmp);
    }
    luaL_addstring(&b, "]");
    const std::int64_t kShow = 16;
    for (std::int64_t k = 0; k < a->numel && k < kShow; ++k)
    {
        luaL_addstring(&b, k ? ", " : " ");
        push_array_element(L, a, k);
        if (lua_istable(L, -1))
        {
            lua_pushliteral(L, "real");
            lua_rawget(L, -2);
            lua_pushliteral(L, "imag");
            lua_rawget(L, -3);
            std::snprintf(tmp, sizeof tmp, "%.17g%+.17gi", lua_tonumber(L, -2), lua_tonumber(L, -1));
            lua_pop(L, 3);
            luaL_addstring(&b, tmp);
        }
        else if (lua_type(L, -1) == LUA_TBOOLEAN)
        {
            luaL_addstring(&b, lua_toboolean(L, -1) ? "true" : "false");
            lua_pop(L, 1);
        }
        else if (ArrayHeader* box = test_array(L, -1))
        {
            IntValue v{};
            read_box(box, v);
            if (v.negative)
                std::snprintf(tmp, sizeof tmp, "%lld", static_cast<long long>(v.s));
            else
                std::snprintf(tmp, sizeof tmp, "%llu", static_cast<unsigned long long>(v.u));
            lua_pop(L, 1);
            luaL_addstring(&b, tmp);
        }
        else
        {
            lua_Number x = lua_tonumber(L, -1);
            std::snprintf(tmp, sizeof tmp, "%.17g", static_cast<double>(x));
#if LUA_VERSION_NUM >= 503
            if (lua_isinteger(L, -1))
                std::snprintf(tmp, sizeof tmp, "%lld", static_cast<long long>(lua_tointeger(L, -1)));
#endif
            lua_pop(L, 1);
            luaL_addstring(&b, tmp);
        }
    }
    if (a->numel > kShow)
        luaL_addstring(&b, ", ...");
    luaL_pushresult(&b);
    return 1;
}

void set_funcs(lua_State* L, const luaL_Reg* r)
{
    for (; r->name; ++r)
    {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
}

const luaL_Reg kMethods[] = {{"type", m_type},       {"numel", m_numel}, {"dims", m_dims},
                             {"get", m_get},         {"set", m_set},     {"toTable", m_totable},
                             {"copy", m_copy},       {"reshape", m_reshape}, {nullptr, nullptr}};

const luaL_Reg kMeta[] = {{"__newindex", mm_newindex}, {"__len", mm_len}, {"__eq", mm_eq},
                          {"__tostring", mm_tostring}, {nullptr, nullptr}};

void push_metatable(lua_State* L)
{
    if (luaL_newmetatable(L, kArrayMeta))
    {
        set_funcs(L, kMeta);
        lua_newtable(L);
        set_funcs(L, kMethods);
        lua_pushcclosure(L, mm_index, 1);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, kArrayMeta);
        lua_setfield(L, -2, "__name");
    }
}

} // namespace

// ------------------------------------------------------------------------------------------
// Shared API

std::size_t array_elem_size(DataTypes t)
{
    const TypeInfo* i = info(t);
    return i ? i->size : 0;
}

const char* array_type_name(DataTypes t)
{
    const TypeInfo* i = info(t);
    return i ? i->name : nullptr;
}

bool array_type_from_name(const char* name, DataTypes& t)
{
    for (const TypeInfo& i : kTypes)
        if (std::strcmp(i.name, name) == 0)
        {
            t = i.type;
            return true;
        }
    return false;
}

ArrayHeader* test_array(lua_State* L, int idx)
{
    void* p = lua_touserdata(L, idx);
    if (!p || !lua_getmetatable(L, idx))
        return nullptr;
    luaL_getmetatable(L, kArrayMeta);
    bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same ? static_cast<ArrayHeader*>(p) : nullptr;
}

ArrayHeader* push_array(lua_State* L, DataTypes type, const std::int64_t* dims, int ndims)
{
    std::size_t es = array_elem_size(type);
    if (es == 0)
        luaL_error(L, "rr.array: unsupported element type %d", static_cast<int>(type));
    if (ndims < 1 || ndims > kMaxArrayDims)
        luaL_error(L, "rr.array: %d dimensions (1 to %d allowed)", ndims, kMaxArrayDims);
    std::int64_t numel = 1;
    for (int i = 0; i < ndims; ++i)
    {
        if (dims[i] < 0 || dims[i] > 2147483647)
            luaL_error(L, "rr.array: bad dimension %lld", static_cast<long long>(dims[i]));
        if (dims[i] != 0 && numel > 2147483647 / dims[i])
            luaL_error(L, "rr.array: too many elements");
        numel *= dims[i];
    }
    std::size_t bytes = array_data_offset(ndims) + es * static_cast<std::size_t>(numel);
    auto* a = static_cast<ArrayHeader*>(lua_newuserdata(L, bytes));
    std::memset(a, 0, bytes);
    a->type = static_cast<std::int32_t>(type);
    a->ndims = ndims;
    a->numel = numel;
    std::memcpy(array_dims(a), dims, sizeof(std::int64_t) * ndims);
    push_metatable(L);
    lua_setmetatable(L, -2);
    return a;
}

void push_array_element(lua_State* L, const ArrayHeader* a, std::int64_t k)
{
    DataTypes t = static_cast<DataTypes>(a->type);
    push_element_raw(L, t, static_cast<const char*>(array_data(a)) + array_elem_size(t) * k);
}

void push_element_raw(lua_State* L, DataTypes t, const void* vp)
{
    const char* p = static_cast<const char*>(vp);
    switch (t)
    {
#define RL_GET(T, C, PUSH)                                                                                             \
    case RobotRaconteur::T: {                                                                                          \
        C x;                                                                                                           \
        std::memcpy(&x, p, sizeof x);                                                                                  \
        PUSH;                                                                                                          \
        return;                                                                                                        \
    }
        RL_GET(DataTypes_double_t, double, lua_pushnumber(L, x))
        RL_GET(DataTypes_single_t, float, lua_pushnumber(L, x))
        RL_GET(DataTypes_int8_t, std::int8_t, push_small_int(L, x))
        RL_GET(DataTypes_uint8_t, std::uint8_t, push_small_int(L, x))
        RL_GET(DataTypes_int16_t, std::int16_t, push_small_int(L, x))
        RL_GET(DataTypes_uint16_t, std::uint16_t, push_small_int(L, x))
        RL_GET(DataTypes_int32_t, std::int32_t, push_small_int(L, x))
        RL_GET(DataTypes_uint32_t, std::uint32_t, push_small_int(L, x))
        RL_GET(DataTypes_int64_t, std::int64_t, push_int64(L, x))
        RL_GET(DataTypes_uint64_t, std::uint64_t, push_uint64(L, x))
        RL_GET(DataTypes_bool_t, std::uint8_t, lua_pushboolean(L, x != 0))
#undef RL_GET
    case RobotRaconteur::DataTypes_cdouble_t:
    case RobotRaconteur::DataTypes_csingle_t: {
        double re, im;
        if (t == RobotRaconteur::DataTypes_cdouble_t)
        {
            std::memcpy(&re, p, 8);
            std::memcpy(&im, p + 8, 8);
        }
        else
        {
            float r, i;
            std::memcpy(&r, p, 4);
            std::memcpy(&i, p + 4, 4);
            re = r;
            im = i;
        }
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, re);
        lua_setfield(L, -2, "real");
        lua_pushnumber(L, im);
        lua_setfield(L, -2, "imag");
        return;
    }
    default:
        lua_pushnil(L);
    }
}

const char* set_array_element(lua_State* L, ArrayHeader* a, std::int64_t k, int idx)
{
    DataTypes t = static_cast<DataTypes>(a->type);
    return set_element_raw(L, t, static_cast<char*>(array_data(a)) + array_elem_size(t) * k, idx);
}

const char* set_element_raw(lua_State* L, DataTypes t, void* vp, int idx)
{
    char* p = static_cast<char*>(vp);
    if (t == RobotRaconteur::DataTypes_double_t || t == RobotRaconteur::DataTypes_single_t)
    {
        if (lua_type(L, idx) != LUA_TNUMBER)
            return "expected a number";
        double d = lua_tonumber(L, idx);
        if (t == RobotRaconteur::DataTypes_double_t)
            std::memcpy(p, &d, 8);
        else
        {
            float f = static_cast<float>(d);
            std::memcpy(p, &f, 4);
        }
        return nullptr;
    }
    if (t == RobotRaconteur::DataTypes_bool_t)
    {
        std::uint8_t b;
        if (lua_type(L, idx) == LUA_TBOOLEAN)
            b = lua_toboolean(L, idx) ? 1 : 0;
        else if (lua_type(L, idx) == LUA_TNUMBER && (lua_tonumber(L, idx) == 0 || lua_tonumber(L, idx) == 1))
            b = lua_tonumber(L, idx) != 0 ? 1 : 0;
        else
            return "expected a boolean (or 0 or 1)";
        *p = static_cast<char>(b);
        return nullptr;
    }
    if (is_complex_type(t))
    {
        double re, im;
        if (!read_complex(L, idx, re, im))
            return "expected a number or {real=, imag=}";
        if (t == RobotRaconteur::DataTypes_cdouble_t)
        {
            std::memcpy(p, &re, 8);
            std::memcpy(p + 8, &im, 8);
        }
        else
        {
            float r = static_cast<float>(re), i = static_cast<float>(im);
            std::memcpy(p, &r, 4);
            std::memcpy(p + 4, &i, 4);
        }
        return nullptr;
    }
    IntValue v{};
    if (!read_int(L, idx, v))
        return "expected an integer";
    switch (t)
    {
    case RobotRaconteur::DataTypes_int8_t:
        return store_int<std::int8_t>(p, v);
    case RobotRaconteur::DataTypes_uint8_t:
        return store_int<std::uint8_t>(p, v);
    case RobotRaconteur::DataTypes_int16_t:
        return store_int<std::int16_t>(p, v);
    case RobotRaconteur::DataTypes_uint16_t:
        return store_int<std::uint16_t>(p, v);
    case RobotRaconteur::DataTypes_int32_t:
        return store_int<std::int32_t>(p, v);
    case RobotRaconteur::DataTypes_uint32_t:
        return store_int<std::uint32_t>(p, v);
    case RobotRaconteur::DataTypes_int64_t:
        return store_int<std::int64_t>(p, v);
    case RobotRaconteur::DataTypes_uint64_t:
        return store_int<std::uint64_t>(p, v);
    default:
        return "unsupported element type";
    }
}

} // namespace rl

// rr.array(type, n [, fill]) | rr.array(type, values)
int rl_array_new(lua_State* L)
{
    DataTypes t = rl::check_type(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        std::int64_t n = rl::check_count(L, 2, "length");
        bool fill = !lua_isnoneornil(L, 3); // check before pushing the array onto the stack
        lua_settop(L, 3);
        rl::ArrayHeader* a = rl::push_array(L, t, &n, 1);
        if (fill)
            for (std::int64_t k = 0; k < n; ++k)
            {
                const char* err = rl::set_array_element(L, a, k, 3);
                if (err)
                    return luaL_error(L, "rr.array: fill value: %s", err);
            }
        return 1;
    }
    std::int64_t n;
    if (rl::ArrayHeader* src = rl::test_array(L, 2))
        n = src->numel;
    else if (lua_istable(L, 2))
        n = static_cast<std::int64_t>(rl::raw_len(L, 2));
    else
        return luaL_argerror(L, 2, "expected a length, a table of values or an rr.array");
    rl::ArrayHeader* a = rl::push_array(L, t, &n, 1);
    rl::fill_from(L, a, 2);
    return 1;
}

// rr.multidim(type, dims [, values]): values are column-major (a table or rr.array).
int rl_multidim_new(lua_State* L)
{
    DataTypes t = rl::check_type(L, 1);
    std::int64_t dims[rl::kMaxArrayDims];
    int nd = rl::read_dims(L, 2, dims);
    bool values = !lua_isnoneornil(L, 3);
    lua_settop(L, 3);
    rl::ArrayHeader* a = rl::push_array(L, t, dims, nd);
    if (values)
        rl::fill_from(L, a, 3);
    return 1;
}

int rl_isarray(lua_State* L)
{
    lua_pushboolean(L, rl::test_array(L, 1) != nullptr);
    return 1;
}
