cmake_minimum_required(VERSION 3.20)
# C for the rosidl-generated C type support.
project(rmw_intraprocess_torture C CXX)

# Test-only package (M8c): torture components for rmw_intraprocess_cpp, loaded
# through their component factories into one process by test/test_torture.cpp.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden by default (portability rule; class_loader registers the components
# through static initializers, so nothing needs exporting).
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor)
elseif(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(test_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/Payload.msg"
  "srv/GetStats.srv")
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} rosidl_typesupport_cpp)

# One component library; every class is registered with
# RCLCPP_COMPONENTS_REGISTER_NODE and listed in the ament index below.
add_library(${PROJECT_NAME}_components SHARED
  src/action_storm.cpp
  src/entity_churn.cpp
  src/lifecycle_churn.cpp
  src/param_churn.cpp
  src/qos_matrix.cpp
  src/reentrant_callbacks.cpp
  src/service_storm.cpp
  src/topic_flood.cpp)
target_link_libraries(${PROJECT_NAME}_components
  "${cpp_typesupport_target}"
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  rclcpp_components::component
  rclcpp_lifecycle::rclcpp_lifecycle
  ${test_msgs_TARGETS})
foreach(component ActionStorm EntityChurn LifecycleChurn ParamChurn QosMatrix ReentrantCallbacks
    ServiceStorm TopicFlood)
  rclcpp_components_register_nodes(${PROJECT_NAME}_components
    "${PROJECT_NAME}::${component}")
endforeach()
install(TARGETS ${PROJECT_NAME}_components
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(composition_interfaces REQUIRED)
  find_package(lifecycle_msgs REQUIRED)
  find_package(rmw_intraprocess_cpp REQUIRED)

  # The driver loads the components from the ament index (installed prefix),
  # so it runs after `colcon build` has installed this package.
  # This RMW only. rmw_fastrtps_cpp was tried as a reference (2026-11-14) and
  # fails the invariants in the same process (lost RELIABLE samples, service
  # timeouts, then a hang), so it cannot separate RMW defects from test ones
  # here; the result is recorded in docs/unsupported.md. To repeat it, add
  # rmw_fastrtps_cpp to this list.
  foreach(rmw rmw_intraprocess_cpp)
    ament_add_gtest(test_torture__${rmw} test/test_torture.cpp
      ENV RMW_IMPLEMENTATION=${rmw}
      # Self-contained: works without a sourced setup file (the RoboStack CI
      # runs colcon test without one). The components come from this
      # package's install prefix, the RMW library from its package.
      APPEND_ENV AMENT_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
      APPEND_LIBRARY_DIRS
        "${CMAKE_INSTALL_PREFIX}/lib" "${CMAKE_INSTALL_PREFIX}/bin"
        "$<TARGET_FILE_DIR:rmw_intraprocess_cpp::rmw_intraprocess_cpp>"
      TIMEOUT 1200)
    target_link_libraries(test_torture__${rmw}
      "${cpp_typesupport_target}"
      rclcpp::rclcpp
      rclcpp_components::component_manager
      ${composition_interfaces_TARGETS}
      ${test_msgs_TARGETS}
      # Keeps lifecycle_msgs' type support mapped until exit (see
      # keep_type_support_mapped in the test).
      ${lifecycle_msgs_TARGETS})
    if(rmw STREQUAL "rmw_fastrtps_cpp")
      # TSan builds only: the reference RMW's suppressions (see
      # rmw_intraprocess_cpp/test/tsan_reference_rmw.supp).
      set(_tsan_supp
        "${CMAKE_CURRENT_SOURCE_DIR}/../rmw_intraprocess_cpp/test/tsan_reference_rmw.supp")
      set_tests_properties(test_torture__${rmw} PROPERTIES ENVIRONMENT
        "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:suppressions=${_tsan_supp}")
    endif()
  endforeach()
endif()

ament_export_dependencies(rosidl_default_runtime)
ament_package()
