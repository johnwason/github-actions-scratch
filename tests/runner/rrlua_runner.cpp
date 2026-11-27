// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Minimal script runner that embeds Lua the way a host does. Used instead of the stock
// interpreter in sanitizer builds, so the sanitizer runtime is linked into the process
// that loads the instrumented modules.
//
// usage: rrlua_runner <script.lua> [args...]

#include "rrlua/lua.hpp"

#include <cstdio>

static int traceback(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s script.lua [args...]\n", argv[0]);
        return 2;
    }
    lua_State* L = luaL_newstate();
    if (!L)
        return 1;
    luaL_openlibs(L);

    lua_newtable(L);
    for (int i = 0; i < argc; ++i)
    {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, i - 1);
    }
    lua_setglobal(L, "arg");

    lua_pushcfunction(L, traceback);
    int status = luaL_loadfile(L, argv[1]);
    if (status == 0)
    {
        for (int i = 2; i < argc; ++i)
            lua_pushstring(L, argv[i]);
        status = lua_pcall(L, argc - 2, 0, 1);
    }
    if (status != 0)
        std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L); // also runs finalizers, as a host closing a state does
    return status == 0 ? 0 : 1;
}
