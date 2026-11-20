// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Shared SWIG definitions for every project .i file. Never use the SWIG/*.i files from
// RR core (CLAUDE.md, D1).

%{
#include "rrlua/guard.hpp"
%}

// RR data passes through SWIG uninterpreted (CLAUDE.md, "RR data through SWIG").
%typemap(in) rl::Arg %{ $1.L = L; $1.idx = $input; %}
%typemap(in, numinputs=0) lua_State* L %{ $1 = L; %}
%typemap(out) rl::Ret %{ SWIG_arg += $1.n; %}
// Any Lua value is acceptable for an rl::Arg, so overload dispatch always matches.
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER) rl::Arg %{ $1 = 1; %}

// C++ exceptions become Lua errors carrying the script location. SWIG declares argument
// temporaries (for example the std::string for a `const std::string&`) in a block that
// its `fail:` label closes before calling lua_error. So this handler only pushes the
// message and jumps to `fail`; it must never call lua_error itself, which would longjmp
// past those destructors (M0.4 measured ~4 KB leaked per throw when it did).
%exception {
  rl::ErrorBuffer rl_err;
  try {
    $action
  } catch (...) {
    rl_err.assign_current_exception();
  }
  if (rl_err.set) {
    rl::push_error(L, rl_err);
    SWIG_fail;
  }
}
