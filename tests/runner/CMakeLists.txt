add_executable(rrlua_runner rrlua_runner.cpp)
target_link_libraries(rrlua_runner PRIVATE rr_lua_core "${RRLUA_LUA_LIBRARY}" ${CMAKE_DL_LIBS} m)
set_target_properties(rrlua_runner PROPERTIES ENABLE_EXPORTS ON)
