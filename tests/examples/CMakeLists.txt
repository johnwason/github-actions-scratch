# The Lua ports of RR core's Reynard, standard robot and standard camera client examples,
# against stand-in services (a Reynard mock, and the loopback robot and camera from the
# pinned Racket client checkout). Runs the stock interpreter (the examples are subprocesses).
rrlua_add_lua_test(examples test_examples.lua)
# Not created in sanitizer builds (no SANITIZER flag: the examples run the stock interpreter).
if(TEST "${RRLUA_LUA_FLAVOR}.examples")
  set_tests_properties("${RRLUA_LUA_FLAVOR}.examples" PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endif()
