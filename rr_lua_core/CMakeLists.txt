# Host-neutral C++ binding core (static library, CLAUDE.md D7).

find_package(RobotRaconteur REQUIRED)

add_library(rr_lua_core STATIC
  src/guard.cpp src/thread_pool.cpp src/dispatch.cpp src/context.cpp src/node.cpp
  src/module.cpp src/array.cpp src/marshal.cpp src/client.cpp src/async.cpp src/ops.cpp src/streaming.cpp src/server.cpp src/generator.cpp src/memory.cpp src/subscription.cpp
  src/testvectors.cpp)
target_include_directories(rr_lua_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rr_lua_core PUBLIC rrlua_lua_headers RobotRaconteurCore)

# lua-compat-5.3 C sources, compiled in with a private prefix so they never clash with
# a host's own copy.
if(RRLUA_COMPAT53_DIR)
  target_sources(rr_lua_core PRIVATE "${RRLUA_COMPAT53_DIR}/c-api/compat-5.3.c")
  set_source_files_properties("${RRLUA_COMPAT53_DIR}/c-api/compat-5.3.c" PROPERTIES LANGUAGE C)
endif()
target_compile_definitions(rr_lua_core PUBLIC COMPAT53_PREFIX=rrlua_compat53)

set(RRLUA_SWIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/swig" CACHE INTERNAL "")
