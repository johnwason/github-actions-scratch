// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Default values (newStructure) and service constants (getConstants), from service definitions.
#include "rrlua/marshal.hpp"

#include "rrlua/array.hpp"
#include "rrlua/lua.hpp"

#include <cstring>
#include <map>
#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
void need(lua_State* L)
{
    if (!lua_checkstack(L, 16))
        throw std::runtime_error("Lua stack overflow");
}

bool is_float(DataTypes t) { return t == DataTypes_double_t || t == DataTypes_single_t; }

bool fixed_length(const RR_SHARED_PTR<TypeDefinition>& t)
{
    return !t->ArrayVarLength && !t->ArrayLength.empty() && t->ArrayLength[0] > 0;
}

// A list of `n` defaults of a pod or namedarray, with a dims field for multidim.
void push_entry_list(lua_State* L, const RR_SHARED_PTR<ServiceEntryDefinition>& def, const MarshalContext& ctx,
                     size_t n, const std::vector<int32_t>* dims)
{
    lua_createtable(L, static_cast<int>(n), dims ? 1 : 0);
    for (size_t i = 0; i < n; ++i)
    {
        push_default_entry(L, def, ctx);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    if (dims)
    {
        lua_createtable(L, static_cast<int>(dims->size()), 0);
        for (size_t i = 0; i < dims->size(); ++i)
        {
            lua_pushinteger(L, (*dims)[i]);
            lua_rawseti(L, -2, static_cast<int>(i + 1));
        }
        lua_setfield(L, -2, "dims");
    }
}

template <typename T>
void push_constant_numeric(lua_State* L, const ConstantDefinition& c)
{
    DataTypes t = c.Type->Type;
    if (c.Type->ArrayType == DataTypes_ArrayTypes_none)
    {
        T v = c.ValueToScalar<T>();
        push_element_raw(L, t, &v);
        return;
    }
    RR_INTRUSIVE_PTR<RRArray<T> > a = c.ValueToArray<T>();
    int64_t n = static_cast<int64_t>(a->size());
    ArrayHeader* h = push_array(L, t, &n, 1);
    if (n)
        std::memcpy(array_data(h), a->data(), sizeof(T) * a->size());
}

void push_constant_value(lua_State* L, ConstantDefinition& c)
{
    need(L);
    DataTypes t = c.Type->Type;
    switch (t)
    {
    case DataTypes_string_t: {
        std::string s = c.ValueToString();
        lua_pushlstring(L, s.data(), s.size());
        return;
    }
    case DataTypes_double_t:
        return push_constant_numeric<double>(L, c);
    case DataTypes_single_t:
        return push_constant_numeric<float>(L, c);
    case DataTypes_int8_t:
        return push_constant_numeric<int8_t>(L, c);
    case DataTypes_uint8_t:
        return push_constant_numeric<uint8_t>(L, c);
    case DataTypes_int16_t:
        return push_constant_numeric<int16_t>(L, c);
    case DataTypes_uint16_t:
        return push_constant_numeric<uint16_t>(L, c);
    case DataTypes_int32_t:
        return push_constant_numeric<int32_t>(L, c);
    case DataTypes_uint32_t:
        return push_constant_numeric<uint32_t>(L, c);
    case DataTypes_int64_t:
        return push_constant_numeric<int64_t>(L, c);
    case DataTypes_uint64_t:
        return push_constant_numeric<uint64_t>(L, c);
    default:
        throw DataTypeException("constant " + c.Name + ": unsupported constant type");
    }
}

// Constants of one scope (a service or an entry) into the table at the top of the stack.
// Struct constants are filled in later, from the service's own constants.
void add_constants(lua_State* L, const std::vector<RR_SHARED_PTR<ConstantDefinition> >& cs,
                   std::vector<RR_SHARED_PTR<ConstantDefinition> >& structs)
{
    for (const RR_SHARED_PTR<ConstantDefinition>& c : cs)
    {
        if (c->Type->Type == DataTypes_namedtype_t)
        {
            structs.push_back(c);
            continue;
        }
        push_constant_value(L, *c);
        lua_setfield(L, -2, c->Name.c_str());
    }
}
} // namespace

void push_default(lua_State* L, const RR_SHARED_PTR<TypeDefinition>& t, const MarshalContext& ctx)
{
    need(L);
    if (t->ContainerType != DataTypes_ContainerTypes_none)
    {
        lua_newtable(L); // list or map
        return;
    }
    DataTypes dt = t->Type;
    if (dt == DataTypes_varvalue_t)
    {
        lua_pushnil(L);
        return;
    }
    if (dt == DataTypes_string_t)
    {
        lua_pushliteral(L, "");
        return;
    }
    if (dt == DataTypes_namedtype_t)
    {
        RR_SHARED_PTR<NamedTypeDefinition> nt = ctx.resolve(t);
        switch (nt->RRDataType())
        {
        case DataTypes_enum_t:
            lua_pushinteger(L, 0);
            return;
        case DataTypes_pod_t:
        case DataTypes_namedarray_t: {
            RR_SHARED_PTR<ServiceEntryDefinition> def = RR_STATIC_POINTER_CAST<ServiceEntryDefinition>(nt);
            if (t->ArrayType == DataTypes_ArrayTypes_none)
                push_default_entry(L, def, ctx);
            else if (t->ArrayType == DataTypes_ArrayTypes_array)
                push_entry_list(L, def, ctx, fixed_length(t) ? static_cast<size_t>(t->ArrayLength[0]) : 0, nullptr);
            else
            {
                std::vector<int32_t> dims = t->ArrayLength.empty() ? std::vector<int32_t>(2, 0) : t->ArrayLength;
                size_t n = 1;
                for (int32_t d : dims)
                    n *= static_cast<size_t>(d);
                push_entry_list(L, def, ctx, n, &dims);
            }
            return;
        }
        default:
            lua_pushnil(L); // a nested struct is absent, as in Python's NewStructure
            return;
        }
    }
    if (t->ArrayType == DataTypes_ArrayTypes_none)
    {
        if (dt == DataTypes_bool_t)
            lua_pushboolean(L, 0);
        else if (dt == DataTypes_cdouble_t || dt == DataTypes_csingle_t)
        {
            lua_createtable(L, 0, 2);
            lua_pushnumber(L, 0);
            lua_setfield(L, -2, "real");
            lua_pushnumber(L, 0);
            lua_setfield(L, -2, "imag");
        }
        else if (is_float(dt))
            lua_pushnumber(L, 0);
        else
            lua_pushinteger(L, 0);
        return;
    }
    if (t->ArrayType == DataTypes_ArrayTypes_array)
    {
        int64_t n = fixed_length(t) ? t->ArrayLength[0] : 0;
        push_array(L, dt, &n, 1);
        return;
    }
    std::vector<int64_t> dims;
    if (t->ArrayLength.empty())
        dims.assign(2, 0);
    else
        for (int32_t d : t->ArrayLength)
            dims.push_back(d);
    push_array(L, dt, dims.data(), static_cast<int>(dims.size()));
}

void push_default_entry(lua_State* L, const RR_SHARED_PTR<ServiceEntryDefinition>& def, const MarshalContext& ctx)
{
    need(L);
    lua_createtable(L, 0, static_cast<int>(def->Members.size()));
    for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        push_default(L, p->Type, ctx);
        lua_setfield(L, -2, p->Name.c_str());
    }
}

void push_constants(lua_State* L, const RR_SHARED_PTR<ServiceDefinition>& def)
{
    need(L);
    lua_newtable(L);
    int o = lua_gettop(L);
    std::vector<RR_SHARED_PTR<ConstantDefinition> > structs;
    add_constants(L, def->Constants, structs);
    // Struct constants refer to the service's other constants by name (possibly to other struct
    // constants): resolve them in passes.
    std::vector<RR_SHARED_PTR<ConstantDefinition> > pending = structs;
    for (int pass = 0; pass < 16 && !pending.empty(); ++pass)
    {
        std::vector<RR_SHARED_PTR<ConstantDefinition> > later;
        for (const RR_SHARED_PTR<ConstantDefinition>& c : pending)
        {
            std::vector<ConstantDefinition_StructField> fields = c->ValueToStructFields();
            bool ready = true;
            for (const ConstantDefinition_StructField& f : fields)
            {
                lua_getfield(L, o, f.ConstantRefName.c_str());
                ready = ready && !lua_isnil(L, -1);
                lua_pop(L, 1);
            }
            if (!ready)
            {
                later.push_back(c);
                continue;
            }
            lua_createtable(L, 0, static_cast<int>(fields.size()));
            for (const ConstantDefinition_StructField& f : fields)
            {
                lua_getfield(L, o, f.ConstantRefName.c_str());
                lua_setfield(L, -2, f.Name.c_str());
            }
            lua_setfield(L, o, c->Name.c_str());
        }
        if (later.size() == pending.size())
            throw ServiceException("constant " + later[0]->Name + " refers to an unknown constant");
        pending.swap(later);
    }
    // Constants declared in objects, structs, pods and namedarrays, per entry.
    std::vector<RR_SHARED_PTR<ServiceEntryDefinition> > entries;
    entries.insert(entries.end(), def->Objects.begin(), def->Objects.end());
    entries.insert(entries.end(), def->Structures.begin(), def->Structures.end());
    entries.insert(entries.end(), def->Pods.begin(), def->Pods.end());
    entries.insert(entries.end(), def->NamedArrays.begin(), def->NamedArrays.end());
    for (const RR_SHARED_PTR<ServiceEntryDefinition>& e : entries)
    {
        if (e->Constants.empty())
            continue;
        lua_newtable(L);
        std::vector<RR_SHARED_PTR<ConstantDefinition> > ignored; // struct constants are service-level only
        add_constants(L, e->Constants, ignored);
        lua_setfield(L, o, e->Name.c_str());
    }
    for (const RR_SHARED_PTR<EnumDefinition>& e : def->Enums)
    {
        lua_createtable(L, 0, static_cast<int>(e->Values.size()));
        for (const EnumDefinitionValue& v : e->Values)
        {
            lua_pushinteger(L, v.Value);
            lua_setfield(L, -2, v.Name.c_str());
        }
        lua_setfield(L, o, e->Name.c_str());
    }
}

} // namespace rl
