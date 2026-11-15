if(NOT APPLE)
  find_package(Boost REQUIRED COMPONENTS system filesystem stacktrace_noop OPTIONAL_COMPONENTS stacktrace_backtrace
                                                                                               stacktrace_basic)
else()
  find_package(Boost REQUIRED COMPONENTS system filesystem stacktrace_noop)
endif()

find_package(TinyXML2 REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(boost_plugin_loader REQUIRED)

option(TESSERACT_ENABLE_CONDA_ARCHIVES "Read resources from .conda packages" ON)
if(TESSERACT_ENABLE_CONDA_ARCHIVES)
  find_package(LibArchive REQUIRED)
  set(TESSERACT_COMMON_HAS_CONDA_ARCHIVES_CONFIG ON)
else()
  set(TESSERACT_COMMON_HAS_CONDA_ARCHIVES_CONFIG OFF)
endif()
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/conda_config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/tesseract/common/conda_config.h)

if(TARGET Boost::stacktrace_backtrace)
  find_file(BACKTRACE_INCLUDE_FILE backtrace.h PATHS ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
  if(BACKTRACE_INCLUDE_FILE)
    set(TESSERACT_BACKTRACE_DEFINITION BOOST_STACKTRACE_USE_BACKTRACE
                                       BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE="${BACKTRACE_INCLUDE_FILE}")
  else()
    set(TESSERACT_BACKTRACE_DEFINITION BOOST_STACKTRACE_USE_BACKTRACE)
  endif()
  set(TESSERACT_BACKTRACE_COMPONENT stacktrace_backtrace)
  set(TESSERACT_BACKTRACE_LIB Boost::stacktrace_backtrace)
elseif(TARGET Boost::stacktrace_basic)
  set(TESSERACT_BACKTRACE_DEFINITION "")
  set(TESSERACT_BACKTRACE_COMPONENT "stacktrace_basic")
  set(TESSERACT_BACKTRACE_LIB "Boost::stacktrace_basic")
elseif(TARGET Boost::stacktrace_noop)
  set(TESSERACT_BACKTRACE_DEFINITION BOOST_STACKTRACE_USE_NOOP)
  set(TESSERACT_BACKTRACE_COMPONENT "stacktrace_noop")
  set(TESSERACT_BACKTRACE_LIB "Boost::stacktrace_noop")
endif()

list(APPEND SUPPORTED_COMPONENTS common)

add_library(
  common
  src/allowed_collision_matrix.cpp
  src/any_poly.cpp
  src/calibration_info.cpp
  src/collision_margin_data.cpp
  src/contact_allowed_validator.cpp
  src/joint_state.cpp
  src/manipulator_info.cpp
  src/kinematic_limits.cpp
  src/utils.cpp
  src/plugin_info.cpp
  src/ply_io.cpp
  src/profile_dictionary.cpp
  src/profile_plugin_factory.cpp
  src/profile.cpp
  src/resource_locator.cpp
  src/property_tree.cpp
  src/schema_registry.cpp
  src/schema_registration.cpp
  src/yaml_extensions.cpp
  src/stopwatch.cpp
  src/timer.cpp
  src/types.cpp
  src/yaml_utils.cpp
  src/logging.cpp)
add_library(tesseract::common ALIAS common)
if(NOT WIN32)
  # Compile the cereal polymorphic-type registration. On Windows it is registered in the header instead.
  target_sources(common PRIVATE src/cereal_serialization.cpp)
endif()
set_target_properties(common PROPERTIES OUTPUT_NAME tesseract_common)
target_link_libraries(
  common
  PUBLIC Eigen3::Eigen
         tinyxml2::tinyxml2
         boost_plugin_loader::boost_plugin_loader
         ${CMAKE_DL_LIBS}
         Boost::boost
         Boost::system
         Boost::filesystem
         ${TESSERACT_BACKTRACE_LIB}
         spdlog::spdlog
         cereal::cereal
         yaml-cpp)
target_compile_options(common PUBLIC ${TESSERACT_COMPILE_OPTIONS_PUBLIC})
target_compile_options(common PRIVATE ${TESSERACT_COMPILE_OPTIONS_PRIVATE})
target_compile_definitions(common PUBLIC ${TESSERACT_COMPILE_DEFINITIONS_PUBLIC} ${TESSERACT_BACKTRACE_DEFINITION})
target_compile_definitions(common PRIVATE ${TESSERACT_COMPILE_DEFINITIONS_PRIVATE})
target_clang_tidy(common ENABLE ${TESSERACT_ENABLE_CLANG_TIDY})
target_cxx_version(common PUBLIC VERSION ${TESSERACT_CXX_VERSION})
target_code_coverage(
  common
  PRIVATE
  ALL
  EXCLUDE ${COVERAGE_EXCLUDE}
  ENABLE ${TESSERACT_ENABLE_CODE_COVERAGE})
target_include_directories(common PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                         "$<INSTALL_INTERFACE:include>")

target_include_directories(common PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

if(WIN32)
  target_link_libraries(common PUBLIC Bcrypt)
endif()

if(TESSERACT_ENABLE_CONDA_ARCHIVES)
  target_sources(common PRIVATE src/conda_package.cpp)
  target_link_libraries(common PRIVATE LibArchive::LibArchive)
  target_compile_definitions(common PUBLIC TESSERACT_COMMON_HAS_CONDA_ARCHIVES=1)
endif()

# ADD Examples
if(TESSERACT_ENABLE_EXAMPLES OR TESSERACT_COMMON_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

# A static tesseract_common carries its PRIVATE libarchive link into the exported targets
set(TESSERACT_COMMON_FIND_LIBARCHIVE OFF)
if(TESSERACT_ENABLE_CONDA_ARCHIVES)
  get_target_property(TESSERACT_COMMON_LIBRARY_TYPE common TYPE)
  if(TESSERACT_COMMON_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
    set(TESSERACT_COMMON_FIND_LIBARCHIVE ON)
  endif()
endif()

# configure_package
configure_component(COMPONENT common NAMESPACE tesseract TARGETS common)

if(TESSERACT_ENABLE_CONDA_ARCHIVES)
  set(TESSERACT_COMMON_CONDA_HEADER_EXCLUDE "")
else()
  set(TESSERACT_COMMON_CONDA_HEADER_EXCLUDE PATTERN "conda_package.h" EXCLUDE)
endif()
install(
  DIRECTORY include/tesseract
  DESTINATION include
  COMPONENT common
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hpp"
  PATTERN ".svn" EXCLUDE ${TESSERACT_COMMON_CONDA_HEADER_EXCLUDE})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/tesseract/common/conda_config.h DESTINATION include/tesseract/common
        COMPONENT common)

install(
  FILES "${CMAKE_CURRENT_LIST_DIR}/cmake/tesseract_macros.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/Findassimp.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/FindTinyXML2.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/Findtcmalloc.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/Findtcmalloc_minimal.cmake"
  DESTINATION lib/cmake/${PROJECT_NAME}
  COMPONENT common)

# Test Suite
if(TESSERACT_BUILD_TEST_SUITE)
  list(APPEND SUPPORTED_COMPONENTS common_test_suite)
  add_subdirectory(test_suite)
endif()

if(TESSERACT_ENABLE_TESTING OR TESSERACT_COMMON_ENABLE_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()

if(TESSERACT_PACKAGE)
  if(TESSERACT_ENABLE_CONDA_ARCHIVES)
    set(TESSERACT_COMMON_CONDA_LINUX_DEPENDS "libarchive-dev")
    set(TESSERACT_COMMON_CONDA_WINDOWS_DEPENDS "libarchive")
  endif()
  cpack_component(
    COMPONENT common
    VERSION ${pkg_extracted_version}
    DESCRIPTION "Tesseract Common Components"
    LINUX_DEPENDS
      "libboost-system-dev"
      "libboost-filesystem-dev"
      "libspdlog-dev"
      "libtinyxml2-dev"
      "libeigen3-dev"
      "libyaml-cpp-dev"
      "cereal"
      ${TESSERACT_COMMON_CONDA_LINUX_DEPENDS}
    WINDOWS_DEPENDS
      "boost_system"
      "boost_filesystem"
      "spdlog"
      "tinyxml2"
      "Eigen3"
      "yaml-cpp"
      "cereal"
      ${TESSERACT_COMMON_CONDA_WINDOWS_DEPENDS})

  if(UNIX AND TESSERACT_PACKAGE_SOURCE)
    cpack_debian_source_package(
      CHANGLELOG ${CMAKE_CURRENT_LIST_DIR}/CHANGELOG.rst
      UPLOAD ${TESSERACT_PACKAGE_SOURCE_UPLOAD}
      DPUT_HOST ${TESSERACT_PACKAGE_SOURCE_DPUT_HOST}
      DEBIAN_INCREMENT ${TESSERACT_PACKAGE_SOURCE_DEBIAN_INCREMENT}
      DISTRIBUTIONS ${TESSERACT_PACKAGE_SOURCE_DISTRIBUTIONS})
  endif()
endif()

# Propagate accumulated components to parent scope
set(SUPPORTED_COMPONENTS ${SUPPORTED_COMPONENTS} PARENT_SCOPE)
