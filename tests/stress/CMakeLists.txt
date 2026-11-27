# Teardown stress test (M0.5): embeds Lua like a host and cycles RR nodes.

find_package(RobotRaconteur REQUIRED)
find_package(Threads REQUIRED)

add_executable(teardown_stress teardown_stress.cpp)
target_link_libraries(teardown_stress PRIVATE
  rr_lua_core RobotRaconteurCore "${RRLUA_LUA_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS} m)
# Export the Lua API from the executable so dlopen'ed modules resolve against it.
set_target_properties(teardown_stress PROPERTIES ENABLE_EXPORTS ON)

add_test(NAME "${RRLUA_LUA_FLAVOR}.teardown_stress" COMMAND teardown_stress 20 4)
set_tests_properties("${RRLUA_LUA_FLAVOR}.teardown_stress" PROPERTIES
  ENVIRONMENT "LUA_CPATH=${RRLUA_MODULE_DIR}/?.so"
  TIMEOUT 600
  LABELS "stress;sanitizer")
