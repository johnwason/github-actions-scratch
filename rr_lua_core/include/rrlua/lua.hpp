// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Lua headers for C++ sources in this project. Written against the Lua 5.1 C API plus
// lua-compat-5.3 (CLAUDE.md, D4), so the same code builds for Lua 5.4 and LuaJIT.
// Do not include this from a header that SWIG-generated code includes; use
// rrlua/lua_api.hpp there.
#pragma once

#include "rrlua/lua_api.hpp"

extern "C" {
#include "compat-5.3.h"
}
