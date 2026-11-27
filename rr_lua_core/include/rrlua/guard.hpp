// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// The error guard and the raw stack reference types used by every Lua entry point.
//
// lua_error() longjmps (on Lua built as C) past C++ destructors. So C++ exceptions are
// caught here, their message is copied into a fixed buffer (trivially destructible), the
// try/catch block is left so every C++ object in it is destroyed, and only then is the
// Lua error raised. See CLAUDE.md, "Hard rules for C++ code".
#pragma once

#include "rrlua/lua_api.hpp"

#include <cstddef>
#include <exception>
#include <stdexcept>

namespace rl
{

// A Lua value on the stack, passed through SWIG uninterpreted. Valid only during the
// call and only on the Lua thread: never store it or hand it to another thread.
struct Arg
{
    lua_State* L = nullptr;
    int idx = 0;
};

// Number of values a function has pushed itself as its results.
struct Ret
{
    int n = 0;
};

// An error raised by Lua code that the binding ran (an event listener or a coroutine resumed
// by the pump), carried through C++ to the caller. Its message already holds the script
// location, so it is raised again without another location prefix.
class LuaError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// Fixed-size, trivially destructible holder for an error message.
struct ErrorBuffer
{
    static constexpr std::size_t capacity = 2048;
    char text[capacity];
    std::size_t len = 0;
    bool set = false;
    bool from_lua = false; // a LuaError: no location prefix

    void assign(const char* s) noexcept;
    // Store the message of the exception currently being handled. Call only in a catch.
    void assign_current_exception() noexcept;
};

// Push the error message, prefixed with the calling script's location. Can raise only on
// Lua allocation failure.
void push_error(lua_State* L, const ErrorBuffer& err);

// push_error() then lua_error(). Never returns. Call only when no non-trivial C++ object
// is live in any frame between here and the Lua call boundary.
[[noreturn]] void raise(lua_State* L, const ErrorBuffer& err);

// Run f() with C++ exceptions turned into Lua errors. f returns the number of results.
template <typename F>
int guard(lua_State* L, F&& f)
{
    ErrorBuffer err;
    int n = 0;
    try
    {
        n = f();
    }
    catch (...)
    {
        err.assign_current_exception();
    }
    if (err.set)
        raise(L, err); // all C++ objects from the try block are destroyed by now
    return n;
}

} // namespace rl
