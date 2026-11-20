# require("robotraconteur"): the module, and a static library of the same code for host
# plugins that cannot load modules (CLAUDE.md D7).

find_package(SWIG 4.2 REQUIRED COMPONENTS lua)
include(UseSWIG)
set(SWIG_USE_SWIG_DEPENDENCIES ON)

set_property(SOURCE robotraconteur.i PROPERTY CPLUSPLUS ON)
set_property(SOURCE robotraconteur.i PROPERTY INCLUDE_DIRECTORIES
  "${RRLUA_SWIG_DIR}" "${PROJECT_SOURCE_DIR}/rr_lua_core/include")
swig_add_library(robotraconteur TYPE MODULE LANGUAGE lua
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/swig"
  OUTFILE_DIR "${CMAKE_CURRENT_BINARY_DIR}/swig"
  SOURCES robotraconteur.i)
target_link_libraries(robotraconteur PRIVATE rr_lua_core)
rrlua_module_link_options(robotraconteur)
set_target_properties(robotraconteur PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${RRLUA_MODULE_DIR}")

# Static form for host plugins: the generated wrapper plus rr_lua_core.
set(rrlua_wrap "${CMAKE_CURRENT_BINARY_DIR}/swig/robotraconteurLUA_wrap.cxx")
set_source_files_properties("${rrlua_wrap}" PROPERTIES GENERATED TRUE)
add_library(robotraconteur_lua_static STATIC "${rrlua_wrap}")
add_dependencies(robotraconteur_lua_static robotraconteur)
target_link_libraries(robotraconteur_lua_static PUBLIC rr_lua_core)
