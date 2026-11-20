# Fake embedded host (M2.2): links the binding statically, as a host plugin would.
find_package(Threads REQUIRED)
add_executable(host_sim host_sim.cpp)
target_link_libraries(host_sim PRIVATE
  robotraconteur_lua_static "${RRLUA_LUA_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS} m)

foreach(scenario states borrowed connection streaming)
  add_test(NAME "${RRLUA_LUA_FLAVOR}.host_sim.${scenario}" COMMAND host_sim ${scenario})
  set_tests_properties("${RRLUA_LUA_FLAVOR}.host_sim.${scenario}" PROPERTIES
    TIMEOUT 300 LABELS "sanitizer"
    ENVIRONMENT "RRLUA_TEST_SERVER_LIB=${RRLUA_TEST_SERVER_LIB}")
endforeach()
# The test server library uses the same local-transport node name each run.
set_tests_properties("${RRLUA_LUA_FLAVOR}.host_sim.connection" "${RRLUA_LUA_FLAVOR}.host_sim.streaming"
  PROPERTIES RUN_SERIAL TRUE)
