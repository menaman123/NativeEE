execute_process(COMMAND "${SESSION_TEST_PROGRAM}" --restore-timeout
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 7)
if(NOT result EQUAL 2 OR NOT error MATCHES "Session restore exceeded 1 seconds")
    message(FATAL_ERROR "Restore deadline did not terminate a stuck provider: ${result}\n${output}\n${error}")
endif()
