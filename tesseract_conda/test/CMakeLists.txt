find_package(GTest REQUIRED)
find_package(
  tesseract REQUIRED
  COMPONENTS common
             geometry
             scene_graph
             srdf
             urdf
             environment)

macro(tesseract_conda_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE tesseract::conda LibArchive::LibArchive GTest::gtest)
  target_compile_options(${name} PRIVATE ${TESSERACT_CONDA_WARNINGS})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(
    ${name}
    DISCOVERY_TIMEOUT
    60
    DISCOVERY_MODE
    PRE_TEST)
endmacro()

include(GoogleTest)
tesseract_conda_add_test(tesseract_conda_package_unit conda_package_unit.cpp)
tesseract_conda_add_test(tesseract_conda_resource_locator_unit conda_resource_locator_unit.cpp)
tesseract_conda_add_test(tesseract_conda_integration_unit conda_integration_unit.cpp)
target_link_libraries(
  tesseract_conda_integration_unit
  PRIVATE tesseract::geometry
          tesseract::scene_graph
          tesseract::srdf
          tesseract::urdf
          tesseract::environment)
