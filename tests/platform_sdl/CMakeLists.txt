# tests/platform_sdl -- Phase 7 stage 7A (PHASE7_PLAN section 4.4): the AudioPullRing
# contract (arithmetic, order, underrun, overrun, no-drift, SPSC) and the SdlAudioSink
# over SDL's dummy audio driver. The directory name flattens engine/platform/sdl/
# (finding P7-TESTS-PLATFORM-SDL-DIR).
function(ps2r_platform_sdl_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_platform_sdl)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120)
endfunction()

ps2r_platform_sdl_test(test_audio_pull_ring AudioPullRing)
ps2r_platform_sdl_test(test_audio_sink SdlAudioSinkDummy)
