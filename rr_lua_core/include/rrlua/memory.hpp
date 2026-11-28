// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Memories (M5), numeric element types (rr.array types). One Lua handle serves both sides:
// - client: `obj:getMemory(name)` wraps RR's ArrayMemoryClient<T> / MultiDimArrayMemoryClient<T>
//   (each read or write is one RR request);
// - service: `svc:memory(name, initial)` keeps a native ArrayMemory<T> / MultiDimArrayMemory<T>
//   that RR serves directly, without Lua (CLAUDE.md D10).
// Locking rule: every access, from Lua or from RR threads, goes through RR's memory classes,
// which take the memory's own mutex for each read and write. Lua never holds a pointer into
// the memory; reads and writes copy.
//
// Lua positions are 1-based; RR's are 0-based.
#pragma once

#include "rrlua/guard.hpp"

#include <RobotRaconteur.h>

#include <memory>
#include <string>
#include <vector>

namespace rl
{

class MemoryAccess
{
  public:
    virtual ~MemoryAccess() = default;
    virtual bool multidim() const = 0;
    virtual RobotRaconteur::DataTypes type() const = 0;
    virtual uint64_t length() = 0;              // element count
    virtual std::vector<uint64_t> dims() = 0;   // {length} for an array memory
    // Array memories (0-based).
    virtual void read(uint64_t pos, void* dst, uint64_t count) = 0;
    virtual void write(uint64_t pos, const void* src, uint64_t count) = 0;
    // Multidimensional memories (0-based); the buffer is column-major with the given dims.
    virtual void read_md(const std::vector<uint64_t>& pos, void* dst, const std::vector<uint64_t>& count) = 0;
    virtual void write_md(const std::vector<uint64_t>& pos, const void* src, const std::vector<uint64_t>& count) = 0;
    // Service side: answer a memory request for member `name` of an object served by skel.
    virtual RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> serve(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m, const RR_SHARED_PTR<RobotRaconteur::Endpoint>& e,
        const RR_SHARED_PTR<RobotRaconteur::ServiceSkel>& skel, const std::string& name,
        RobotRaconteur::MemberDefinition_Direction direction) = 0;
};

// A client memory for member `def` of stub. Throws for element types it does not support.
std::shared_ptr<MemoryAccess> make_client_memory(const RR_SHARED_PTR<RobotRaconteur::MemoryDefinition>& def,
                                                 const RR_SHARED_PTR<RobotRaconteur::ServiceStub>& stub);
// A service memory of type t holding a copy of the rr.array at idx (its dims for multidim).
std::shared_ptr<MemoryAccess> make_local_memory(lua_State* L, int idx, const RR_SHARED_PTR<RobotRaconteur::MemoryDefinition>& def);

// Lua handle (SWIG).
class Memory
{
  public:
    explicit Memory(std::shared_ptr<MemoryAccess> m);

    std::string type();
    double length();
    rl::Ret dims(lua_State* L);
    // Array memory: read(pos[, count]) -> rr.array. Multidim: read({pos...}, {count...}) -> multidim.
    rl::Ret read(lua_State* L, rl::Arg pos);
    rl::Ret read(lua_State* L, rl::Arg pos, rl::Arg count);
    // write(pos, data): data is an rr.array (or a table, for array memories) of the element type.
    void write(rl::Arg pos, rl::Arg data);

  private:
    std::shared_ptr<MemoryAccess> m_;
};

} // namespace rl
