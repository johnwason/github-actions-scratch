cmake_minimum_required(VERSION 3.18.0)
project(tesseract_conda VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TESSERACT_CONDA_ENABLE_TESTING "Build the tesseract_conda tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

find_package(tesseract REQUIRED COMPONENTS common)
find_package(LibArchive REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(cereal REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(TESSERACT_CONDA_WARNINGS
      -Wall
      -Wextra
      -Werror=all
      -Werror=extra
      -Werror=conversion
      -Werror=sign-conversion
      -Wno-sign-compare
      -Werror=non-virtual-dtor)
elseif(MSVC)
  set(TESSERACT_CONDA_WARNINGS /W3 /bigobj)
endif()

add_library(conda src/conda_package.cpp src/conda_resource_locator.cpp)
add_library(tesseract::conda ALIAS conda)
set_target_properties(conda PROPERTIES OUTPUT_NAME tesseract_conda)
if(NOT WIN32)
  # Compile the cereal polymorphic-type registration. On Windows it is registered in the header instead.
  target_sources(conda PRIVATE src/cereal_serialization.cpp)
endif()
if(WIN32)
  set_target_properties(conda PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_compile_features(conda PUBLIC cxx_std_17)
target_compile_options(conda PRIVATE ${TESSERACT_CONDA_WARNINGS})
target_include_directories(conda PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(conda PUBLIC tesseract::common yaml-cpp cereal::cereal PRIVATE LibArchive::LibArchive)

install(
  TARGETS conda
  EXPORT tesseract_conda-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/tesseract DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT tesseract_conda-targets NAMESPACE tesseract:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tesseract_conda)

# A static library carries its PRIVATE libarchive link into the exported targets
get_target_property(TESSERACT_CONDA_LIBRARY_TYPE conda TYPE)
if(TESSERACT_CONDA_LIBRARY_TYPE STREQUAL "STATIC_LIBRARY")
  set(TESSERACT_CONDA_FIND_LIBARCHIVE ON)
else()
  set(TESSERACT_CONDA_FIND_LIBARCHIVE OFF)
endif()
configure_package_config_file(
  cmake/tesseract_conda-config.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/tesseract_conda-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tesseract_conda)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/tesseract_conda-config-version.cmake
                                 COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tesseract_conda-config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/tesseract_conda-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tesseract_conda)
install(FILES package.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME})

if(TESSERACT_CONDA_ENABLE_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()
