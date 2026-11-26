// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
// M0.4 test module. Not part of the product.
%module rrlua_m0

%include "rrlua_common.i"
%include <std_string.i>

%{
#include "m0.hpp"
%}

%native(varargs) int m0_varargs(lua_State* L);
%native(varargs_throw) int m0_varargs_throw(lua_State* L);
%ignore m0_varargs;
%ignore m0_varargs_throw;
%ignore m0::live_trackers;

%include "m0.hpp"
