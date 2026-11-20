# Client tests against RR core's C++ test server. They share a TCP port and the server's
# local transport node name, so they run one at a time.
rrlua_add_lua_test(servicetest.ts1 test_ts1.lua SANITIZER)
set_tests_properties("${RRLUA_LUA_FLAVOR}.servicetest.ts1" PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)
