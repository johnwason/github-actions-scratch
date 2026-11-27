// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// The plain Lua headers, without lua-compat-5.3. For headers that SWIG-generated code
// includes: SWIG's Lua runtime defines its own 5.1 compatibility macros, which clash
// with compat-5.3's. Restrict such headers to the Lua 5.1 API.
#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
