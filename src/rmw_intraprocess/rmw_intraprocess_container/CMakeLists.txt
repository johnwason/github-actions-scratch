cmake_minimum_required(VERSION 3.20)
project(rmw_intraprocess_container CXX)

# container_host: loads the components listed in a YAML file into one process
# (docs/container.md).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden by default (portability rule); nothing here is exported.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor)
elseif(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(class_loader REQUIRED)
find_package(composition_interfaces REQUIRED)
find_package(rcl_yaml_param_parser REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)

# Everything but main(), shared by the executable and the tests.
add_library(${PROJECT_NAME}_core STATIC
  src/cli.cpp
  src/config.cpp
  src/host.cpp)
target_include_directories(${PROJECT_NAME}_core PUBLIC src)
target_link_libraries(${PROJECT_NAME}_core PUBLIC
  ament_index_cpp::ament_index_cpp
  class_loader::class_loader
  ${composition_interfaces_TARGETS}
  rcl_yaml_param_parser::rcl_yaml_param_parser
  rclcpp::rclcpp
  rclcpp_components::component_manager
  rcpputils::rcpputils
  rcutils::rcutils
  rmw::rmw
  yaml-cpp)

add_executable(container_host src/main.cpp)
target_link_libraries(container_host ${PROJECT_NAME}_core)

install(TARGETS container_host DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(rcl_interfaces REQUIRED)
  find_package(rmw_intraprocess_cpp REQUIRED)
  find_package(std_msgs REQUIRED)

  set(_configs "${CMAKE_CURRENT_SOURCE_DIR}/test/configs")
  set(_rmw_dir "$<TARGET_FILE_DIR:rmw_intraprocess_cpp::rmw_intraprocess_cpp>")

  ament_add_gtest(test_config test/test_config.cpp)
  target_link_libraries(test_config ${PROJECT_NAME}_core)
  target_compile_definitions(test_config PRIVATE "TEST_CONFIG_DIR=\"${_configs}\"")

  foreach(test test_host test_cli)
    ament_add_gtest(${test} test/${test}.cpp
      ENV RMW_IMPLEMENTATION=rmw_intraprocess_cpp
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    target_link_libraries(${test} ${PROJECT_NAME}_core
      ${rcl_interfaces_TARGETS} ${std_msgs_TARGETS})
    target_compile_definitions(${test} PRIVATE "TEST_CONFIG_DIR=\"${_configs}\"")
  endforeach()

  # The real executable, end to end: loads three components, runs two
  # seconds, exits 0 (no RMW_IMPLEMENTATION: the default must be this RMW).
  ament_add_test(test_container_host_smoke
    COMMAND "$<TARGET_FILE:container_host>" "${_configs}/talker_listener.yaml"
      --shutdown-after 2
    ENV RMW_IMPLEMENTATION=
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO
    APPEND_LIBRARY_DIRS "${_rmw_dir}"
    TIMEOUT 120)
  # Self-terminating: the Fibonacci action client shuts rclcpp down.
  ament_add_test(test_container_host_fibonacci
    COMMAND "$<TARGET_FILE:container_host>" "${_configs}/fibonacci.yaml"
    ENV RMW_IMPLEMENTATION=
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO
    APPEND_LIBRARY_DIRS "${_rmw_dir}"
    TIMEOUT 120)
endif()

ament_package()
