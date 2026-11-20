// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/guard.hpp"

#include <RobotRaconteur.h>

#include <cstring>
#include <string>
#include <stdexcept>

namespace rl
{

void ErrorBuffer::assign(const char* s) noexcept
{
    if (!s)
        s = "unknown error";
    len = std::strlen(s);
    if (len >= capacity)
        len = capacity - 1;
    std::memcpy(text, s, len);
    text[len] = '\0';
    set = true;
}

void ErrorBuffer::assign_current_exception() noexcept
{
    try
    {
        throw;
    }
    catch (const RobotRaconteur::RobotRaconteurException& e)
    {
        // "RobotRaconteur.InvalidArgument: message" (or the service-defined error name)
        try
        {
            std::string m = e.Error + ": " + e.Message;
            assign(m.c_str());
        }
        catch (...)
        {
            assign(e.what());
        }
    }
    catch (const std::exception& e)
    {
        assign(e.what());
    }
    catch (...)
    {
        assign("unknown C++ exception");
    }
}

void push_error(lua_State* L, const ErrorBuffer& err)
{
    luaL_where(L, 1);
    lua_pushlstring(L, err.text, err.len);
    lua_concat(L, 2);
}

void raise(lua_State* L, const ErrorBuffer& err)
{
    push_error(L, err);
    lua_error(L);
    // lua_error does not return; this silences [[noreturn]] warnings on old headers.
    std::terminate();
}

} // namespace rl
