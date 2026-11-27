# The binding's thread pool must let a shut-down node be destroyed even with handlers
# still queued (rrlua/thread_pool.hpp).
add_executable(node_release node_release.cpp)
target_link_libraries(node_release PRIVATE rr_lua_core)
add_test(NAME "${RRLUA_LUA_FLAVOR}.node_release" COMMAND node_release)
set_tests_properties("${RRLUA_LUA_FLAVOR}.node_release" PROPERTIES TIMEOUT 300 LABELS "sanitizer")
