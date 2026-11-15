# Runs RobotRaconteurGenGo for the upstream test service definitions and
# compares the output byte for byte with test/golden (like upstream
# RobotRaconteurGen test_gen_<lang>). Set UPDATE=ON to rewrite the golden files.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
execute_process(
    COMMAND "${GEN}" --thunksource --go-import-prefix=github.com/robotraconteur/robotraconteur_go/test/gen
            -I "${ROBDEF_DIR}" --output-dir "${OUT}" com.robotraconteur.testing.TestService1.robdef
            com.robotraconteur.testing.TestService2.robdef com.robotraconteur.testing.TestService3.robdef
    RESULT_VARIABLE res OUTPUT_QUIET)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "RobotRaconteurGenGo failed: ${res}")
endif()
file(GLOB_RECURSE files RELATIVE "${OUT}" "${OUT}/*.go")
foreach(f ${files})
    get_filename_component(name "${f}" NAME)
    if(UPDATE)
        configure_file("${OUT}/${f}" "${GOLDEN}/${name}.golden" COPYONLY)
    else()
        execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${OUT}/${f}" "${GOLDEN}/${name}.golden"
                        RESULT_VARIABLE diff)
        if(NOT diff EQUAL 0)
            message(FATAL_ERROR "generated ${f} differs from golden ${GOLDEN}/${name}.golden")
        endif()
    endif()
endforeach()
