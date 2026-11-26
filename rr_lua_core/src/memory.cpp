// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/memory.hpp"

#include "rrlua/array.hpp"
#include "rrlua/lua.hpp"

#include <boost/thread/mutex.hpp>

#include <cstring>
#include <stdexcept>

using namespace RobotRaconteur;

namespace rl
{
namespace
{
RR_INTRUSIVE_PTR<RRArray<uint32_t> > dims32(const std::vector<uint64_t>& d)
{
    RR_INTRUSIVE_PTR<RRArray<uint32_t> > a = AllocateRRArray<uint32_t>(d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (d[i] > 0xFFFFFFFFull)
            throw InvalidArgumentException("dimension too large");
        (*a)[i] = static_cast<uint32_t>(d[i]);
    }
    return a;
}

uint64_t product(const std::vector<uint64_t>& d)
{
    uint64_t n = 1;
    for (uint64_t x : d)
        n *= x;
    return n;
}

template <typename T>
class ArrayAccess : public MemoryAccess
{
  public:
    explicit ArrayAccess(RR_SHARED_PTR<ArrayMemory<T> > m) : m_(std::move(m)) {}
    bool multidim() const override { return false; }
    DataTypes type() const override { return RRPrimUtil<T>::GetTypeID(); }
    uint64_t length() override { return m_->Length(); }
    std::vector<uint64_t> dims() override { return std::vector<uint64_t>(1, m_->Length()); }
    void read(uint64_t pos, void* dst, uint64_t count) override
    {
        RR_INTRUSIVE_PTR<RRArray<T> > buf = AttachRRArray<T>(static_cast<T*>(dst), count, false);
        m_->Read(pos, buf, 0, count);
    }
    void write(uint64_t pos, const void* src, uint64_t count) override
    {
        RR_INTRUSIVE_PTR<RRArray<T> > buf =
            AttachRRArray<T>(static_cast<T*>(const_cast<void*>(src)), count, false);
        m_->Write(pos, buf, 0, count);
    }
    void read_md(const std::vector<uint64_t>&, void*, const std::vector<uint64_t>&) override
    {
        throw InvalidOperationException("not a multidimensional memory");
    }
    void write_md(const std::vector<uint64_t>&, const void*, const std::vector<uint64_t>&) override
    {
        throw InvalidOperationException("not a multidimensional memory");
    }
    RR_INTRUSIVE_PTR<MessageEntry> serve(const RR_INTRUSIVE_PTR<MessageEntry>& m, const RR_SHARED_PTR<Endpoint>& e,
                                         const RR_SHARED_PTR<ServiceSkel>& skel, const std::string& name,
                                         MemberDefinition_Direction direction) override
    {
        RR_SHARED_PTR<ArrayMemoryServiceSkel<T> > h;
        {
            boost::mutex::scoped_lock lock(lock_);
            if (!helper_ || helper_skel_.lock() != skel)
            {
                helper_ = RR_MAKE_SHARED<ArrayMemoryServiceSkel<T> >(name, skel, direction);
                helper_skel_ = skel;
            }
            h = helper_;
        }
        return h->CallMemoryFunction(m, e, m_);
    }

  private:
    RR_SHARED_PTR<ArrayMemory<T> > m_;
    boost::mutex lock_;
    RR_SHARED_PTR<ArrayMemoryServiceSkel<T> > helper_;
    RR_WEAK_PTR<ServiceSkel> helper_skel_;
};

template <typename T>
class MultiDimAccess : public MemoryAccess
{
  public:
    explicit MultiDimAccess(RR_SHARED_PTR<MultiDimArrayMemory<T> > m) : m_(std::move(m)) {}
    bool multidim() const override { return true; }
    DataTypes type() const override { return RRPrimUtil<T>::GetTypeID(); }
    uint64_t length() override { return product(m_->Dimensions()); }
    std::vector<uint64_t> dims() override { return m_->Dimensions(); }
    void read(uint64_t, void*, uint64_t) override
    {
        throw InvalidOperationException("a multidimensional memory takes position and count lists");
    }
    void write(uint64_t, const void*, uint64_t) override
    {
        throw InvalidOperationException("a multidimensional memory takes a position list");
    }
    void read_md(const std::vector<uint64_t>& pos, void* dst, const std::vector<uint64_t>& count) override
    {
        RR_INTRUSIVE_PTR<RRMultiDimArray<T> > buf = AllocateRRMultiDimArray<T>(
            dims32(count), AttachRRArray<T>(static_cast<T*>(dst), product(count), false));
        m_->Read(pos, buf, std::vector<uint64_t>(count.size(), 0), count);
    }
    void write_md(const std::vector<uint64_t>& pos, const void* src, const std::vector<uint64_t>& count) override
    {
        RR_INTRUSIVE_PTR<RRMultiDimArray<T> > buf = AllocateRRMultiDimArray<T>(
            dims32(count), AttachRRArray<T>(static_cast<T*>(const_cast<void*>(src)), product(count), false));
        m_->Write(pos, buf, std::vector<uint64_t>(count.size(), 0), count);
    }
    RR_INTRUSIVE_PTR<MessageEntry> serve(const RR_INTRUSIVE_PTR<MessageEntry>& m, const RR_SHARED_PTR<Endpoint>& e,
                                         const RR_SHARED_PTR<ServiceSkel>& skel, const std::string& name,
                                         MemberDefinition_Direction direction) override
    {
        RR_SHARED_PTR<MultiDimArrayMemoryServiceSkel<T> > h;
        {
            boost::mutex::scoped_lock lock(lock_);
            if (!helper_ || helper_skel_.lock() != skel)
            {
                helper_ = RR_MAKE_SHARED<MultiDimArrayMemoryServiceSkel<T> >(name, skel, direction);
                helper_skel_ = skel;
            }
            h = helper_;
        }
        return h->CallMemoryFunction(m, e, m_);
    }

  private:
    RR_SHARED_PTR<MultiDimArrayMemory<T> > m_;
    boost::mutex lock_;
    RR_SHARED_PTR<MultiDimArrayMemoryServiceSkel<T> > helper_;
    RR_WEAK_PTR<ServiceSkel> helper_skel_;
};

// Call F::template run<T>() for the C++ type of t.
template <typename F>
std::shared_ptr<MemoryAccess> dispatch(DataTypes t, F&& f)
{
    switch (t)
    {
    case DataTypes_double_t:
        return f.template run<double>();
    case DataTypes_single_t:
        return f.template run<float>();
    case DataTypes_int8_t:
        return f.template run<int8_t>();
    case DataTypes_uint8_t:
        return f.template run<uint8_t>();
    case DataTypes_int16_t:
        return f.template run<int16_t>();
    case DataTypes_uint16_t:
        return f.template run<uint16_t>();
    case DataTypes_int32_t:
        return f.template run<int32_t>();
    case DataTypes_uint32_t:
        return f.template run<uint32_t>();
    case DataTypes_int64_t:
        return f.template run<int64_t>();
    case DataTypes_uint64_t:
        return f.template run<uint64_t>();
    case DataTypes_cdouble_t:
        return f.template run<cdouble>();
    case DataTypes_csingle_t:
        return f.template run<cfloat>();
    case DataTypes_bool_t:
        return f.template run<rr_bool>();
    default:
        throw InvalidOperationException("memories of this element type are not supported yet (numeric only)");
    }
}

struct MakeClient
{
    std::string name;
    RR_SHARED_PTR<ServiceStub> stub;
    MemberDefinition_Direction dir;
    bool md;
    template <typename T>
    std::shared_ptr<MemoryAccess> run()
    {
        if (md)
            return std::make_shared<MultiDimAccess<T> >(RR_MAKE_SHARED<MultiDimArrayMemoryClient<T> >(name, stub, dir));
        return std::make_shared<ArrayAccess<T> >(RR_MAKE_SHARED<ArrayMemoryClient<T> >(name, stub, dir));
    }
};

struct MakeLocal
{
    const ArrayHeader* a;
    bool md;
    template <typename T>
    std::shared_ptr<MemoryAccess> run()
    {
        size_t n = static_cast<size_t>(a->numel);
        RR_INTRUSIVE_PTR<RRArray<T> > data = AllocateRRArray<T>(n);
        if (n)
            std::memcpy(data->data(), array_data(a), sizeof(T) * n);
        if (md)
        {
            std::vector<uint64_t> d(array_dims(a), array_dims(a) + a->ndims);
            return std::make_shared<MultiDimAccess<T> >(
                RR_MAKE_SHARED<MultiDimArrayMemory<T> >(AllocateRRMultiDimArray<T>(dims32(d), data)));
        }
        return std::make_shared<ArrayAccess<T> >(RR_MAKE_SHARED<ArrayMemory<T> >(data));
    }
};

DataTypes element_type(const RR_SHARED_PTR<MemoryDefinition>& def)
{
    if (!IsTypeNumeric(def->Type->Type) && def->Type->Type != DataTypes_bool_t &&
        def->Type->Type != DataTypes_cdouble_t && def->Type->Type != DataTypes_csingle_t)
        throw InvalidOperationException("memory '" + def->Name +
                                        "': pod and namedarray memories are not supported yet");
    return def->Type->Type;
}

bool is_md(const RR_SHARED_PTR<MemoryDefinition>& def)
{
    return def->Type->ArrayType == DataTypes_ArrayTypes_multidimarray;
}

// A 1-based position (number) or list of positions (table) as 0-based.
std::vector<uint64_t> positions(lua_State* L, int idx, size_t n, const char* what)
{
    std::vector<uint64_t> out;
    auto one = [&](int i) {
        if (lua_type(L, i) != LUA_TNUMBER)
            throw InvalidArgumentException(std::string(what) + " must be integers");
        double d = lua_tonumber(L, i);
        if (!(d >= 1) || d != static_cast<double>(static_cast<uint64_t>(d)))
            throw InvalidArgumentException(std::string(what) + " must be integers >= 1");
        out.push_back(static_cast<uint64_t>(d) - 1);
    };
    if (lua_type(L, idx) == LUA_TTABLE)
    {
        size_t len = lua_rawlen(L, idx);
        if (len != n)
            throw InvalidArgumentException(std::string(what) + ": expected " + std::to_string(n) + " entries");
        for (size_t i = 1; i <= len; ++i)
        {
            lua_rawgeti(L, idx, static_cast<int>(i));
            try
            {
                one(lua_gettop(L));
            }
            catch (...)
            {
                lua_pop(L, 1);
                throw;
            }
            lua_pop(L, 1);
        }
    }
    else
    {
        if (n != 1)
            throw InvalidArgumentException(std::string(what) + ": expected a list of " + std::to_string(n));
        one(idx);
    }
    return out;
}
} // namespace

std::shared_ptr<MemoryAccess> make_client_memory(const RR_SHARED_PTR<MemoryDefinition>& def,
                                                 const RR_SHARED_PTR<ServiceStub>& stub)
{
    MakeClient f{def->Name, stub, def->Direction(), is_md(def)};
    return dispatch(element_type(def), f);
}

std::shared_ptr<MemoryAccess> make_local_memory(lua_State* L, int idx, const RR_SHARED_PTR<MemoryDefinition>& def)
{
    DataTypes t = element_type(def);
    ArrayHeader* a = test_array(L, idx);
    if (!a)
        throw InvalidArgumentException("memory '" + def->Name + "': the initial contents must be an rr.array");
    if (static_cast<DataTypes>(a->type) != t)
        throw InvalidArgumentException("memory '" + def->Name + "': expected an rr.array of " +
                                       array_type_name(t) + ", got " + array_type_name(static_cast<DataTypes>(a->type)));
    MakeLocal f{a, is_md(def)};
    return dispatch(t, f);
}

// ------------------------------------------------------------------------------------------
// Memory (Lua handle)

Memory::Memory(std::shared_ptr<MemoryAccess> m) : m_(std::move(m)) {}

std::string Memory::type() { return array_type_name(m_->type()); }

double Memory::length() { return static_cast<double>(m_->length()); }

rl::Ret Memory::dims(lua_State* L)
{
    std::vector<uint64_t> d = m_->dims();
    lua_createtable(L, static_cast<int>(d.size()), 0);
    for (size_t i = 0; i < d.size(); ++i)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(d[i]));
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return {1};
}

rl::Ret Memory::read(lua_State* L, rl::Arg pos)
{
    if (m_->multidim())
        throw InvalidArgumentException("read({pos...}, {count...}) for a multidimensional memory");
    uint64_t p = positions(L, pos.idx, 1, "read position")[0];
    uint64_t len = m_->length();
    uint64_t count = p < len ? len - p : 0;
    int64_t n = static_cast<int64_t>(count);
    ArrayHeader* a = push_array(L, m_->type(), &n, 1);
    if (count)
        m_->read(p, array_data(a), count);
    return {1};
}

rl::Ret Memory::read(lua_State* L, rl::Arg pos, rl::Arg count)
{
    if (!m_->multidim())
    {
        uint64_t p = positions(L, pos.idx, 1, "read position")[0];
        if (lua_type(L, count.idx) != LUA_TNUMBER || !(lua_tonumber(L, count.idx) >= 0))
            throw InvalidArgumentException("read count must be a non-negative integer");
        int64_t n = static_cast<int64_t>(lua_tonumber(L, count.idx));
        ArrayHeader* a = push_array(L, m_->type(), &n, 1);
        if (n)
            m_->read(p, array_data(a), static_cast<uint64_t>(n));
        return {1};
    }
    std::vector<uint64_t> d = m_->dims();
    std::vector<uint64_t> p = positions(L, pos.idx, d.size(), "read position");
    std::vector<uint64_t> c;
    if (lua_type(L, count.idx) != LUA_TTABLE || lua_rawlen(L, count.idx) != d.size())
        throw InvalidArgumentException("read count must be a list of " + std::to_string(d.size()) + " counts");
    std::vector<int64_t> cd;
    for (size_t i = 1; i <= d.size(); ++i)
    {
        lua_rawgeti(L, count.idx, static_cast<int>(i));
        double x = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (!(x >= 0))
            throw InvalidArgumentException("read counts must be non-negative integers");
        c.push_back(static_cast<uint64_t>(x));
        cd.push_back(static_cast<int64_t>(x));
    }
    ArrayHeader* a = push_array(L, m_->type(), cd.data(), static_cast<int>(cd.size()));
    if (product(c))
        m_->read_md(p, array_data(a), c);
    return {1};
}

void Memory::write(rl::Arg pos, rl::Arg data)
{
    lua_State* L = pos.L;
    ArrayHeader* a = test_array(L, data.idx);
    if (!m_->multidim())
    {
        uint64_t p = positions(L, pos.idx, 1, "write position")[0];
        if (a)
        {
            if (static_cast<DataTypes>(a->type) != m_->type())
                throw InvalidArgumentException(std::string("write: expected an rr.array of ") +
                                               array_type_name(m_->type()));
            if (a->numel)
                m_->write(p, array_data(a), static_cast<uint64_t>(a->numel));
            return;
        }
        if (lua_type(L, data.idx) != LUA_TTABLE)
            throw InvalidArgumentException("write: data must be an rr.array or a table of numbers");
        size_t n = lua_rawlen(L, data.idx);
        size_t es = array_elem_size(m_->type());
        std::vector<char> buf(n * es);
        for (size_t i = 0; i < n; ++i)
        {
            lua_rawgeti(L, data.idx, static_cast<int>(i + 1));
            const char* err = set_element_raw(L, m_->type(), buf.data() + i * es, -1);
            lua_pop(L, 1);
            if (err)
                throw InvalidArgumentException(std::string("write: ") + err);
        }
        if (n)
            m_->write(p, buf.data(), n);
        return;
    }
    if (!a || static_cast<DataTypes>(a->type) != m_->type())
        throw InvalidArgumentException(std::string("write: expected an rr.array (rr.multidim) of ") +
                                       array_type_name(m_->type()));
    std::vector<uint64_t> d = m_->dims();
    std::vector<uint64_t> p = positions(L, pos.idx, d.size(), "write position");
    std::vector<uint64_t> c(d.size(), 1);
    for (int i = 0; i < a->ndims && i < static_cast<int>(c.size()); ++i)
        c[i] = static_cast<uint64_t>(array_dims(a)[i]);
    if (a->ndims > static_cast<int>(d.size()))
        throw InvalidArgumentException("write: the data has more dimensions than the memory");
    if (a->numel)
        m_->write_md(p, array_data(a), c);
}

} // namespace rl
