// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Test support: a reader for RR core's shared test vector blob (test/data/test_vectors.bin),
// following the "Reader requirements" of test/data/TEST_VECTORS_FORMAT.md (version 1).
// rr._testVector(path, name, type) returns the vector as a 1-D rr.array of that type.

#include "rrlua/array.hpp"
#include "rrlua/guard.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

uint32_t u32(const std::vector<unsigned char>& b, std::size_t off)
{
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

} // namespace

int rl_test_vector(lua_State* L)
{
    return rl::guard(L, [L]() -> int {
        if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING || lua_type(L, 3) != LUA_TSTRING)
            throw std::invalid_argument("_testVector(path, name, type)");
        std::string path = lua_tostring(L, 1);
        std::string name = lua_tostring(L, 2);
        std::string tname = lua_tostring(L, 3);
        RobotRaconteur::DataTypes type;
        if (!rl::array_type_from_name(tname.c_str(), type) || static_cast<int>(type) < 1 || static_cast<int>(type) > 10)
            throw std::invalid_argument("test vector " + name + ": unsupported type " + tname);

        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("cannot open test vectors " + path);
        std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // 1. size and magic
        if (b.size() < 24 || std::memcmp(b.data(), "RRTVECTS", 8) != 0)
            throw std::runtime_error("not a test vector blob: " + path);
        // 2. version and entry size
        if (u32(b, 8) != 1 || u32(b, 12) != 64)
            throw std::runtime_error("unsupported test vector blob version or entry size");
        // 3. file size and index bounds
        uint32_t count = u32(b, 16);
        if (u32(b, 20) != b.size() || static_cast<uint64_t>(count) * 64 + 24 > b.size())
            throw std::runtime_error("test vector blob size mismatch");
        // 4. exact name lookup
        for (uint32_t i = 0; i < count; ++i)
        {
            std::size_t e = 24 + static_cast<std::size_t>(i) * 64;
            std::size_t n = 0;
            while (n < 48 && b[e + n] != 0)
                ++n;
            if (std::string(reinterpret_cast<const char*>(&b[e]), n) != name)
                continue;
            // 5. type must match exactly
            if (u32(b, e + 48) != static_cast<uint32_t>(type))
                throw std::runtime_error("test vector " + name + " is not of type " + tname);
            uint32_t elems = u32(b, e + 52);
            uint32_t off = u32(b, e + 56);
            uint64_t bytes = static_cast<uint64_t>(elems) * rl::array_elem_size(type);
            // 6. data bounds without 32-bit overflow
            if (static_cast<uint64_t>(off) + bytes > b.size())
                throw std::runtime_error("test vector " + name + " extends past the end of the blob");
            int64_t n64 = elems;
            rl::ArrayHeader* a = rl::push_array(L, type, &n64, 1);
            std::memcpy(rl::array_data(a), &b[off], static_cast<std::size_t>(bytes)); // copy, never cast
            return 1;
        }
        // 7. missing vector named in the message
        throw std::runtime_error("test vector " + name + " not found");
    });
}
