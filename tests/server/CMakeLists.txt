# M4: Lua services.
rrlua_add_lua_test(server.lua_client test_server.lua SANITIZER)
# RR core's Python client against a Lua service (the M4 acceptance peer).
rrlua_add_lua_test(server.python_client test_python_client.lua SANITIZER)
set_tests_properties("${RRLUA_LUA_FLAVOR}.server.python_client" PROPERTIES TIMEOUT 120)
