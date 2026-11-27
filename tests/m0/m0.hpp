// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// M0.4 test fixture: a trivial handle class wrapped by SWIG. Not part of the product.
#pragma once

#include "rrlua/guard.hpp"

#include <string>

namespace m0
{

// Counts live C++ objects created on paths that throw, to prove destructors run.
int live_trackers();

class Probe
{
  public:
    Probe();
    ~Probe();

    rl::Ret echo(rl::Arg a);                // returns a copy of any value, nil included
    rl::Ret swap(rl::Arg a, rl::Arg b);     // returns b, a
    rl::Ret none(lua_State* L);             // returns no values
    std::string typeName(rl::Arg a);        // Lua type name of a
    void throwWith(const std::string& msg); // throws with msg while a tracker is live
    int liveTrackers();
};

} // namespace m0

// %native varargs functions.
extern "C++" int m0_varargs(lua_State* L);       // returns select('#', ...), ...
extern "C++" int m0_varargs_throw(lua_State* L); // throws with its first argument
