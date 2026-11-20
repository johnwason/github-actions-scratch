if(NOT RRLUA_BUILD_TESTS)
  return()
endif()

add_subdirectory(runner)
add_subdirectory(m0)

add_subdirectory(stress)
