add_library(ps2r_session_restart STATIC ../host/session_restart.cpp ../host/session_workers.cpp)
target_link_libraries(ps2r_session_restart PUBLIC ps2r_session_checkpoint)
target_compile_options(ps2r_session_restart PRIVATE -Wall -Wextra -Werror)

if(PS2R_PRODUCT_TESTS)
    # Use the production source with the existing narrow fixture support in
    # both root and isolated builds. No title body or duplicate shadow ABI.
    add_executable(test_session_restart "${PS2R_PRODUCT_ROOT}/tests/app/test_session_restart.cpp"
        ../host/session_restart.cpp)
    target_link_libraries(test_session_restart PRIVATE ps2r_continuation_checkpoint)
    target_compile_options(test_session_restart PRIVATE -Wall -Wextra -Werror)
    add_test(NAME SessionRestart COMMAND test_session_restart)
    set_tests_properties(SessionRestart PROPERTIES TIMEOUT 30)
    if(PS2R_PRODUCT_GUI)
        add_executable(test_session_restart_runtime
            "${PS2R_PRODUCT_ROOT}/tests/app/test_session_restart_runtime.cpp"
            ../host/session_restart.cpp ../host/session_workers.cpp ../host/platform_input.cpp)
        target_link_libraries(test_session_restart_runtime PRIVATE
            ps2r_continuation_checkpoint ps2r_session_runtime ${product_input_support})
        target_include_directories(test_session_restart_runtime PRIVATE $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_options(test_session_restart_runtime PRIVATE -Wall -Wextra -Werror)
        add_test(NAME SessionRestartRuntime COMMAND test_session_restart_runtime)
        set_tests_properties(SessionRestartRuntime PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        add_test(NAME SessionRestoreDeadline COMMAND "${CMAKE_COMMAND}"
            "-DSESSION_TEST_PROGRAM=$<TARGET_FILE:test_session_restart_runtime>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test_restore_deadline.cmake")
        set_tests_properties(SessionRestoreDeadline PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE)
    endif()
endif()
