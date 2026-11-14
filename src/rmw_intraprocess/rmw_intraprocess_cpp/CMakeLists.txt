cmake_minimum_required(VERSION 3.20)
project(rmw_intraprocess_cpp CXX)

# C++17 only, no compiler extensions (Windows/MSVC portability, see CLAUDE.md).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden by default so a missing visibility macro fails on Linux the same way it
# would on MSVC (enforced by scripts/check-portability.sh and check-symbols.sh).
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor)
elseif(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

find_package(ament_cmake REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(fastcdr REQUIRED CONFIG)
find_package(rosidl_runtime_c REQUIRED)
find_package(rosidl_typesupport_fastrtps_c REQUIRED)
find_package(rosidl_typesupport_fastrtps_cpp REQUIRED)
find_package(rosidl_typesupport_introspection_c REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
ament_package_xml()

set(rmw_intraprocess_cpp_sources
  src/build_info.cpp
  src/qos.cpp
  src/endpoint.cpp
  src/event.cpp
  src/registry.cpp
  src/rmw_graph.cpp
  src/rmw_guard_condition.cpp
  src/rmw_identifier.cpp
  src/rmw_client.cpp
  src/rmw_event.cpp
  src/rmw_init.cpp
  src/rmw_node.cpp
  src/rmw_publisher.cpp
  src/rmw_serialize.cpp
  src/rmw_service.cpp
  src/rmw_subscription.cpp
  src/rmw_unsupported.cpp
  src/rmw_wait_set.cpp
  src/service.cpp
  src/tap.cpp
  src/type_support.cpp
)

# Usage requirements shared by the shipped library and the static test build.
function(rmw_intraprocess_cpp_configure target)
  target_include_directories(${target} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
  target_link_libraries(${target} PUBLIC
    fastcdr
    rcutils::rcutils
    rmw::rmw
    rosidl_runtime_c::rosidl_runtime_c
    rosidl_typesupport_fastrtps_c::rosidl_typesupport_fastrtps_c
    rosidl_typesupport_fastrtps_cpp::rosidl_typesupport_fastrtps_cpp
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
    rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp)
  target_compile_definitions(${target} PRIVATE
    # rmw.h declarations use RMW_PUBLIC; this makes them dllexport on Windows.
    RMW_BUILDING_DLL
    RMW_INTRAPROCESS_CPP_BUILDING_LIBRARY
    "RMW_INTRAPROCESS_CPP_VERSION_STRING=\"${${PROJECT_NAME}_VERSION}\"")
endfunction()

add_library(${PROJECT_NAME} SHARED ${rmw_intraprocess_cpp_sources})
rmw_intraprocess_cpp_configure(${PROJECT_NAME})

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})
install(TARGETS ${PROJECT_NAME} EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(
  fastcdr rcutils rmw rosidl_runtime_c
  rosidl_typesupport_fastrtps_c rosidl_typesupport_fastrtps_cpp
  rosidl_typesupport_introspection_c rosidl_typesupport_introspection_cpp)

# Registers this package as an RMW implementation in the ament index, so
# RMW_IMPLEMENTATION=rmw_intraprocess_cpp selects it and
# call_for_each_rmw_implementation() includes it.
register_rmw_implementation(
  "c:rosidl_typesupport_c:rosidl_typesupport_fastrtps_c:rosidl_typesupport_introspection_c"
  "cpp:rosidl_typesupport_cpp:rosidl_typesupport_fastrtps_cpp:rosidl_typesupport_introspection_cpp")

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
  find_package(action_msgs REQUIRED)
  find_package(action_tutorials_interfaces REQUIRED)
  find_package(ament_index_cpp REQUIRED)
  find_package(class_loader REQUIRED)
  find_package(composition_interfaces REQUIRED)
  find_package(example_interfaces REQUIRED)
  find_package(lifecycle_msgs REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rcl_interfaces REQUIRED)
  find_package(rclcpp_action REQUIRED)
  find_package(rclcpp_components REQUIRED)
  find_package(rclcpp_lifecycle REQUIRED)
  find_package(rosbag2_cpp REQUIRED)
  find_package(rosbag2_transport REQUIRED)
  find_package(rcpputils REQUIRED)
  find_package(rosidl_typesupport_cpp REQUIRED)
  find_package(std_msgs REQUIRED)
  find_package(test_msgs REQUIRED)

  ament_add_gtest(test_build_info test/test_build_info.cpp)
  target_link_libraries(test_build_info ${PROJECT_NAME})

  # Unit tests link a static build of the same sources, so they can use internal
  # (hidden) classes; each test process then has exactly one registry.
  add_library(${PROJECT_NAME}_test_static STATIC ${rmw_intraprocess_cpp_sources})
  rmw_intraprocess_cpp_configure(${PROJECT_NAME}_test_static)
  # The tests compile the rmw.h (and our) declarations the way the library does
  # (dllexport, not dllimport), so on Windows they link to the static
  # definitions instead of looking for __imp_ symbols from a DLL.
  target_compile_definitions(${PROJECT_NAME}_test_static PUBLIC
    RMW_BUILDING_DLL RMW_INTRAPROCESS_CPP_BUILDING_LIBRARY)
  foreach(t test_init test_node test_guard_condition test_unsupported test_endpoints
      test_pubsub test_wait_set test_qos test_events test_services test_graph test_tap
      test_cdr_fuzz)
    ament_add_gtest(${t} test/${t}.cpp)
    target_include_directories(${t} PRIVATE src)
    target_link_libraries(${t} ${PROJECT_NAME}_test_static)
  endforeach()
  foreach(t test_endpoints test_pubsub test_wait_set test_events test_services test_graph
      test_tap test_cdr_fuzz)
    target_link_libraries(${t} ${test_msgs_TARGETS} rosidl_typesupport_cpp::rosidl_typesupport_cpp)
  endforeach()
  # test_events checks the KEEP_ALL limit with a small value.
  set_tests_properties(test_events PROPERTIES ENVIRONMENT "RMW_INTRAPROCESS_KEEP_ALL_LIMIT=50")

  ament_add_gtest(test_symbols test/test_symbols.cpp)
  target_link_libraries(test_symbols rcpputils::rcpputils)
  target_compile_definitions(test_symbols PRIVATE
    "RMW_INTRAPROCESS_CPP_LIBRARY_PATH=\"$<TARGET_FILE:${PROJECT_NAME}>\"")
  add_dependencies(test_symbols ${PROJECT_NAME})

  # Goes through rmw_implementation (dynamic loading); does not link this library.
  ament_add_gtest(test_rclcpp_smoke test/test_rclcpp_smoke.cpp
    APPEND_LIBRARY_DIRS "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    ENV RMW_IMPLEMENTATION=rmw_intraprocess_cpp
    TIMEOUT 60)
  target_link_libraries(test_rclcpp_smoke rclcpp::rclcpp)
  add_dependencies(test_rclcpp_smoke ${PROJECT_NAME})

  # Same rclcpp tests against this RMW and against the reference RMW.
  foreach(rmw rmw_intraprocess_cpp rmw_fastrtps_cpp)
    foreach(t test_rclcpp_pubsub test_rclcpp_qos test_rclcpp_services test_rclcpp_graph
        test_rclcpp_generic test_rosbag2_components test_rclcpp_intra_process
        test_rclcpp_executors test_rclcpp_actions test_rclcpp_parameters test_rclcpp_lifecycle
        test_composition_demo test_rclcpp_tap)
      ament_add_gtest(${t}__${rmw} test/${t}.cpp
        APPEND_LIBRARY_DIRS "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        ENV RMW_IMPLEMENTATION=${rmw}
        TIMEOUT 300)
      target_link_libraries(${t}__${rmw} rclcpp::rclcpp ${test_msgs_TARGETS})
      add_dependencies(${t}__${rmw} ${PROJECT_NAME})
      if(rmw STREQUAL "rmw_fastrtps_cpp")
        # TSan builds only: suppressions for races inside the reference RMW's
        # libraries. These processes never load rmw_intraprocess_cpp.
        set(_tsan_supp "${CMAKE_CURRENT_SOURCE_DIR}/test/tsan_reference_rmw.supp")
        set_tests_properties(${t}__${rmw} PROPERTIES ENVIRONMENT
          "TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:suppressions=${_tsan_supp}")
      endif()
    endforeach()
    # M8: actions, lifecycle, and the composition demo (component container
    # loading the standard demo components).
    target_link_libraries(test_rclcpp_actions__${rmw}
      rclcpp_action::rclcpp_action ${action_msgs_TARGETS})
    target_link_libraries(test_rclcpp_parameters__${rmw} ${rcl_interfaces_TARGETS})
    # M9: the tap test uses the public extension headers only (no link to this
    # library: the API is found at run time).
    target_include_directories(test_rclcpp_tap__${rmw} PRIVATE include)
    target_link_libraries(test_rclcpp_lifecycle__${rmw}
      rclcpp_lifecycle::rclcpp_lifecycle ${lifecycle_msgs_TARGETS})
    target_link_libraries(test_composition_demo__${rmw}
      rclcpp_action::rclcpp_action
      rclcpp_components::component_manager
      ${action_msgs_TARGETS}
      ${action_tutorials_interfaces_TARGETS}
      ${composition_interfaces_TARGETS}
      ${example_interfaces_TARGETS}
      ${std_msgs_TARGETS})
    # M6: rosbag2 recorder/player loaded as components. The test also links
    # rosbag2_transport so the component library stays mapped until exit;
    # otherwise its unload at exit orphans rosidl_typesupport_cpp's per-type
    # library-handle cache and LeakSanitizer reports it (with either RMW).
    target_link_libraries(test_rosbag2_components__${rmw}
      ament_index_cpp::ament_index_cpp
      class_loader::class_loader
      rclcpp_components::component
      rosbag2_cpp::rosbag2_cpp
      rosbag2_transport::rosbag2_transport)
  endforeach()

  # M10: benchmark executable (not a test; scripts/benchmark.sh runs it).
  add_executable(bench_pubsub benchmark/bench_pubsub.cpp)
  target_link_libraries(bench_pubsub rclcpp::rclcpp ${std_msgs_TARGETS})
endif()

ament_package()
