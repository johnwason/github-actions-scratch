cmake_minimum_required(VERSION 3.16)
project(RobotRaconteurGo LANGUAGES C CXX)

# Robot Raconteur Go bindings.
#   - RobotRaconteurCore comes from an out-of-tree build of the upstream
#     robotraconteur source (scripts/build.sh), found with find_package.
#   - SWIG generates the internal Go package and a C++ wrapper.
#   - The C++ wrapper + upstream SWIG/RobotRaconteurWrapped.cpp form the static
#     library RobotRaconteurGoNative that `go build` links via cgo.

set(RR_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../robotraconteur" CACHE PATH "Upstream robotraconteur source directory")
get_filename_component(RR_SOURCE_DIR "${RR_SOURCE_DIR}" ABSOLUTE)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(RobotRaconteur REQUIRED)
find_package(SWIG 4.0.2 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/GoLinkFlags.cmake)

set(RRGO_SWIG_DIR "${CMAKE_SOURCE_DIR}/swig")
set(RRGO_GO_PKG_DIR "${CMAKE_SOURCE_DIR}/robotraconteur/internal/swig")
set(RRGO_GEN_DIR "${CMAKE_BINARY_DIR}/swig")
file(MAKE_DIRECTORY "${RRGO_GEN_DIR}")

# On LP64 (Linux/macOS) int64_t is 'long'; tell SWIG's stdint.i (as the C#/Java bindings do)
include(CheckTypeSize)
check_type_size("long" RRGO_SIZEOF_LONG)
if(RRGO_SIZEOF_LONG EQUAL 8)
    set(RRGO_SWIG_WORDSIZE -DSWIGWORDSIZE64)
endif()

file(GLOB RRGO_SWIG_DEPS "${RRGO_SWIG_DIR}/*.i" "${RRGO_SWIG_DIR}/*.h" "${RR_SOURCE_DIR}/SWIG/*.i")

set(RRGO_WRAP_CXX "${RRGO_GEN_DIR}/RobotRaconteurGo_wrap.cxx")
set(RRGO_WRAP_GO "${RRGO_GO_PKG_DIR}/robotraconteurswig.go")
set(RRGO_UPCASTS_CXX "${RRGO_GEN_DIR}/rrgo_upcasts.cxx")
set(RRGO_UPCASTS_GO "${RRGO_GO_PKG_DIR}/upcasts_gen.go")

add_custom_command(
    OUTPUT "${RRGO_WRAP_CXX}" "${RRGO_WRAP_GO}" "${RRGO_UPCASTS_CXX}" "${RRGO_UPCASTS_GO}"
    COMMAND "${SWIG_EXECUTABLE}" -go -cgo -c++ -intgosize 64 ${RRGO_SWIG_WORDSIZE} -I${RRGO_SWIG_DIR} -I${RR_SOURCE_DIR}/SWIG
            -I${RR_SOURCE_DIR}/RobotRaconteurCore/include -outdir "${RRGO_GEN_DIR}" -o "${RRGO_WRAP_CXX}"
            -xmlout "${RRGO_GEN_DIR}/RobotRaconteurGo.xml" "${RRGO_SWIG_DIR}/RobotRaconteurGo.i"
    COMMAND "${Python3_EXECUTABLE}" "${RRGO_SWIG_DIR}/postprocess_go.py" "${RRGO_GEN_DIR}/robotraconteurswig.go"
            "${RRGO_GEN_DIR}/RobotRaconteurGo.xml" "${RRGO_GEN_DIR}/upcasts_gen.go" "${RRGO_UPCASTS_CXX}"
    COMMAND ${CMAKE_COMMAND} -E copy "${RRGO_GEN_DIR}/robotraconteurswig.go" "${RRGO_WRAP_GO}"
    COMMAND ${CMAKE_COMMAND} -E copy "${RRGO_GEN_DIR}/upcasts_gen.go" "${RRGO_UPCASTS_GO}"
    DEPENDS ${RRGO_SWIG_DEPS} "${RRGO_SWIG_DIR}/postprocess_go.py"
    COMMENT "SWIG: generating Robot Raconteur Go wrapper"
    VERBATIM)

add_library(RobotRaconteurGoNative STATIC "${RRGO_WRAP_CXX}" "${RRGO_UPCASTS_CXX}"
                                          "${RR_SOURCE_DIR}/SWIG/RobotRaconteurWrapped.cpp")
target_include_directories(RobotRaconteurGoNative PRIVATE "${RR_SOURCE_DIR}/SWIG" "${RRGO_SWIG_DIR}")
target_compile_definitions(RobotRaconteurGoNative PRIVATE SWIG_TYPE_TABLE=robotraconteur_go)
target_link_libraries(RobotRaconteurGoNative PUBLIC RobotRaconteurCore)
# The SWIG wrapper is a very large translation unit; keep memory use bounded.
set_source_files_properties("${RRGO_WRAP_CXX}" PROPERTIES COMPILE_OPTIONS "-g0")

# cgo link flags for the internal swig package
rrgo_collect_link_flags(RRGO_LINK_FLAGS RobotRaconteurGoNative)
string(JOIN "\n" RRGO_LDFLAGS_LINES ${RRGO_LINK_FLAGS})
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/cgo_link_libs.txt" CONTENT "${RRGO_LDFLAGS_LINES}\n")
add_custom_command(
    OUTPUT "${RRGO_GO_PKG_DIR}/cgo_flags_linux.go"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/gen_cgo_flags.py" "${CMAKE_BINARY_DIR}/cgo_link_libs.txt"
            "${RRGO_GO_PKG_DIR}/cgo_flags_linux.go" linux
    DEPENDS "${CMAKE_BINARY_DIR}/cgo_link_libs.txt" "${CMAKE_SOURCE_DIR}/cmake/gen_cgo_flags.py"
    COMMENT "Generating cgo link flags"
    VERBATIM)
add_custom_target(rrgo_cgo_flags ALL DEPENDS "${RRGO_GO_PKG_DIR}/cgo_flags_linux.go" RobotRaconteurGoNative)

# Go error codes/types from the upstream core headers
add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/robotraconteur/errors_gen.go"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/gen_go_errors.py"
            "${RR_SOURCE_DIR}/RobotRaconteurCore/include/RobotRaconteur" "${CMAKE_SOURCE_DIR}/robotraconteur/errors_gen.go"
    DEPENDS "${CMAKE_SOURCE_DIR}/cmake/gen_go_errors.py" "${RR_SOURCE_DIR}/RobotRaconteurCore/include/RobotRaconteur/Error.h"
            "${RR_SOURCE_DIR}/RobotRaconteurCore/include/RobotRaconteur/RobotRaconteurConstants.h"
    COMMENT "Generating Go error types"
    VERBATIM)
add_custom_target(rrgo_errors ALL DEPENDS "${CMAKE_SOURCE_DIR}/robotraconteur/errors_gen.go")
