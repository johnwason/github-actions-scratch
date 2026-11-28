# M0.4: a trivial SWIG module exercising rl::Arg, rl::Ret, %native varargs and the
# error guard on both runtimes.

find_package(SWIG 4.2 REQUIRED COMPONENTS lua)
include(UseSWIG)
# Regenerate wrappers when an included .i file (such as rrlua_common.i) changes.
set(SWIG_USE_SWIG_DEPENDENCIES ON)

set_property(SOURCE rrlua_m0.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE rrlua_m0.i PROPERTY INCLUDE_DIRECTORIES "${RRLUA_SWIG_DIR}")
swig_add_library(rrlua_m0 TYPE MODULE LANGUAGE lua
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/swig"
  OUTFILE_DIR "${CMAKE_CURRENT_BINARY_DIR}/swig"
  SOURCES rrlua_m0.i m0.cpp)
target_include_directories(rrlua_m0 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rrlua_m0 PRIVATE rr_lua_core)
rrlua_module_link_options(rrlua_m0)
set_target_properties(rrlua_m0 PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${RRLUA_MODULE_DIR}")

rrlua_add_lua_test(m0.passthrough test_m0.lua SANITIZER)
# RSS measurements are meaningless under sanitizers, so this one is not marked SANITIZER.
rrlua_add_lua_test(m0.throw_memory test_m0_memory.lua)
