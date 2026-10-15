if(PS2R_PRODUCT_TESTS AND PS2R_PRODUCT_GUI)
    add_executable(test_session_audio "${PS2R_PRODUCT_ROOT}/tests/app/test_session_audio.cpp"
        ../host/session_audio.cpp
        "${PS2R_PRODUCT_ROOT}/engine/platform/sdl/audio_pull_ring.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/platform/sdl/sdl_audio_sink.cpp")
    target_include_directories(test_session_audio PRIVATE "${PS2R_PRODUCT_ROOT}")
    target_compile_features(test_session_audio PRIVATE cxx_std_17)
    target_compile_options(test_session_audio PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_session_audio PRIVATE SDL2::SDL2 Threads::Threads)
    add_test(NAME SessionAudio COMMAND test_session_audio)
    set_tests_properties(SessionAudio PROPERTIES TIMEOUT 10)
endif()
