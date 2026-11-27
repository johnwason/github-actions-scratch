if(NOT RRLUA_BUILD_TESTS)
  return()
endif()

add_subdirectory(runner)
add_subdirectory(m0)
add_subdirectory(module)

add_subdirectory(stress)
add_subdirectory(node_release)
add_subdirectory(host_sim)
add_subdirectory(servicetest)
add_subdirectory(server)
add_subdirectory(examples)
