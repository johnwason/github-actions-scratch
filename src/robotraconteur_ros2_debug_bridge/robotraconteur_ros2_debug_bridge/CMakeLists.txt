cmake_minimum_required(VERSION 3.20)
project(robotraconteur_ros2_debug_bridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden by default (portability rule); the public API goes through
# visibility_control.hpp.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor)
elseif(MSVC)
  # Two-phase lookup off: Robot Raconteur's public _WIN32_WINNT=0x0600 hides
  # Windows 8 declarations that Boost.Atomic's wait templates name
  # (docs/upstream_issues.md).
  add_compile_options(/W4 /permissive- /Zc:twoPhase-)
endif()

find_package(ament_cmake REQUIRED)
find_package(action_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(composition_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rosidl_runtime_c REQUIRED)
find_package(rosidl_typesupport_introspection_c REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(rmw REQUIRED)
find_package(std_msgs REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)
# Robot Raconteur (static core library, linked into the component: RR and
# Boost stay behind its DLL boundary).
find_package(RobotRaconteur REQUIRED)

# Generated thunk code for the static root definition.
robotraconteur_generate_thunk(
  ROOT_THUNK_SRCS ROOT_THUNK_HDRS
  experimental.ros2_debug_bridge.robdef
  MASTER_HEADER
  INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/robdef")
add_library(${PROJECT_NAME}_robdef STATIC ${ROOT_THUNK_SRCS} ${ROOT_THUNK_HDRS})
# SYSTEM: the generated code is not warning-clean under this package's flags.
target_include_directories(${PROJECT_NAME}_robdef SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(${PROJECT_NAME}_robdef PRIVATE -w)
target_link_libraries(${PROJECT_NAME}_robdef PUBLIC RobotRaconteurCore)

# Everything but the component registration, shared with the unit tests.
add_library(${PROJECT_NAME}_core STATIC
  src/action_proxy.cpp
  src/bridge_component.cpp
  src/component_proxy.cpp
  src/config.cpp
  src/exposure_policy.cpp
  src/generic_service.cpp
  src/proxy_executor.cpp
  src/graph_object.cpp
  src/parameter_proxy.cpp
  src/service_proxy.cpp
  src/topic_proxy.cpp
  src/root_object.cpp
  src/type_mapper.cpp
  src/typesupport.cpp
  src/value_converter.cpp)
target_include_directories(${PROJECT_NAME}_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_compile_definitions(${PROJECT_NAME}_core PRIVATE ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_BUILDING_DLL)
target_link_libraries(${PROJECT_NAME}_core PUBLIC
  ${PROJECT_NAME}_robdef
  ${action_msgs_TARGETS}
  ament_index_cpp::ament_index_cpp
  ${composition_interfaces_TARGETS}
  rclcpp::rclcpp
  rclcpp_components::component
  rcpputils::rcpputils
  rosidl_runtime_c::rosidl_runtime_c
  rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
  rmw::rmw
  yaml-cpp)

# B12 fast path: rmw_intraprocess_cpp's tap API, if that package is there
# at build time. Header only: found at run time through the RMW in use, so
# the bridge neither links nor needs that RMW.
find_package(rmw_intraprocess_cpp QUIET)
if(rmw_intraprocess_cpp_FOUND)
  target_compile_definitions(${PROJECT_NAME}_core PRIVATE ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_HAVE_TAP)
  target_include_directories(${PROJECT_NAME}_core PRIVATE
    "$<TARGET_PROPERTY:rmw_intraprocess_cpp::rmw_intraprocess_cpp,INTERFACE_INCLUDE_DIRECTORIES>")
  message(STATUS "rmw_intraprocess_cpp tap API: on")
else()
  message(STATUS "rmw_intraprocess_cpp tap API: not found (fast path off)")
endif()

# The component library (rclcpp_components) and the public make_bridge_node().
add_library(${PROJECT_NAME} SHARED src/component_library.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE ROBOTRACONTEUR_ROS2_DEBUG_BRIDGE_BUILDING_DLL)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_core)
target_link_libraries(${PROJECT_NAME} PUBLIC rclcpp::rclcpp)
rclcpp_components_register_nodes(${PROJECT_NAME} "robotraconteur_ros2_debug_bridge::DebugBridge")
# Robot Raconteur (static) stays private to this library: its globals (for
# example IntraTransport's registry) must not be shared, by symbol
# interposition, with another copy in the process (an executable or another
# component linking Robot Raconteur); that destroys them twice at unload/exit.
# A Windows DLL exports nothing by default, so this matches it. test_symbols.py
# checks it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
  target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--exclude-libs,ALL")
endif()

add_executable(debug_bridge src/main.cpp)
target_link_libraries(debug_bridge ${PROJECT_NAME} ${std_msgs_TARGETS})

install(TARGETS ${PROJECT_NAME} EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
# Writes the generated definitions for ROS interface types (and tests use it).
add_executable(generate_robdef src/generate_robdef.cpp)
target_link_libraries(generate_robdef ${PROJECT_NAME}_core)

install(TARGETS debug_bridge generate_robdef DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})
install(DIRECTORY config robdef DESTINATION share/${PROJECT_NAME})
install(PROGRAMS ../tools/robotraconteur_ros2_debug_cli.py DESTINATION lib/${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp)

if(BUILD_TESTING)
  # A checkout of github.com/robotraconteur-contrib/robotraconteur_ros2_bridge
  # (the image has one here; the Windows CI clones one; docs/windows.md).
  set(UPSTREAM_BRIDGE_DIR "/opt/robotraconteur_ros2_bridge" CACHE PATH
    "Reference robotraconteur_ros2_bridge checkout used by the compatibility tests")
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  find_package(class_loader REQUIRED)
  find_package(rmw_intraprocess_cpp REQUIRED)

  # The safety helper every C++ test uses to create Robot Raconteur nodes.
  add_library(rr_test_safety STATIC test/rr_test_safety.cpp)
  target_include_directories(rr_test_safety PUBLIC test)
  target_link_libraries(rr_test_safety PUBLIC RobotRaconteurCore)

  ament_add_gtest(test_rr_test_safety test/test_rr_test_safety.cpp)
  target_link_libraries(test_rr_test_safety rr_test_safety)

  ament_add_gtest(test_config test/test_config.cpp)
  target_link_libraries(test_config ${PROJECT_NAME}_core rr_test_safety)
  target_compile_definitions(test_config PRIVATE
    "DEFAULT_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/config/default.yaml\"")

  ament_add_gtest(test_exposure_policy test/test_exposure_policy.cpp)
  target_link_libraries(test_exposure_policy ${PROJECT_NAME}_core)
  target_compile_definitions(test_exposure_policy PRIVATE
    "DEFAULT_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/config/default.yaml\"")

  # B1 acceptance: load/unload through the component factory 100 times, each
  # time reading root properties from an in-process client, under both RMWs.
  set(_rmw_dir "$<TARGET_FILE_DIR:rmw_intraprocess_cpp::rmw_intraprocess_cpp>")
  foreach(rmw rmw_intraprocess_cpp rmw_fastrtps_cpp)
    ament_add_gtest(test_component_lifecycle__${rmw} test/test_component_lifecycle.cpp
      ENV RMW_IMPLEMENTATION=${rmw}
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 600)
    target_link_libraries(test_component_lifecycle__${rmw}
      rr_test_safety ${PROJECT_NAME}_robdef rclcpp::rclcpp class_loader::class_loader
      rclcpp_components::component)
    target_compile_definitions(test_component_lifecycle__${rmw} PRIVATE
      "COMPONENT_LIBRARY=\"$<TARGET_FILE:${PROJECT_NAME}>\"")
  endforeach()

  # B2: type mapper unit, golden-file and all-installed-interfaces tests, and
  # the compatibility test against the upstream bridge's generator.
  ament_add_gtest(test_type_mapper test/test_type_mapper.cpp TIMEOUT 600)
  target_link_libraries(test_type_mapper ${PROJECT_NAME}_core)
  target_compile_definitions(test_type_mapper PRIVATE
    "GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test/golden\"")
  ament_add_pytest_test(test_upstream_compat test/test_upstream_compat.py
    ENV "GENERATE_ROBDEF_EXE=$<TARGET_FILE:generate_robdef>"
        "UPSTREAM_BRIDGE_DIR=${UPSTREAM_BRIDGE_DIR}"
    TIMEOUT 600)

  # B3: value converter round trips; the reference packing is
  # RobotRaconteurGen's generated code for these golden definitions.
  set(_conformance_defs
    rosmsg_builtin_interfaces__Duration rosmsg_builtin_interfaces__Time
    rosmsg_std_msgs__Header rosmsg_sensor_msgs__JointState
    rosmsg_geometry_msgs__Point rosmsg_geometry_msgs__Quaternion rosmsg_geometry_msgs__Pose
    rosmsg_geometry_msgs__PoseWithCovariance rosmsg_geometry_msgs__Vector3
    rosmsg_geometry_msgs__Twist rosmsg_geometry_msgs__TwistWithCovariance
    rosmsg_nav_msgs__Odometry
    rosmsg_test_msgs__Arrays rosmsg_test_msgs__BasicTypes rosmsg_test_msgs__BoundedPlainSequences
    rosmsg_test_msgs__BoundedSequences rosmsg_test_msgs__Builtins rosmsg_test_msgs__Constants
    rosmsg_test_msgs__Defaults rosmsg_test_msgs__Empty rosmsg_test_msgs__MultiNested
    rosmsg_test_msgs__Nested rosmsg_test_msgs__Strings rosmsg_test_msgs__UnboundedSequences
    rosmsg_test_msgs__WStrings)
  set(_conformance_files)
  foreach(d ${_conformance_defs})
    list(APPEND _conformance_files "${d}.robdef")
  endforeach()
  robotraconteur_generate_thunk(
    CONFORMANCE_THUNK_SRCS CONFORMANCE_THUNK_HDRS
    ${_conformance_files}
    MASTER_HEADER
    OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/conformance_thunk"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/test/golden")
  set_source_files_properties(${CONFORMANCE_THUNK_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
  # B11: fuzzing the converter's inputs (time-boxed; FUZZ_SECONDS).
  ament_add_gtest(test_converter_fuzz test/test_converter_fuzz.cpp
    ENV "FUZZ_SECONDS=20"
    TIMEOUT 600)
  target_link_libraries(test_converter_fuzz ${PROJECT_NAME}_core)

  ament_add_gtest(test_value_converter test/test_value_converter.cpp
    ${CONFORMANCE_THUNK_SRCS} ${CONFORMANCE_THUNK_HDRS}
    TIMEOUT 600)
  target_include_directories(test_value_converter SYSTEM BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/conformance_thunk" "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(test_value_converter ${PROJECT_NAME}_core)

  # B4 acceptance: topics from a Python client, under both RMWs.
  find_package(geometry_msgs REQUIRED)
  find_package(sensor_msgs REQUIRED)
  find_package(turtlesim REQUIRED)
  find_package(action_tutorials_interfaces REQUIRED)
  find_package(example_interfaces REQUIRED)
  find_package(rclcpp_action REQUIRED)
  add_executable(topic_test_host test/topic_test_host.cpp)
  target_link_libraries(topic_test_host ${PROJECT_NAME} ${std_msgs_TARGETS}
    ${geometry_msgs_TARGETS} ${sensor_msgs_TARGETS} ${turtlesim_TARGETS}
    ${action_tutorials_interfaces_TARGETS} ${example_interfaces_TARGETS}
    rclcpp_action::rclcpp_action rclcpp_components::component_manager)
  target_include_directories(topic_test_host PRIVATE src)
  # B12 benchmark host (docs/benchmark.md).
  add_executable(benchmark_host test/benchmark_host.cpp)
  target_link_libraries(benchmark_host ${PROJECT_NAME} ${sensor_msgs_TARGETS})
  target_include_directories(benchmark_host PRIVATE src)
  foreach(rmw rmw_intraprocess_cpp rmw_fastrtps_cpp)
    ament_add_pytest_test(test_topics__${rmw} test/test_topics.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B5 acceptance: graph introspection.
    ament_add_pytest_test(test_graph__${rmw} test/test_graph.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B6 acceptance: services.
    ament_add_pytest_test(test_services__${rmw} test/test_services.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B7 acceptance: parameters.
    ament_add_pytest_test(test_parameters__${rmw} test/test_parameters.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B8 acceptance: actions.
    ament_add_pytest_test(test_actions__${rmw} test/test_actions.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B9 acceptance: component loading.
    ament_add_pytest_test(test_components__${rmw} test/test_components.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B10 acceptance: the debug CLI, every subcommand.
    ament_add_pytest_test(test_cli__${rmw} test/test_cli.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
          "DEBUG_CLI=${CMAKE_CURRENT_SOURCE_DIR}/../tools/robotraconteur_ros2_debug_cli.py"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 600)
    # B11 acceptance: authentication.
    ament_add_pytest_test(test_authentication__${rmw} test/test_authentication.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # B12: the benchmark runs (one short case); docs/benchmark.md.
    ament_add_pytest_test(test_benchmark_smoke__${rmw} test/test_benchmark_smoke.py
      ENV "BENCHMARK_HOST_EXE=$<TARGET_FILE:benchmark_host>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
    # The upstream bridge's turtlesim examples, with only the URL changed.
    ament_add_pytest_test(test_upstream_examples__${rmw} test/test_upstream_examples.py
      ENV "TOPIC_TEST_HOST_EXE=$<TARGET_FILE:topic_test_host>" "RMW_IMPLEMENTATION=${rmw}"
          "UPSTREAM_BRIDGE_DIR=${UPSTREAM_BRIDGE_DIR}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 300)
  endforeach()

  # B0 acceptance: C++ service (generated thunk code) + Python dynamic client.
  robotraconteur_generate_thunk(
    SMOKE_THUNK_SRCS SMOKE_THUNK_HDRS
    experimental.ros2_debug_bridge_smoke.robdef
    MASTER_HEADER
    OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/smoke_thunk"
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/test/smoke")
  add_executable(rr_smoke_service test/rr_smoke_service.cpp
    ${SMOKE_THUNK_SRCS} ${SMOKE_THUNK_HDRS})
  target_include_directories(rr_smoke_service SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/smoke_thunk")
  target_link_libraries(rr_smoke_service rr_test_safety)
  set_source_files_properties(${SMOKE_THUNK_SRCS} PROPERTIES COMPILE_OPTIONS "-w")

  ament_add_pytest_test(test_python_client test/test_python_client.py
    ENV "RR_SMOKE_SERVICE_EXE=$<TARGET_FILE:rr_smoke_service>"
    TIMEOUT 120)
  ament_add_pytest_test(test_safety_helper_usage test/test_safety_helper_usage.py
    TIMEOUT 60)
  ament_add_pytest_test(test_symbols test/test_symbols.py
    ENV "COMPONENT_LIBRARY=$<TARGET_FILE:${PROJECT_NAME}>"
    TIMEOUT 60)
  # B1 acceptance: the standalone executable, a Python client over
  # LocalTransport reading root properties, under both RMWs.
  foreach(rmw rmw_intraprocess_cpp rmw_fastrtps_cpp)
    ament_add_pytest_test(test_standalone__${rmw} test/test_standalone.py
      ENV "DEBUG_BRIDGE_EXE=$<TARGET_FILE:debug_bridge>" "RMW_IMPLEMENTATION=${rmw}"
      APPEND_LIBRARY_DIRS "${_rmw_dir}"
      TIMEOUT 120)
  endforeach()
endif()

ament_package()
