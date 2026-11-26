# The Lua ports of RR core's Reynard, standard robot and standard camera client examples,
# against stand-in services (a Reynard mock, and the loopback robot and camera from the
# pinned Racket client checkout). Runs the stock interpreter (the examples are subprocesses).
rrlua_add_lua_test(examples test_examples.lua)
set_tests_properties("${RRLUA_LUA_FLAVOR}.examples" PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
