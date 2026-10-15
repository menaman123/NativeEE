add_library(ps2r_session_clock STATIC ../host/session_clock.cpp)
target_include_directories(ps2r_session_clock PUBLIC "${PS2R_PRODUCT_ROOT}")
target_compile_features(ps2r_session_clock PUBLIC cxx_std_17)
target_compile_options(ps2r_session_clock PRIVATE -Wall -Wextra -Werror)
if(TARGET ps2r_ee)
    target_link_libraries(ps2r_session_clock PRIVATE ps2r_ee)
else()
    target_sources(ps2r_session_clock PRIVATE
        "${PS2R_PRODUCT_ROOT}/engine/ee/timing/emu_domain.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/timing/regime_detector.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/timing/tick_source.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/timing/tick_sync.cpp")
    target_link_libraries(ps2r_session_clock PRIVATE ps2r_checkpoint_support)
endif()
if(PS2R_PRODUCT_TESTS)
    # Product owner proofs must not pull generated title bodies through the
    # production clock's ps2r_ee dependency in a full repository build.
    set(product_clock_support ps2r_session_clock)
    if(TARGET ps2r_ee)
        add_library(ps2r_product_clock STATIC ../host/session_clock.cpp
            "${PS2R_PRODUCT_ROOT}/engine/ee/timing/emu_domain.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/timing/regime_detector.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/timing/tick_source.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/timing/tick_sync.cpp")
        target_link_libraries(ps2r_product_clock PUBLIC ${continuation_support})
        target_compile_options(ps2r_product_clock PRIVATE -Wall -Wextra -Werror)
        set(product_clock_support ps2r_product_clock)
    endif()
    add_executable(test_session_clock "${PS2R_PRODUCT_ROOT}/tests/app/test_session_clock.cpp")
    target_link_libraries(test_session_clock PRIVATE ps2r_session_clock)
    target_compile_options(test_session_clock PRIVATE -Wall -Wextra -Werror)
    add_test(NAME SessionClock COMMAND test_session_clock)
    set_tests_properties(SessionClock PROPERTIES TIMEOUT 20)
    add_executable(test_checkpoint_clock "${PS2R_PRODUCT_ROOT}/tests/app/test_checkpoint_clock.cpp"
        ../host/checkpoint_capture.cpp)
    target_link_libraries(test_checkpoint_clock PRIVATE ps2r_session_clock)
    if(TARGET ps2r_ee)
        target_link_libraries(test_checkpoint_clock PRIVATE ps2r_ee)
    else()
        target_sources(test_checkpoint_clock PRIVATE
            "${PS2R_PRODUCT_ROOT}/engine/core/hash/hash.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/core/verify/memory_checkpointer.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/core/savestate/quiescence.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/mem/main_memory.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/mem/scratchpad.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/savestate/memory_sections.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/ee/savestate/state_restorer.cpp")
    endif()
    target_compile_options(test_checkpoint_clock PRIVATE -Wall -Wextra -Werror)
    add_test(NAME CheckpointClock COMMAND test_checkpoint_clock)
    set_tests_properties(CheckpointClock PROPERTIES TIMEOUT 30)
endif()
if(PS2R_PRODUCT_TESTS)
    # The clock contract's census (rule 5): game_hz / field_hz / render_fps per 1 s wall window.
    add_executable(test_clock_census "${PS2R_PRODUCT_ROOT}/tests/app/test_clock_census.cpp" ../host/clock_census.cpp)
    target_include_directories(test_clock_census PRIVATE "${PS2R_PRODUCT_ROOT}")
    target_compile_features(test_clock_census PRIVATE cxx_std_17)
    target_compile_options(test_clock_census PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_clock_census PRIVATE Threads::Threads)
    add_test(NAME ClockCensus COMMAND test_clock_census)
    set_tests_properties(ClockCensus PROPERTIES TIMEOUT 20)
    # The display-rate camera transform source (Charter 2 A3): inverse translation,
    # boundary-promoted samples, phase interpolation, duplicates, the trace.
    add_executable(test_camera_track "${PS2R_PRODUCT_ROOT}/tests/app/test_camera_track.cpp" ../host/camera_track.cpp)
    target_include_directories(test_camera_track PRIVATE "${PS2R_PRODUCT_ROOT}")
    target_compile_features(test_camera_track PRIVATE cxx_std_17)
    target_compile_options(test_camera_track PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_camera_track PRIVATE Threads::Threads)
    add_test(NAME CameraTrack COMMAND test_camera_track)
    set_tests_properties(CameraTrack PROPERTIES TIMEOUT 20)
endif()
