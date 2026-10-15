# A stuck synthetic worker must end through the production deadline, not merely
# fail with an arbitrary nonzero code. No title code is linked into this test.
execute_process(COMMAND "${SESSION_TEST_PROGRAM}" --shutdown-timeout
    RESULT_VARIABLE result ERROR_VARIABLE error OUTPUT_VARIABLE output TIMEOUT 10)
if(NOT result STREQUAL "2" OR NOT error MATCHES "Session shutdown exceeded 1 seconds")
    message(FATAL_ERROR "Shutdown deadline not observed: ${result}\n${error}\n${output}")
endif()
