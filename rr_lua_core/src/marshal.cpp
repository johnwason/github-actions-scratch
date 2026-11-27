// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Lua <-> MessageElement marshalling (see rrlua/marshal.hpp). Wire formats follow RR core's
// own bindings (RobotRaconteurPython/PythonTypeSupport.cpp, RobotRaconteurMex).

#include "rrlua/marshal.hpp"

#include "rrlua/array.hpp"
#include "rrlua/guard.hpp"

#include <RobotRaconteur/IOUtils.h>

#include <boost/shared_array.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

using namespace RobotRaconteur;

namespace rl
{
namespace
{

const char kNullKey = 0;
const char* const kVarValueMeta = "robotraconteur.varvalue";
const char* const kNullMeta = "robotraconteur.null";

#if LUA_VERSION_NUM >= 502
size_t raw_len(lua_State* L, int idx) { return lua_rawlen(L, idx); }
#else
size_t raw_len(lua_State* L, int idx) { return lua_objlen(L, idx); }
#endif

int abs_index(lua_State* L, int idx) { return (idx > 0 || idx <= LUA_REGISTRYINDEX) ? idx : lua_gettop(L) + idx + 1; }

void need(lua_State* L, int n)
{
    if (!lua_checkstack(L, n))
        throw DataTypeException("Lua stack overflow while marshalling (value nested too deeply?)");
}

bool is_varvalue(lua_State* L, int idx)
{
    if (!lua_istable(L, idx) || !lua_getmetatable(L, idx))
        return false;
    luaL_getmetatable(L, kVarValueMeta);
    bool r = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return r;
}

std::string lua_str(lua_State* L, int idx)
{
    size_t n = 0;
    const char* s = lua_tolstring(L, idx, &n); // only called on LUA_TSTRING values
    return std::string(s, n);
}

uint32_t product(const std::vector<int32_t>& v)
{
    uint32_t c = 1;
    for (int32_t x : v)
        c *= static_cast<uint32_t>(x);
    return c;
}

RR_SHARED_PTR<ServiceEntryDefinition> as_entry(const RR_SHARED_PTR<NamedTypeDefinition>& nt, DataTypes expected,
                                               const char* what)
{
    RR_SHARED_PTR<ServiceEntryDefinition> e = RR_DYNAMIC_POINTER_CAST<ServiceEntryDefinition>(nt);
    if (!e || e->EntryType != expected)
        throw DataTypeException(std::string("expected a ") + what + " type");
    return e;
}

RR_SHARED_PTR<PropertyDefinition> field_def(const RR_SHARED_PTR<MemberDefinition>& m, const std::string& owner)
{
    RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
    if (!p)
        throw ServiceException("invalid field definition in " + owner);
    return p;
}

// ------------------------------------------------------------------------------------------

// Nesting limit for packing and unpacking, matching RR core's message reader (512 levels). It
// stops a self-referencing Lua table, or a recursive definition, from overflowing the C stack.
constexpr int kMaxNesting = 512;

struct DepthGuard
{
    int& d;
    explicit DepthGuard(int& depth) : d(depth)
    {
        if (++d > kMaxNesting)
        {
            --d;
            throw DataTypeException("value nested more than " + std::to_string(kMaxNesting) +
                                    " levels deep (a table that contains itself?)");
        }
    }
    ~DepthGuard() { --d; }
};

// ------------------------------------------------------------------------------------------

class Packer
{
  public:
    Packer(lua_State* L, const MarshalContext& ctx) : L_(L), ctx_(ctx) {}

    RR_INTRUSIVE_PTR<MessageElement> pack(int idx, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        DepthGuard depth(depth_);
        need(L_, 8);
        idx = abs_index(L_, idx);

        if (type->Type == DataTypes_varvalue_t && type->ContainerType == DataTypes_ContainerTypes_none &&
            !lua_isnil(L_, idx) && !is_null(L_, idx))
            return pack_varvalue(idx, type);

        RR_INTRUSIVE_PTR<MessageElement> element = CreateMessageElement();
        element->ElementName = type->Name;

        if (lua_isnil(L_, idx) || is_null(L_, idx))
        {
            check_nullable(type);
            element->ElementType = DataTypes_void_t;
            element->DataCount = 0;
            element->SetData(RR_INTRUSIVE_PTR<MessageElementData>());
            return element;
        }

        switch (type->ContainerType)
        {
        case DataTypes_ContainerTypes_list:
            element->SetData(pack_list(idx, type));
            return element;
        case DataTypes_ContainerTypes_map_int32:
            element->SetData(pack_map_int32(idx, type));
            return element;
        case DataTypes_ContainerTypes_map_string:
            element->SetData(pack_map_string(idx, type));
            return element;
        default:
            break;
        }

        if (IsTypeNumeric(type->Type) && type->ArrayType == DataTypes_ArrayTypes_multidimarray)
        {
            element->SetData(pack_multidim(idx, type));
            return element;
        }

        if (type->Type == DataTypes_namedtype_t)
        {
            RR_SHARED_PTR<NamedTypeDefinition> nt = ctx_.resolve(type);
            switch (nt->RRDataType())
            {
            case DataTypes_structure_t: {
                RR_SHARED_PTR<ServiceEntryDefinition> def = as_entry(nt, DataTypes_structure_t, "struct");
                if (type->ArrayType != DataTypes_ArrayTypes_none)
                    throw DataTypeException("arrays of structs are not valid");
                element->SetData(pack_struct(idx, def));
                return element;
            }
            case DataTypes_enum_t:
                if (type->ArrayType != DataTypes_ArrayTypes_none)
                    throw DataTypeException("arrays of enums are not valid");
                element->SetData(pack_enum(idx, RR_STATIC_POINTER_CAST<EnumDefinition>(nt)));
                return element;
            case DataTypes_pod_t:
                if (type->ArrayType == DataTypes_ArrayTypes_multidimarray)
                    element->SetData(pack_composite_multidim(idx, type, as_entry(nt, DataTypes_pod_t, "pod"), true));
                else
                    element->SetData(pack_pods(idx, type, as_entry(nt, DataTypes_pod_t, "pod")));
                return element;
            case DataTypes_namedarray_t:
                if (type->ArrayType == DataTypes_ArrayTypes_multidimarray)
                    element->SetData(
                        pack_composite_multidim(idx, type, as_entry(nt, DataTypes_namedarray_t, "namedarray"), false));
                else
                    element->SetData(pack_namedarrays(idx, type, as_entry(nt, DataTypes_namedarray_t, "namedarray")));
                return element;
            default:
                throw DataTypeException("unsupported named type " + nt->ResolveQualifiedName());
            }
        }

        if (type->Type == DataTypes_string_t)
        {
            if (lua_type(L_, idx) != LUA_TSTRING)
                fail(std::string("expected a string, got ") + luaL_typename(L_, idx));
            size_t n = 0;
            const char* s = lua_tolstring(L_, idx, &n);
            element->SetData(AttachRRArrayCopy<char>(s, n));
            return element;
        }

        if (IsTypeNumeric(type->Type))
        {
            element->SetData(pack_numeric(idx, type));
            return element;
        }

        fail("cannot pack type " + type->ToString());
        return element;
    }

  private:
    lua_State* L_;
    const MarshalContext& ctx_;
    std::vector<std::string> path_;
    int depth_ = 0;

    struct Level
    {
        Packer& p;
        Level(Packer& pp, const std::string& name) : p(pp) { p.path_.push_back(name); }
        ~Level() { p.path_.pop_back(); }
    };

    [[noreturn]] void fail(const std::string& msg)
    {
        std::string where;
        for (const std::string& s : path_)
            where += (s.empty() || s[0] == '[' || where.empty()) ? s : "." + s;
        throw DataTypeException(where.empty() ? msg : msg + " for field \"" + where + "\"");
    }

    void check_nullable(const RR_SHARED_PTR<TypeDefinition>& type)
    {
        if (type->ContainerType != DataTypes_ContainerTypes_none)
            return;
        if (IsTypeNumeric(type->Type))
            fail("numbers and numeric arrays must not be nil");
        if (type->Type == DataTypes_string_t)
            fail("strings must not be nil");
        if (type->Type == DataTypes_namedtype_t)
        {
            DataTypes t = ctx_.resolve(type)->RRDataType();
            if (t == DataTypes_pod_t || t == DataTypes_namedarray_t || t == DataTypes_enum_t)
                fail("pods, namedarrays and enums must not be nil");
        }
    }

    RR_INTRUSIVE_PTR<MessageElement> pack_varvalue(int idx, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        RR_SHARED_PTR<TypeDefinition> t2 = RR_MAKE_SHARED<TypeDefinition>();
        int value_idx = idx;
        bool popped = false;
        if (is_varvalue(L_, idx))
        {
            lua_pushliteral(L_, "type");
            lua_rawget(L_, idx);
            if (lua_type(L_, -1) != LUA_TSTRING)
            {
                lua_pop(L_, 1);
                fail("invalid rr.varvalue: no type");
            }
            std::string ts = lua_str(L_, -1);
            lua_pop(L_, 1);
            try
            {
                t2->FromString(ts + " value");
            }
            catch (std::exception& e)
            {
                fail("invalid rr.varvalue type '" + ts + "': " + e.what());
            }
            if (t2->Type == DataTypes_namedtype_t)
            {
                RR_SHARED_PTR<NamedTypeDefinition> d = ctx_.resolve(t2);
                if (d->RRDataType() == DataTypes_pod_t && t2->ArrayType == DataTypes_ArrayTypes_none)
                    t2->ArrayType = DataTypes_ArrayTypes_array; // pods travel as pod arrays in varvalue
            }
            lua_pushliteral(L_, "value");
            lua_rawget(L_, idx);
            value_idx = lua_gettop(L_);
            popped = true;
        }
        else if (lua_type(L_, idx) == LUA_TSTRING)
        {
            t2->Type = DataTypes_string_t;
        }
        else if (ArrayHeader* a = test_array(L_, idx))
        {
            t2->Type = static_cast<DataTypes>(a->type);
            t2->ArrayType = a->ndims == 1 ? DataTypes_ArrayTypes_array : DataTypes_ArrayTypes_multidimarray;
            t2->ArrayVarLength = true;
            t2->ArrayLength.push_back(0);
        }
        else
        {
            fail(std::string("a varvalue needs rr.varvalue(type, value) for a ") + luaL_typename(L_, idx) +
                 " (strings and rr.array are detected)");
        }
        t2->Name = type->Name;
        RR_INTRUSIVE_PTR<MessageElement> el = pack(value_idx, t2);
        if (popped)
            lua_pop(L_, 1);
        return el;
    }

    void require_table(int idx, const char* what)
    {
        if (!lua_istable(L_, idx) || is_varvalue(L_, idx))
            fail(std::string("expected a table for a ") + what + ", got " + luaL_typename(L_, idx));
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_list(int idx, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        require_table(idx, "list");
        RR_SHARED_PTR<TypeDefinition> t2 = type->Clone();
        t2->RemoveContainers();
        int32_t n = static_cast<int32_t>(raw_len(L_, idx));
        // Reject keys other than 1..n so a map passed as a list is not silently truncated.
        int32_t keys = 0;
        lua_pushnil(L_);
        while (lua_next(L_, idx) != 0)
        {
            lua_pop(L_, 1);
            ++keys;
        }
        if (keys != n)
            fail("a list must be a sequence 1..n (use rr.null for null entries)");
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        out.reserve(n);
        for (int32_t i = 0; i < n; ++i)
        {
            Level lv(*this, "[" + std::to_string(i + 1) + "]");
            lua_rawgeti(L_, idx, i + 1);
            RR_INTRUSIVE_PTR<MessageElement> el = pack(-1, t2);
            lua_pop(L_, 1);
            el->ElementFlags &= ~MessageElementFlags_ELEMENT_NAME_STR;
            el->ElementFlags |= MessageElementFlags_ELEMENT_NUMBER;
            el->ElementNumber = i;
            out.push_back(el);
        }
        return CreateMessageElementNestedElementList(DataTypes_list_t, "", RR_MOVE(out));
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_map_int32(int idx,
                                                                    const RR_SHARED_PTR<TypeDefinition>& type)
    {
        require_table(idx, "map");
        RR_SHARED_PTR<TypeDefinition> t2 = type->Clone();
        t2->RemoveContainers();
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        lua_pushnil(L_);
        while (lua_next(L_, idx) != 0)
        {
            double k = lua_type(L_, -2) == LUA_TNUMBER ? lua_tonumber(L_, -2) : 0.5;
            if (k != static_cast<double>(static_cast<int32_t>(k)))
            {
                lua_pop(L_, 2);
                fail("keys of an int32 map must be int32 integers");
            }
            int32_t key = static_cast<int32_t>(k);
            Level lv(*this, "[" + std::to_string(key) + "]");
            RR_INTRUSIVE_PTR<MessageElement> el = pack(-1, t2);
            lua_pop(L_, 1);
            el->ElementFlags &= ~MessageElementFlags_ELEMENT_NAME_STR;
            el->ElementFlags |= MessageElementFlags_ELEMENT_NUMBER;
            el->ElementNumber = key;
            out.push_back(el);
        }
        std::sort(out.begin(), out.end(),
                  [](const RR_INTRUSIVE_PTR<MessageElement>& a, const RR_INTRUSIVE_PTR<MessageElement>& b) {
                      return a->ElementNumber < b->ElementNumber;
                  });
        return CreateMessageElementNestedElementList(DataTypes_vector_t, "", RR_MOVE(out));
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_map_string(int idx,
                                                                     const RR_SHARED_PTR<TypeDefinition>& type)
    {
        require_table(idx, "map");
        RR_SHARED_PTR<TypeDefinition> t2 = type->Clone();
        t2->RemoveContainers();
        std::vector<std::pair<std::string, RR_INTRUSIVE_PTR<MessageElement> > > tmp;
        lua_pushnil(L_);
        while (lua_next(L_, idx) != 0)
        {
            if (lua_type(L_, -2) != LUA_TSTRING)
            {
                lua_pop(L_, 2);
                fail("keys of a string map must be strings");
            }
            std::string key = lua_str(L_, -2);
            Level lv(*this, "[\"" + key + "\"]");
            RR_INTRUSIVE_PTR<MessageElement> el = pack(-1, t2);
            lua_pop(L_, 1);
            el->ElementName = key;
            tmp.push_back(std::make_pair(key, el));
        }
        std::sort(tmp.begin(), tmp.end(),
                  [](const std::pair<std::string, RR_INTRUSIVE_PTR<MessageElement> >& a,
                     const std::pair<std::string, RR_INTRUSIVE_PTR<MessageElement> >& b) { return a.first < b.first; });
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        for (auto& p : tmp)
            out.push_back(p.second);
        return CreateMessageElementNestedElementList(DataTypes_dictionary_t, "", RR_MOVE(out));
    }

    // Fill count elements of type t at dst from the rr.array or table at idx.
    void fill_numeric(int idx, DataTypes t, void* dst, size_t count)
    {
        size_t es = RRArrayElementSize(t);
        char* p = static_cast<char*>(dst);
        if (ArrayHeader* a = test_array(L_, idx))
        {
            if (static_cast<size_t>(a->numel) != count)
                fail("expected " + std::to_string(count) + " elements, got " + std::to_string(a->numel));
            if (a->type == static_cast<int32_t>(t))
            {
                std::memcpy(p, array_data(a), es * count);
                return;
            }
            for (size_t k = 0; k < count; ++k)
            {
                push_array_element(L_, a, static_cast<int64_t>(k));
                const char* err = set_element_raw(L_, t, p + es * k, -1);
                lua_pop(L_, 1);
                if (err)
                    fail(std::string("element ") + std::to_string(k + 1) + ": " + err);
            }
            return;
        }
        if (!lua_istable(L_, idx))
            fail(std::string("expected an rr.array or a table of numbers, got ") + luaL_typename(L_, idx));
        if (raw_len(L_, idx) != count)
            fail("expected " + std::to_string(count) + " elements, got " + std::to_string(raw_len(L_, idx)));
        for (size_t k = 0; k < count; ++k)
        {
            lua_rawgeti(L_, idx, static_cast<int>(k + 1));
            const char* err = set_element_raw(L_, t, p + es * k, -1);
            lua_pop(L_, 1);
            if (err)
                fail(std::string("element ") + std::to_string(k + 1) + ": " + err);
        }
    }

    size_t value_count(int idx)
    {
        if (ArrayHeader* a = test_array(L_, idx))
            return static_cast<size_t>(a->numel);
        if (lua_istable(L_, idx))
            return raw_len(L_, idx);
        fail(std::string("expected an rr.array or a table of numbers, got ") + luaL_typename(L_, idx));
        return 0;
    }

    void check_array_length(const RR_SHARED_PTR<TypeDefinition>& type, size_t n)
    {
        if (type->ArrayType != DataTypes_ArrayTypes_array || type->ArrayLength.empty() || type->ArrayLength[0] == 0)
            return;
        size_t declared = static_cast<size_t>(type->ArrayLength[0]);
        if (type->ArrayVarLength ? n > declared : n != declared)
            fail("array length " + std::to_string(n) + " does not match " + type->ToString());
    }

    RR_INTRUSIVE_PTR<RRBaseArray> pack_numeric(int idx, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        DataTypes t = type->Type;
        if (type->ArrayType == DataTypes_ArrayTypes_none)
        {
            RR_INTRUSIVE_PTR<RRBaseArray> arr = AllocateRRArrayByType(t, 1);
            if (ArrayHeader* a = test_array(L_, idx))
            {
                if (a->numel != 1)
                    fail("expected a scalar, got an rr.array of " + std::to_string(a->numel) + " elements");
                fill_numeric(idx, t, arr->void_ptr(), 1);
                return arr;
            }
            const char* err = set_element_raw(L_, t, arr->void_ptr(), idx);
            if (err)
                fail(err);
            return arr;
        }
        size_t n = value_count(idx);
        check_array_length(type, n);
        RR_INTRUSIVE_PTR<RRBaseArray> arr = AllocateRRArrayByType(t, n);
        fill_numeric(idx, t, arr->void_ptr(), n);
        return arr;
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_multidim(int idx, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        std::vector<uint32_t> dims;
        if (ArrayHeader* a = test_array(L_, idx))
            for (int i = 0; i < a->ndims; ++i)
                dims.push_back(static_cast<uint32_t>(array_dims(a)[i]));
        else if (lua_istable(L_, idx))
            dims.push_back(static_cast<uint32_t>(raw_len(L_, idx)));
        else
            fail(std::string("expected an rr.array, got ") + luaL_typename(L_, idx));
        if (!type->ArrayVarLength && !type->ArrayLength.empty())
        {
            bool same = dims.size() == type->ArrayLength.size();
            for (size_t i = 0; same && i < dims.size(); ++i)
                same = dims[i] == static_cast<uint32_t>(type->ArrayLength[i]);
            if (!same)
                fail("array dimensions do not match " + type->ToString());
        }
        size_t n = 1;
        for (uint32_t d : dims)
            n *= d;
        RR_INTRUSIVE_PTR<RRArray<uint32_t> > rdims = AllocateRRArray<uint32_t>(dims.size());
        for (size_t i = 0; i < dims.size(); ++i)
            (*rdims)[i] = dims[i];
        RR_INTRUSIVE_PTR<RRBaseArray> arr = AllocateRRArrayByType(type->Type, n);
        fill_numeric(idx, type->Type, arr->void_ptr(), n);
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > v;
        v.push_back(CreateMessageElement("dims", rdims));
        v.push_back(CreateMessageElement("array", arr));
        return CreateMessageElementNestedElementList(DataTypes_multidimarray_t, "", RR_MOVE(v));
    }

    // Reject table keys that are not fields of def (catches typos).
    void check_fields(int idx, const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        lua_pushnil(L_);
        while (lua_next(L_, idx) != 0)
        {
            lua_pop(L_, 1);
            bool ok = false;
            if (lua_type(L_, -1) == LUA_TSTRING)
            {
                std::string k = lua_str(L_, -1);
                for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
                    if (m->Name == k)
                        ok = true;
                if (!ok)
                {
                    lua_pop(L_, 1);
                    fail("unknown field '" + k + "' for " + def->ResolveQualifiedName());
                }
            }
            if (!ok)
            {
                lua_pop(L_, 1);
                fail("field names of " + def->ResolveQualifiedName() + " must be strings");
            }
        }
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_struct(int idx,
                                                                 const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        std::string typestr = def->ResolveQualifiedName();
        require_table(idx, "struct");
        check_fields(idx, def);
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, typestr);
            Level lv(*this, p->Name);
            lua_pushstring(L_, p->Name.c_str());
            lua_rawget(L_, idx);
            RR_INTRUSIVE_PTR<MessageElement> el = pack(-1, p->Type);
            lua_pop(L_, 1);
            el->ElementName = p->Name;
            out.push_back(el);
        }
        return CreateMessageElementNestedElementList(DataTypes_structure_t, typestr, RR_MOVE(out));
    }

    RR_INTRUSIVE_PTR<RRBaseArray> pack_enum(int idx, const RR_SHARED_PTR<EnumDefinition>& def)
    {
        int32_t v = 0;
        if (lua_type(L_, idx) == LUA_TSTRING)
        {
            std::string name = lua_str(L_, idx);
            bool found = false;
            for (const EnumDefinitionValue& e : def->Values)
                if (e.Name == name)
                {
                    v = e.Value;
                    found = true;
                }
            if (!found)
                fail("'" + name + "' is not a value of enum " + def->ResolveQualifiedName());
        }
        else
        {
            const char* err = set_element_raw(L_, DataTypes_int32_t, &v, idx);
            if (err)
                fail(std::string("enum ") + def->ResolveQualifiedName() + ": " + err);
        }
        return ScalarToRRArray<int32_t>(v);
    }

    // A multidimensional array of pods or namedarrays: a list of tables in column-major order with
    // a `dims` field, for example {dims = {2, 3}, p11, p21, p12, ...}.
    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_composite_multidim(
        int idx, const RR_SHARED_PTR<TypeDefinition>& type, const RR_SHARED_PTR<ServiceEntryDefinition>& def,
        bool pod)
    {
        const char* what = pod ? "pod multidimarray" : "namedarray multidimarray";
        require_table(idx, what);
        lua_pushstring(L_, "dims");
        lua_rawget(L_, idx);
        if (!lua_istable(L_, -1))
            fail(std::string(what) + " needs a dims field (a list of dimensions)");
        int dt = lua_gettop(L_);
        size_t nd = raw_len(L_, dt);
        if (nd == 0)
            fail("dims must not be empty");
        RR_INTRUSIVE_PTR<RRArray<uint32_t> > dims = AllocateRRArray<uint32_t>(nd);
        uint64_t total = 1;
        for (size_t i = 0; i < nd; ++i)
        {
            lua_rawgeti(L_, dt, static_cast<int>(i + 1));
            // lua_tonumber, not lua_tointegerx: compat-5.3's version on LuaJIT truncates 1.5 to 1.
            bool isnum = lua_type(L_, -1) == LUA_TNUMBER;
            double d = isnum ? static_cast<double>(lua_tonumber(L_, -1)) : -1.0;
            lua_pop(L_, 1);
            if (!isnum || !(d >= 0) || d != std::floor(d) || d > static_cast<double>(UINT32_MAX))
                fail("dims entries must be non-negative integers");
            (*dims)[i] = static_cast<uint32_t>(d);
            total *= static_cast<uint64_t>(d);
            if (total > static_cast<uint64_t>(INT32_MAX))
                fail("dims are too large");
        }
        lua_pop(L_, 1);
        if (!type->ArrayLength.empty())
        {
            bool same = type->ArrayLength.size() == nd;
            for (size_t i = 0; same && i < nd; ++i)
                same = static_cast<uint32_t>(type->ArrayLength[i]) == (*dims)[i];
            if (!same)
                fail("dims do not match " + type->ToString());
        }
        if (raw_len(L_, idx) != total)
            fail("expected " + std::to_string(total) + " entries for the given dims, got " +
                 std::to_string(raw_len(L_, idx)));
        RR_SHARED_PTR<TypeDefinition> flat = type->Clone();
        flat->ArrayType = DataTypes_ArrayTypes_array;
        flat->ArrayLength.clear();
        flat->ArrayVarLength = true;
        std::string typestr = def->ResolveQualifiedName();
        RR_INTRUSIVE_PTR<MessageElementNestedElementList> arr =
            pod ? pack_pods(idx, flat, def) : pack_namedarrays(idx, flat, def);
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > v;
        v.push_back(CreateMessageElement("dims", dims));
        v.push_back(CreateMessageElement("array", arr));
        return CreateMessageElementNestedElementList(
            pod ? DataTypes_pod_multidimarray_t : DataTypes_namedarray_multidimarray_t, typestr, RR_MOVE(v));
    }

    // Number of pods/namedarrays given at idx and whether it is a single table.
    size_t composite_count(int idx, const RR_SHARED_PTR<TypeDefinition>& type, const char* what)
    {
        require_table(idx, what);
        if (type->ArrayType == DataTypes_ArrayTypes_none)
            return 1;
        size_t n = raw_len(L_, idx);
        check_array_length(type, n);
        return n;
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_pods(int idx, const RR_SHARED_PTR<TypeDefinition>& type,
                                                               const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        std::string typestr = def->ResolveQualifiedName();
        size_t n = composite_count(idx, type, "pod");
        bool single = type->ArrayType == DataTypes_ArrayTypes_none;
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        for (size_t i = 0; i < n; ++i)
        {
            int item = idx;
            if (!single)
            {
                lua_rawgeti(L_, idx, static_cast<int>(i + 1));
                item = lua_gettop(L_);
            }
            Level lv(*this, single ? std::string() : "[" + std::to_string(i + 1) + "]");
            require_table(item, "pod");
            RR_INTRUSIVE_PTR<MessageElement> el =
                CreateMessageElement("", CreateMessageElementNestedElementList(DataTypes_pod_t, "", pod_fields(item, def)));
            el->ElementFlags &= ~MessageElementFlags_ELEMENT_NAME_STR;
            el->ElementFlags |= MessageElementFlags_ELEMENT_NUMBER;
            el->ElementNumber = static_cast<int32_t>(i);
            out.push_back(el);
            if (!single)
                lua_pop(L_, 1);
        }
        return CreateMessageElementNestedElementList(DataTypes_pod_array_t, typestr, RR_MOVE(out));
    }

    std::vector<RR_INTRUSIVE_PTR<MessageElement> > pod_fields(int idx, const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        check_fields(idx, def);
        std::string typestr = def->ResolveQualifiedName();
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > out;
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, typestr);
            Level lv(*this, p->Name);
            lua_pushstring(L_, p->Name.c_str());
            lua_rawget(L_, idx);
            RR_SHARED_PTR<TypeDefinition> ft = p->Type;
            if (ft->ArrayType == DataTypes_ArrayTypes_multidimarray)
            {
                // Fixed-size multidimensional pod fields travel flattened (column-major).
                RR_SHARED_PTR<TypeDefinition> flat = ft->Clone();
                uint32_t c = product(ft->ArrayLength);
                flat->ArrayLength.clear();
                flat->ArrayLength.push_back(static_cast<int32_t>(c));
                flat->ArrayType = DataTypes_ArrayTypes_array;
                flat->ArrayVarLength = false;
                ft = flat;
            }
            RR_INTRUSIVE_PTR<MessageElement> el = pack(-1, ft);
            lua_pop(L_, 1);
            el->ElementName = p->Name;
            out.push_back(el);
        }
        return out;
    }

    RR_INTRUSIVE_PTR<MessageElementNestedElementList> pack_namedarrays(
        int idx, const RR_SHARED_PTR<TypeDefinition>& type, const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        std::string typestr = def->ResolveQualifiedName();
        boost::tuple<DataTypes, size_t> info =
            GetNamedArrayElementTypeAndCount(def, ctx_.defs, ctx_.node, ctx_.client);
        DataTypes et = info.get<0>();
        size_t per = info.get<1>();
        RR_INTRUSIVE_PTR<RRBaseArray> arr;
        if (ArrayHeader* a = test_array(L_, idx))
        {
            // Flat numeric data, per elements each: no table conversion.
            if (per == 0 || a->numel % per != 0)
                fail("rr.array length is not a multiple of " + std::to_string(per) + " for " + typestr);
            size_t n = static_cast<size_t>(a->numel) / per;
            if (type->ArrayType == DataTypes_ArrayTypes_none && n != 1)
                fail("expected one " + typestr);
            check_array_length(type, n);
            arr = AllocateRRArrayByType(et, n * per);
            fill_numeric(idx, et, arr->void_ptr(), n * per);
        }
        else
        {
            size_t n = composite_count(idx, type, "namedarray");
            bool single = type->ArrayType == DataTypes_ArrayTypes_none;
            arr = AllocateRRArrayByType(et, n * per);
            char* base = static_cast<char*>(arr->void_ptr());
            for (size_t i = 0; i < n; ++i)
            {
                int item = idx;
                if (!single)
                {
                    lua_rawgeti(L_, idx, static_cast<int>(i + 1));
                    item = lua_gettop(L_);
                }
                Level lv(*this, single ? std::string() : "[" + std::to_string(i + 1) + "]");
                size_t off = i * per;
                write_namedarray(item, def, et, base, off);
                if (!single)
                    lua_pop(L_, 1);
            }
        }
        std::vector<RR_INTRUSIVE_PTR<MessageElement> > v;
        v.push_back(CreateMessageElement("array", arr));
        return CreateMessageElementNestedElementList(DataTypes_namedarray_array_t, typestr, RR_MOVE(v));
    }

    void write_namedarray(int idx, const RR_SHARED_PTR<ServiceEntryDefinition>& def, DataTypes et, char* base,
                          size_t& off)
    {
        require_table(idx, "namedarray");
        check_fields(idx, def);
        size_t es = RRArrayElementSize(et);
        std::string typestr = def->ResolveQualifiedName();
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, typestr);
            Level lv(*this, p->Name);
            lua_pushstring(L_, p->Name.c_str());
            lua_rawget(L_, idx);
            int fidx = lua_gettop(L_);
            if (lua_isnil(L_, fidx))
                fail("namedarray fields must not be nil");
            RR_SHARED_PTR<TypeDefinition> ft = p->Type;
            size_t count = ft->ArrayType == DataTypes_ArrayTypes_none ? 1 : product(ft->ArrayLength);
            if (ft->Type == DataTypes_namedtype_t)
            {
                RR_SHARED_PTR<ServiceEntryDefinition> sub =
                    as_entry(ctx_.resolve(ft), DataTypes_namedarray_t, "namedarray");
                if (count == 1 && ft->ArrayType == DataTypes_ArrayTypes_none)
                    write_namedarray(fidx, sub, et, base, off);
                else
                {
                    require_table(fidx, "namedarray list");
                    if (raw_len(L_, fidx) != count)
                        fail("expected " + std::to_string(count) + " entries");
                    for (size_t k = 0; k < count; ++k)
                    {
                        lua_rawgeti(L_, fidx, static_cast<int>(k + 1));
                        write_namedarray(lua_gettop(L_), sub, et, base, off);
                        lua_pop(L_, 1);
                    }
                }
            }
            else if (ft->ArrayType == DataTypes_ArrayTypes_none)
            {
                const char* err = set_element_raw(L_, et, base + es * off, fidx);
                if (err)
                    fail(err);
                off += 1;
            }
            else
            {
                fill_numeric(fidx, et, base + es * off, count);
                off += count;
            }
            lua_pop(L_, 1);
        }
    }
};

// ------------------------------------------------------------------------------------------

class Unpacker
{
  public:
    Unpacker(lua_State* L, const MarshalContext& ctx) : L_(L), ctx_(ctx) {}

    // type may be null: the value is then unpacked from the wire alone (varvalue).
    void unpack(const RR_INTRUSIVE_PTR<MessageElement>& el, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        DepthGuard depth(depth_);
        need(L_, 8);
        if (!el)
            throw DataTypeException("missing message element");
        DataTypes wt = el->ElementType;
        if (wt == DataTypes_void_t)
        {
            lua_pushnil(L_);
            return;
        }
        if (type && type->Type == DataTypes_varvalue_t && type->ContainerType == DataTypes_ContainerTypes_none)
        {
            unpack(el, RR_SHARED_PTR<TypeDefinition>());
            return;
        }
        RR_SHARED_PTR<TypeDefinition> inner;
        if (type && type->ContainerType != DataTypes_ContainerTypes_none)
        {
            inner = type->Clone();
            inner->RemoveContainers();
        }
        else if (!type)
        {
            inner = RR_MAKE_SHARED<TypeDefinition>();
            inner->Type = DataTypes_varvalue_t;
        }

        switch (wt)
        {
        case DataTypes_list_t: {
            RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_list_t);
            lua_createtable(L_, static_cast<int>(l->Elements.size()), 0);
            int t = lua_gettop(L_);
            for (size_t i = 0; i < l->Elements.size(); ++i)
            {
                unpack_or_null(l->Elements[i], inner);
                lua_rawseti(L_, t, static_cast<int>(i + 1));
            }
            return;
        }
        case DataTypes_vector_t: {
            RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_vector_t);
            lua_createtable(L_, 0, static_cast<int>(l->Elements.size()));
            int t = lua_gettop(L_);
            for (const RR_INTRUSIVE_PTR<MessageElement>& e : l->Elements)
            {
                int32_t key;
                if (!MessageElement_GetElementNumber(e, key))
                    throw DataTypeException("int32 map entry without a number");
                lua_pushinteger(L_, key);
                unpack_or_null(e, inner);
                lua_rawset(L_, t);
            }
            return;
        }
        case DataTypes_dictionary_t: {
            RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_dictionary_t);
            lua_createtable(L_, 0, static_cast<int>(l->Elements.size()));
            int t = lua_gettop(L_);
            for (const RR_INTRUSIVE_PTR<MessageElement>& e : l->Elements)
            {
                std::string key = e->ElementName.str().to_string();
                lua_pushlstring(L_, key.data(), key.size());
                unpack_or_null(e, inner);
                lua_rawset(L_, t);
            }
            return;
        }
        case DataTypes_multidimarray_t:
            unpack_multidim(el->CastDataToNestedList(DataTypes_multidimarray_t));
            return;
        case DataTypes_structure_t:
            unpack_struct(el);
            return;
        case DataTypes_pod_array_t:
            unpack_pods(el, type);
            return;
        case DataTypes_namedarray_array_t:
            unpack_namedarrays(el, type);
            return;
        case DataTypes_pod_multidimarray_t:
        case DataTypes_namedarray_multidimarray_t:
            unpack_composite_multidim(el->CastDataToNestedList(wt), wt == DataTypes_pod_multidimarray_t);
            return;
        case DataTypes_string_t: {
            RR_INTRUSIVE_PTR<RRArray<char> > s = el->CastData<RRArray<char> >();
            lua_pushlstring(L_, s ? s->data() : "", s ? s->size() : 0);
            return;
        }
        default:
            break;
        }

        if (IsTypeNumeric(wt))
        {
            RR_INTRUSIVE_PTR<RRBaseArray> a = el->CastData<RRBaseArray>();
            bool scalar = false;
            if (type && type->ContainerType == DataTypes_ContainerTypes_none && type->ArrayType == DataTypes_ArrayTypes_none)
            {
                if (IsTypeNumeric(type->Type))
                    scalar = true;
                else if (type->Type == DataTypes_namedtype_t && ctx_.resolve(type)->RRDataType() == DataTypes_enum_t)
                    scalar = true;
            }
            if (scalar)
            {
                if (!a || a->size() != 1)
                    throw DataTypeException("expected a scalar");
                push_element_raw(L_, a->GetTypeID(), a->void_ptr());
                return;
            }
            push_rrarray(a, type);
            return;
        }
        throw DataTypeException("cannot unpack element type " + std::to_string(static_cast<int>(wt)));
    }

  private:
    lua_State* L_;
    const MarshalContext& ctx_;
    int depth_ = 0;

    // Dims and element type that push_array accepts, checked here so that received data is
    // rejected with a C++ exception (push_array raises a Lua error, which must not happen with
    // C++ objects live), and without signed overflow. Returns the element count.
    static int64_t checked_numel(const std::vector<int64_t>& dims, DataTypes t)
    {
        try
        {
            return array_numel_checked(t, dims.data(), static_cast<int>(dims.size()));
        }
        catch (const std::invalid_argument& e)
        {
            throw DataTypeException(e.what());
        }
    }

    void unpack_or_null(const RR_INTRUSIVE_PTR<MessageElement>& e, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        unpack(e, type);
        if (lua_isnil(L_, -1))
        {
            lua_pop(L_, 1);
            push_null(L_);
        }
    }

    // A 1-D array, or with dims from a fixed multidim declaration (flattened pod fields).
    void push_rrarray(const RR_INTRUSIVE_PTR<RRBaseArray>& a, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        DataTypes t = a ? a->GetTypeID() : DataTypes_double_t;
        int64_t n = a ? static_cast<int64_t>(a->size()) : 0;
        std::vector<int64_t> dims(1, n);
        if (type && type->ArrayType == DataTypes_ArrayTypes_multidimarray && !type->ArrayLength.empty() &&
            product(type->ArrayLength) == static_cast<uint32_t>(n))
        {
            dims.clear();
            for (int32_t d : type->ArrayLength)
                dims.push_back(d);
        }
        checked_numel(dims, t);
        ArrayHeader* h = push_array(L_, t, dims.data(), static_cast<int>(dims.size()));
        if (n)
            std::memcpy(array_data(h), a->void_ptr(), array_elem_size(t) * static_cast<size_t>(n));
    }

    void unpack_multidim(const RR_INTRUSIVE_PTR<MessageElementNestedElementList>& l)
    {
        RR_INTRUSIVE_PTR<MessageElement> de = MessageElement::FindElement(l->Elements, "dims");
        RR_INTRUSIVE_PTR<MessageElement> ae = MessageElement::FindElement(l->Elements, "array");
        RR_INTRUSIVE_PTR<RRBaseArray> d = de->CastData<RRBaseArray>();
        RR_INTRUSIVE_PTR<RRBaseArray> a = ae->CastData<RRBaseArray>();
        std::vector<int64_t> dims;
        for (size_t i = 0; i < d->size(); ++i)
        {
            if (d->GetTypeID() == DataTypes_uint32_t)
                dims.push_back(static_cast<int64_t>(static_cast<uint32_t*>(d->void_ptr())[i]));
            else if (d->GetTypeID() == DataTypes_int32_t)
                dims.push_back(static_cast<int64_t>(static_cast<int32_t*>(d->void_ptr())[i]));
            else
                throw DataTypeException("invalid multidimarray dims");
        }
        if (dims.empty())
            dims.push_back(static_cast<int64_t>(a->size()));
        int64_t n = checked_numel(dims, a->GetTypeID());
        if (n != static_cast<int64_t>(a->size()))
            throw DataTypeException("multidimarray dims do not match its data");
        ArrayHeader* h = push_array(L_, a->GetTypeID(), dims.data(), static_cast<int>(dims.size()));
        if (n)
            std::memcpy(array_data(h), a->void_ptr(), array_elem_size(a->GetTypeID()) * static_cast<size_t>(n));
    }

    RR_SHARED_PTR<ServiceEntryDefinition> entry_named(const RR_INTRUSIVE_PTR<MessageElement>& el, DataTypes kind,
                                                      const char* what)
    {
        std::string name = el->ElementTypeName.str().to_string();
        if (name.empty())
        {
            RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList();
            name = l->TypeName.str().to_string();
        }
        return as_entry(ctx_.resolve_name(name), kind, what);
    }

    void unpack_struct(const RR_INTRUSIVE_PTR<MessageElement>& el)
    {
        RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_structure_t);
        RR_SHARED_PTR<ServiceEntryDefinition> def = entry_named(el, DataTypes_structure_t, "struct");
        lua_createtable(L_, 0, static_cast<int>(def->Members.size()));
        int t = lua_gettop(L_);
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, def->ResolveQualifiedName());
            RR_INTRUSIVE_PTR<MessageElement> fe;
            if (!MessageElement::TryFindElement(l->Elements, p->Name, fe))
                continue;
            unpack(fe, p->Type);
            if (lua_isnil(L_, -1))
            {
                lua_pop(L_, 1);
                continue; // null fields are absent
            }
            lua_pushstring(L_, p->Name.c_str());
            lua_insert(L_, -2);
            lua_rawset(L_, t);
        }
    }

    bool declared_single(const RR_SHARED_PTR<TypeDefinition>& type)
    {
        return type && type->Type == DataTypes_namedtype_t && type->ContainerType == DataTypes_ContainerTypes_none &&
               type->ArrayType == DataTypes_ArrayTypes_none;
    }

    void unpack_composite_multidim(const RR_INTRUSIVE_PTR<MessageElementNestedElementList>& l, bool pod)
    {
        RR_INTRUSIVE_PTR<MessageElement> de = MessageElement::FindElement(l->Elements, "dims");
        RR_INTRUSIVE_PTR<MessageElement> ae = MessageElement::FindElement(l->Elements, "array");
        RR_INTRUSIVE_PTR<RRArray<uint32_t> > d = de->CastData<RRArray<uint32_t> >();
        if (!d || d->size() == 0)
            throw DataTypeException("invalid multidimarray dims");
        uint64_t total = 1;
        for (size_t i = 0; i < d->size(); ++i)
            total *= (*d)[i];
        if (pod)
            unpack_pods(ae, RR_SHARED_PTR<TypeDefinition>());
        else
            unpack_namedarrays(ae, RR_SHARED_PTR<TypeDefinition>());
        if (raw_len(L_, -1) != total)
            throw DataTypeException("multidimarray dims do not match its data");
        set_dims_field(d->data(), d->size());
    }

    // Set t.dims = {d1, d2, ...} on the table at the top of the stack.
    void set_dims_field(const uint32_t* d, size_t nd)
    {
        need(L_, 4);
        lua_pushstring(L_, "dims");
        lua_createtable(L_, static_cast<int>(nd), 0);
        for (size_t i = 0; i < nd; ++i)
        {
            lua_pushinteger(L_, static_cast<lua_Integer>(d[i]));
            lua_rawseti(L_, -2, static_cast<int>(i + 1));
        }
        lua_rawset(L_, -3);
    }

    // Fixed multidim pod or namedarray fields of a pod travel flattened; give them their dims.
    void add_declared_dims(const RR_SHARED_PTR<TypeDefinition>& type, size_t n)
    {
        if (!type || type->ArrayType != DataTypes_ArrayTypes_multidimarray || type->ArrayLength.empty() ||
            product(type->ArrayLength) != n)
            return;
        std::vector<uint32_t> d(type->ArrayLength.begin(), type->ArrayLength.end());
        set_dims_field(d.data(), d.size());
    }

    void unpack_pods(const RR_INTRUSIVE_PTR<MessageElement>& el, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_pod_array_t);
        RR_SHARED_PTR<ServiceEntryDefinition> def = entry_named(el, DataTypes_pod_t, "pod");
        if (declared_single(type))
        {
            if (l->Elements.size() != 1)
                throw DataTypeException("expected one pod");
            unpack_pod(l->Elements[0], def);
            return;
        }
        lua_createtable(L_, static_cast<int>(l->Elements.size()), 0);
        int t = lua_gettop(L_);
        for (size_t i = 0; i < l->Elements.size(); ++i)
        {
            unpack_pod(l->Elements[i], def);
            lua_rawseti(L_, t, static_cast<int>(i + 1));
        }
        add_declared_dims(type, l->Elements.size());
    }

    void unpack_pod(const RR_INTRUSIVE_PTR<MessageElement>& el, const RR_SHARED_PTR<ServiceEntryDefinition>& def)
    {
        need(L_, 8);
        RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_pod_t);
        lua_createtable(L_, 0, static_cast<int>(def->Members.size()));
        int t = lua_gettop(L_);
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, def->ResolveQualifiedName());
            RR_INTRUSIVE_PTR<MessageElement> fe = MessageElement::FindElement(l->Elements, p->Name);
            unpack(fe, p->Type);
            lua_pushstring(L_, p->Name.c_str());
            lua_insert(L_, -2);
            lua_rawset(L_, t);
        }
    }

    void unpack_namedarrays(const RR_INTRUSIVE_PTR<MessageElement>& el, const RR_SHARED_PTR<TypeDefinition>& type)
    {
        RR_INTRUSIVE_PTR<MessageElementNestedElementList> l = el->CastDataToNestedList(DataTypes_namedarray_array_t);
        RR_SHARED_PTR<ServiceEntryDefinition> def = entry_named(el, DataTypes_namedarray_t, "namedarray");
        boost::tuple<DataTypes, size_t> info =
            GetNamedArrayElementTypeAndCount(def, ctx_.defs, ctx_.node, ctx_.client);
        RR_INTRUSIVE_PTR<RRBaseArray> a = MessageElement::FindElement(l->Elements, "array")->CastData<RRBaseArray>();
        size_t per = info.get<1>();
        if (!a || a->GetTypeID() != info.get<0>() || per == 0 || a->size() % per != 0)
            throw DataTypeException("invalid namedarray data for " + def->ResolveQualifiedName());
        size_t n = a->size() / per;
        const char* base = static_cast<const char*>(a->void_ptr());
        if (declared_single(type))
        {
            if (n != 1)
                throw DataTypeException("expected one namedarray");
            size_t off = 0;
            read_namedarray(def, info.get<0>(), base, off);
            return;
        }
        lua_createtable(L_, static_cast<int>(n), 0);
        int t = lua_gettop(L_);
        for (size_t i = 0; i < n; ++i)
        {
            size_t off = i * per;
            read_namedarray(def, info.get<0>(), base, off);
            lua_rawseti(L_, t, static_cast<int>(i + 1));
        }
        add_declared_dims(type, n);
    }

    void read_namedarray(const RR_SHARED_PTR<ServiceEntryDefinition>& def, DataTypes et, const char* base, size_t& off)
    {
        need(L_, 8);
        size_t es = RRArrayElementSize(et);
        lua_createtable(L_, 0, static_cast<int>(def->Members.size()));
        int t = lua_gettop(L_);
        for (const RR_SHARED_PTR<MemberDefinition>& m : def->Members)
        {
            RR_SHARED_PTR<PropertyDefinition> p = field_def(m, def->ResolveQualifiedName());
            RR_SHARED_PTR<TypeDefinition> ft = p->Type;
            lua_pushstring(L_, p->Name.c_str());
            size_t count = ft->ArrayType == DataTypes_ArrayTypes_none ? 1 : product(ft->ArrayLength);
            if (ft->Type == DataTypes_namedtype_t)
            {
                RR_SHARED_PTR<ServiceEntryDefinition> sub =
                    as_entry(ctx_.resolve(ft), DataTypes_namedarray_t, "namedarray");
                if (ft->ArrayType == DataTypes_ArrayTypes_none)
                    read_namedarray(sub, et, base, off);
                else
                {
                    lua_createtable(L_, static_cast<int>(count), 0);
                    for (size_t k = 0; k < count; ++k)
                    {
                        read_namedarray(sub, et, base, off);
                        lua_rawseti(L_, -2, static_cast<int>(k + 1));
                    }
                }
            }
            else if (ft->ArrayType == DataTypes_ArrayTypes_none)
            {
                push_element_raw(L_, et, base + es * off);
                off += 1;
            }
            else
            {
                int64_t c = static_cast<int64_t>(count);
                array_numel_checked(et, &c, 1);
                ArrayHeader* h = push_array(L_, et, &c, 1);
                std::memcpy(array_data(h), base + es * off, es * count);
                off += count;
            }
            lua_rawset(L_, t);
        }
    }
};

} // namespace

// ------------------------------------------------------------------------------------------

RR_SHARED_PTR<NamedTypeDefinition> MarshalContext::resolve(const RR_SHARED_PTR<TypeDefinition>& type) const
{
    return type->ResolveNamedType(defs, node, client);
}

RR_SHARED_PTR<NamedTypeDefinition> MarshalContext::resolve_name(const std::string& qualified) const
{
    if (qualified.empty())
        throw DataTypeException("named type without a type name");
    RR_SHARED_PTR<TypeDefinition> t = RR_MAKE_SHARED<TypeDefinition>();
    t->Type = DataTypes_namedtype_t;
    t->TypeString = qualified;
    return t->ResolveNamedType(defs, node, client);
}

RR_INTRUSIVE_PTR<MessageElement> pack_element(lua_State* L, int idx, const RR_SHARED_PTR<TypeDefinition>& type,
                                              const MarshalContext& ctx)
{
    Packer p(L, ctx);
    return p.pack(idx, type);
}

void unpack_element(lua_State* L, const RR_INTRUSIVE_PTR<MessageElement>& element,
                    const RR_SHARED_PTR<TypeDefinition>& type, const MarshalContext& ctx)
{
    int top = lua_gettop(L);
    Unpacker u(L, ctx);
    try
    {
        u.unpack(element, type);
    }
    catch (...)
    {
        lua_settop(L, top);
        throw;
    }
}

namespace
{
RR_SHARED_PTR<TypeDefinition> attribute_type()
{
    RR_SHARED_PTR<TypeDefinition> t = RR_MAKE_SHARED<TypeDefinition>();
    t->Name = "value";
    t->Type = DataTypes_varvalue_t;
    return t;
}
} // namespace

std::map<std::string, RR_INTRUSIVE_PTR<RRValue> > pack_attributes(lua_State* L, int idx)
{
    if (idx < 0 && idx > LUA_REGISTRYINDEX)
        idx = lua_gettop(L) + idx + 1;
    if (lua_type(L, idx) != LUA_TTABLE)
        throw DataTypeException("service attributes must be a table {name = value}");
    if (!lua_checkstack(L, 4))
        throw DataTypeException("Lua stack overflow");
    MarshalContext ctx;
    RR_SHARED_PTR<TypeDefinition> type = attribute_type();
    std::map<std::string, RR_INTRUSIVE_PTR<RRValue> > out;
    int top = lua_gettop(L);
    try
    {
        lua_pushnil(L);
        while (lua_next(L, idx) != 0)
        {
            if (lua_type(L, -2) != LUA_TSTRING)
                throw DataTypeException("service attribute names must be strings");
            std::string name = lua_tostring(L, -2);
            RR_INTRUSIVE_PTR<MessageElement> el;
            try
            {
                el = pack_element(L, lua_gettop(L), type, ctx);
            }
            catch (std::exception& e)
            {
                throw DataTypeException("service attribute '" + name + "': " + e.what());
            }
            RR_INTRUSIVE_PTR<RRBaseArray> a = boost::dynamic_pointer_cast<RRBaseArray>(el->GetData());
            if (!a)
                throw DataTypeException("service attribute '" + name +
                                        "' must be a string or a numeric array (RR attributes may not use "
                                        "structures, lists or maps)");
            out[name] = a;
            lua_pop(L, 1);
        }
    }
    catch (...)
    {
        lua_settop(L, top);
        throw;
    }
    return out;
}

void push_attributes(lua_State* L, const std::map<std::string, RR_INTRUSIVE_PTR<RRValue> >& attrs)
{
    if (!lua_checkstack(L, 4))
        throw DataTypeException("Lua stack overflow");
    MarshalContext ctx;
    RR_SHARED_PTR<TypeDefinition> type = attribute_type();
    lua_createtable(L, 0, static_cast<int>(attrs.size()));
    int t = lua_gettop(L);
    try
    {
        for (std::map<std::string, RR_INTRUSIVE_PTR<RRValue> >::const_iterator it = attrs.begin(); it != attrs.end();
             ++it)
        {
            RR_INTRUSIVE_PTR<RRBaseArray> a = boost::dynamic_pointer_cast<RRBaseArray>(it->second);
            if (!a)
                continue;
            lua_pushlstring(L, it->first.data(), it->first.size());
            unpack_element(L, CreateMessageElement("value", a), type, ctx);
            lua_rawset(L, t);
        }
    }
    catch (...)
    {
        lua_settop(L, t - 1);
        throw;
    }
}

void push_null(lua_State* L)
{
    lua_pushlightuserdata(L, const_cast<char*>(&kNullKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (!lua_isnil(L, -1))
        return;
    lua_pop(L, 1);
    lua_newuserdata(L, 1);
    if (luaL_newmetatable(L, kNullMeta))
    {
        lua_pushliteral(L, "rr.null");
        lua_pushcclosure(
            L, [](lua_State* L2) -> int {
                lua_pushvalue(L2, lua_upvalueindex(1));
                return 1;
            },
            1);
        lua_setfield(L, -2, "__tostring");
        lua_pushliteral(L, "rr.null");
        lua_setfield(L, -2, "__name");
    }
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, const_cast<char*>(&kNullKey));
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

bool is_null(lua_State* L, int idx)
{
    if (lua_type(L, idx) != LUA_TUSERDATA)
        return false;
    idx = abs_index(L, idx);
    lua_pushlightuserdata(L, const_cast<char*>(&kNullKey));
    lua_rawget(L, LUA_REGISTRYINDEX);
    bool r = lua_rawequal(L, idx, -1) != 0;
    lua_pop(L, 1);
    return r;
}

} // namespace rl

// rr.varvalue(type, value)
int rl_varvalue_new(lua_State* L)
{
    luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    lua_settop(L, 2);
    lua_createtable(L, 0, 2);
    lua_pushliteral(L, "type");
    lua_pushvalue(L, 1);
    lua_rawset(L, -3);
    lua_pushliteral(L, "value");
    lua_pushvalue(L, 2);
    lua_rawset(L, -3);
    if (luaL_newmetatable(L, rl::kVarValueMeta))
    {
        lua_pushliteral(L, "rr.varvalue");
        lua_setfield(L, -2, "__name");
    }
    lua_setmetatable(L, -2);
    return 1;
}

int rl_test_roundtrip(lua_State* L)
{
    return rl::guard(L, [L]() -> int {
        if (!lua_istable(L, 1) || lua_type(L, 2) != LUA_TSTRING)
            throw std::invalid_argument("_roundtrip(defs, type, value)");
        rl::MarshalContext ctx;
        for (int i = 1;; ++i)
        {
            lua_rawgeti(L, 1, i);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                break;
            }
            if (lua_type(L, -1) != LUA_TSTRING)
            {
                lua_pop(L, 1);
                throw std::invalid_argument("defs must be strings");
            }
            std::string text = rl::lua_str(L, -1);
            lua_pop(L, 1);
            RR_SHARED_PTR<ServiceDefinition> d = RR_MAKE_SHARED<ServiceDefinition>();
            d->FromString(text);
            ctx.defs.push_back(d);
        }
        std::string ts = rl::lua_str(L, 2);
        RR_SHARED_PTR<TypeDefinition> type = RR_MAKE_SHARED<TypeDefinition>();
        type->FromString(ts + " value");
        // An unqualified type name refers to the first definition (test convenience).
        if (!ctx.defs.empty() && type->Type == DataTypes_namedtype_t &&
            type->TypeString.find('.') == std::string::npos)
            type->TypeString = ctx.defs[0]->Name + "." + type->TypeString;
        lua_settop(L, 3);
        RR_INTRUSIVE_PTR<MessageElement> el = rl::pack_element(L, 3, type, ctx);

        // Through the real wire format: write a message and read it back.
        RR_INTRUSIVE_PTR<Message> m = CreateMessage();
        m->header = CreateMessageHeader();
        RR_INTRUSIVE_PTR<MessageEntry> e = CreateMessageEntry(MessageEntryType_FunctionCallReq, "x");
        e->AddElement(el);
        m->entries.push_back(e);
        size_t size = m->ComputeSize();
        boost::shared_array<uint8_t> buf(new uint8_t[size]);
        ArrayBinaryWriter w(buf.get(), 0, size);
        m->Write(w);
        ArrayBinaryReader r(buf.get(), 0, size);
        RR_INTRUSIVE_PTR<Message> m2 = CreateMessage();
        m2->Read(r);
        rl::unpack_element(L, m2->entries.at(0)->elements.at(0), type, ctx);
        return 1;
    });
}
