# tests/input -- Phase 7 stage 7B (PHASE7_PLAN section 4.5): the InputMapper port
# of the OLD nine cases, the padlog replay contract (mapper -> wire -> InputLog
# recorder byte-identity) and the bindings INI round trip. Host-only: no SDL_Init,
# no window; SDL2 is linked for SDL_GetScancodeFromName / SDL_GetScancodeName and
# the SDL_SCANCODE_* / SDL_CONTROLLER_* constants.
function(ps2r_input_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_platform_sdl)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120)
endfunction()

ps2r_input_test(test_input_mapper InputMapper)
ps2r_input_test(test_padlog_replay PadlogReplay)
ps2r_input_test(test_bindings_roundtrip BindingsRoundtrip
    ${CMAKE_SOURCE_DIR}/engine/platform/sdl/input/default_bindings.ini)
