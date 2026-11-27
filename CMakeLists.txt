cmake_minimum_required(VERSION 3.22)
project(robotraconteur_lua VERSION 0.0.1 LANGUAGES C CXX)

# One build tree per Lua runtime, e.g. build/lua54 and build/luajit. scripts/verify.sh
# configures both. The Lua headers and library come from .deps/ (built by
# scripts/bootstrap.sh), never from the system; see CLAUDE.md.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hide symbols: hosts may ship their own Boost and their own copy of RR.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(RRLUA_LUA_FLAVOR "lua54" CACHE STRING "Lua runtime: lua54 or luajit")
set_property(CACHE RRLUA_LUA_FLAVOR PROPERTY STRINGS lua54 luajit)
set(RRLUA_LUA_INCLUDE_DIR "" CACHE PATH "Directory holding lua.h for the chosen runtime")
set(RRLUA_LUA_LIBRARY "" CACHE FILEPATH
    "Lua library to link test executables against (modules do not link Lua)")
set(RRLUA_LUA_INTERPRETER "" CACHE FILEPATH "Interpreter that runs the Lua tests")
set(RRLUA_COMPAT53_DIR "" CACHE PATH "lua-compat-5.3 source checkout")
set(RRLUA_TEST_SERVER "" CACHE FILEPATH "RR core's robotraconteur_test_server, for client tests")
set(RRLUA_TEST_VECTORS "" CACHE FILEPATH "RR core's test/data/test_vectors.bin")
set(RRLUA_TEST_SERVER_LIB "" CACHE FILEPATH "RR core's librobotraconteur_test_server_lib.so")
set(RRLUA_PYTHON "" CACHE FILEPATH "Python with the robotraconteur package (test peer)")

option(RRLUA_BUILD_TESTS "Build tests" ON)
option(RRLUA_SANITIZER_BUILD
  "Sanitizer build: run Lua tests with the embedding runner instead of the interpreter" OFF)

if(NOT RRLUA_LUA_INCLUDE_DIR OR NOT EXISTS "${RRLUA_LUA_INCLUDE_DIR}/lua.h")
  message(FATAL_ERROR "Set RRLUA_LUA_INCLUDE_DIR to a directory containing lua.h "
                      "(run scripts/bootstrap.sh, then configure through scripts/verify.sh)")
endif()

# Interface target carrying the Lua headers. Modules leave Lua symbols unresolved so
# they bind to whatever Lua the host process already has loaded.
add_library(rrlua_lua_headers INTERFACE)
target_include_directories(rrlua_lua_headers INTERFACE "${RRLUA_LUA_INCLUDE_DIR}")
if(RRLUA_COMPAT53_DIR)
  target_include_directories(rrlua_lua_headers INTERFACE "${RRLUA_COMPAT53_DIR}/c-api")
endif()
target_compile_definitions(rrlua_lua_headers INTERFACE
  $<$<STREQUAL:${RRLUA_LUA_FLAVOR},luajit>:RRLUA_LUAJIT=1>)

# Link options for require-able Lua modules: keep everything but luaopen_* hidden,
# including symbols from static archives (Boost, RR core). -z nodelete keeps a module
# loaded after the last lua_State using it closes (Lua dlcloses C modules on lua_close).
# Unloading would reset SWIG's unlocked global type table, so the next load would
# reinitialize it while other threads may be loading too, and would pull code out from
# under any RR threads still running.
function(rrlua_module_link_options target)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${target} PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:-z,nodelete")
  endif()
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()

if(RRLUA_BUILD_TESTS)
  enable_testing()
  if(NOT RRLUA_LUA_INTERPRETER)
    message(FATAL_ERROR "Set RRLUA_LUA_INTERPRETER to run tests (or RRLUA_BUILD_TESTS=OFF)")
  endif()
endif()

# Each runtime's client tests use their own port, so two build trees can test at once.
if(RRLUA_LUA_FLAVOR STREQUAL "luajit")
  set(RRLUA_TEST_PORT 22232)
else()
  set(RRLUA_TEST_PORT 22231)
endif()

# Directory where every require-able module of this build is placed.
set(RRLUA_MODULE_DIR "${CMAKE_BINARY_DIR}/lua_modules")

# rrlua_add_lua_test(<name> <script> [SANITIZER] [ARGS ...]): run a Lua script with this
# build's interpreter, with built modules on package.cpath and tests/lua on package.path.
# SANITIZER marks a test to also run in sanitizer builds (label "sanitizer"); only those
# tests are added in a sanitizer build.
function(rrlua_add_lua_test name script)
  cmake_parse_arguments(T "SANITIZER" "" "ARGS" ${ARGN})
  if(RRLUA_SANITIZER_BUILD AND NOT T_SANITIZER)
    return()
  endif()
  if(RRLUA_SANITIZER_BUILD)
    set(interp $<TARGET_FILE:rrlua_runner>)
  else()
    set(interp "${RRLUA_LUA_INTERPRETER}")
  endif()
  set(tname "${RRLUA_LUA_FLAVOR}.${name}")
  add_test(NAME "${tname}" COMMAND ${interp} "${script}" ${T_ARGS}
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties("${tname}" PROPERTIES ENVIRONMENT
    "LUA_CPATH=${RRLUA_MODULE_DIR}/?.so;LUA_PATH=${PROJECT_SOURCE_DIR}/tests/lua/?.lua;RRLUA_LUA_FLAVOR=${RRLUA_LUA_FLAVOR};RRLUA_TEST_SERVER=${RRLUA_TEST_SERVER};RRLUA_TEST_VECTORS=${RRLUA_TEST_VECTORS};RRLUA_TEST_PORT=${RRLUA_TEST_PORT};RRLUA_PYTHON=${RRLUA_PYTHON}")
  if(T_SANITIZER)
    set_tests_properties("${tname}" PROPERTIES LABELS "sanitizer")
  endif()
endfunction()

# rrlua_add_module(<target> <sources...>): a require-able module in RRLUA_MODULE_DIR.
function(rrlua_add_module target)
  add_library(${target} MODULE ${ARGN})
  rrlua_module_link_options(${target})
  set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${RRLUA_MODULE_DIR}")
endfunction()

foreach(subdir rr_lua_core module tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt")
    add_subdirectory(${subdir})
  endif()
endforeach()
