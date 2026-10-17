# This module can be built alone for product tests, or added by the app root.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SessionProduct LANGUAGES CXX)
    enable_testing()
endif()

get_filename_component(PS2R_PRODUCT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
add_library(ps2r_session_controls STATIC ../host/session_controls.cpp)
target_compile_features(ps2r_session_controls PUBLIC cxx_std_17)
target_include_directories(ps2r_session_controls PUBLIC "${PS2R_PRODUCT_ROOT}")
target_compile_options(ps2r_session_controls PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_session_controls PUBLIC Threads::Threads)

option(PS2R_PRODUCT_GUI "Build the SDL/OpenGL session menu" ON)
if(PS2R_PRODUCT_GUI)
    include(${CMAKE_SOURCE_DIR}/cmake/Sdl2Dependency.cmake)
    include(${CMAKE_SOURCE_DIR}/cmake/OpenGlDependency.cmake)
    set(PS2R_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    add_library(ps2r_imgui STATIC
        ${PS2R_IMGUI_DIR}/imgui.cpp ${PS2R_IMGUI_DIR}/imgui_draw.cpp
        ${PS2R_IMGUI_DIR}/imgui_tables.cpp ${PS2R_IMGUI_DIR}/imgui_widgets.cpp
        ${PS2R_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${PS2R_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_compile_features(ps2r_imgui PUBLIC cxx_std_17)
    target_include_directories(ps2r_imgui SYSTEM PUBLIC "${PS2R_IMGUI_DIR}")
    target_link_libraries(ps2r_imgui PRIVATE SDL2::SDL2 OpenGL::GL ${CMAKE_DL_LIBS})
    add_library(ps2r_session_gui STATIC session_gui.cpp session_panel.cpp savestate_rows.cpp
        savestate_browser.cpp sound_section.cpp display_section.cpp controls_section.cpp triage_section.cpp)
    target_link_libraries(ps2r_session_gui PUBLIC ps2r_session_controls PRIVATE ps2r_imgui SDL2::SDL2)
    target_compile_options(ps2r_session_gui PRIVATE -Wall -Wextra -Werror)
    add_library(ps2r_session_runtime STATIC ../host/session_runtime.cpp
        ../host/session_runtime_restore.cpp ../host/session_window.cpp ../host/shutdown_census.cpp)
    target_link_libraries(ps2r_session_runtime PUBLIC ps2r_session_gui ps2r_gs_aspect PRIVATE SDL2::SDL2 OpenGL::GL)
    target_compile_options(ps2r_session_runtime PRIVATE -Wall -Wextra -Werror)
endif()

option(PS2R_PRODUCT_TESTS "Build isolated session product tests" ON)
if(PS2R_PRODUCT_TESTS)
    add_executable(test_session_controls "${PS2R_PRODUCT_ROOT}/tests/app/test_session_controls.cpp")
    target_compile_options(test_session_controls PRIVATE -Wall -Wextra -Werror)
    add_test(NAME SessionControls COMMAND test_session_controls)
    set_tests_properties(SessionControls PROPERTIES TIMEOUT 20)
    if(PS2R_PRODUCT_GUI)
        target_compile_definitions(test_session_controls PRIVATE PS2R_PRODUCT_GUI_TESTS=1)
        # SDL2's headers only: the archive reaches the link line once, through ps2r_session_runtime
        # (a second, direct mention is a duplicate archive under the static macos-dist preset).
        target_link_libraries(test_session_controls PRIVATE OpenGL::GL)
        target_include_directories(test_session_controls PRIVATE $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>)
        add_test(NAME SessionGui COMMAND test_session_controls --gui)
        set_tests_properties(SessionGui PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        add_test(NAME SessionRuntime COMMAND test_session_controls --runtime)
        set_tests_properties(SessionRuntime PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        # The s5 F5: a real SDL_KEYDOWN(F5) pushed into SDL's queue reaches SessionControls through
        # the production pump (windowID 0 = no focus window recorded, the session window's id, and
        # the harness's InjectFunctionKey); red before the SessionGui window-id fix.
        add_test(NAME SessionHotkeyRoute COMMAND test_session_controls --hotkey-route)
        set_tests_properties(SessionHotkeyRoute PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        add_test(NAME SessionShutdown COMMAND "${CMAKE_COMMAND}"
            "-DSESSION_TEST_PROGRAM=$<TARGET_FILE:test_session_controls>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/test_shutdown.cmake")
        set_tests_properties(SessionShutdown PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
        include(input_tests.cmake)
        target_link_libraries(test_session_controls PRIVATE ps2r_session_runtime)
        # Pure panel logic: reproduction-file listing/decoding and the triage report.
        add_executable(test_session_panels "${PS2R_PRODUCT_ROOT}/tests/app/test_session_panels.cpp")
        target_compile_options(test_session_panels PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(test_session_panels PRIVATE ps2r_session_gui)
        add_test(NAME SessionPanels COMMAND test_session_panels "${CMAKE_CURRENT_BINARY_DIR}/session_panels_scratch")
        set_tests_properties(SessionPanels PROPERTIES TIMEOUT 30)
    else()
        target_link_libraries(test_session_controls PRIVATE ps2r_session_controls)
    endif()
endif()

if(TARGET ps2r_session_runtime)
    if(TARGET ps2r_core)
        target_link_libraries(ps2r_session_runtime PRIVATE ps2r_core)
    elseif(NOT TARGET ps2r_product_input_support)
        target_sources(ps2r_session_runtime PRIVATE "${PS2R_PRODUCT_ROOT}/engine/core/threads/park.cpp")
    endif()
endif()

include(checkpoint.cmake)
include(restart.cmake)
include(clock.cmake)
include(audio.cmake)
include(files.cmake)
# continuation_tests / input_save / restore_tests registered suites over ONE title's
# recompiled shards, save-storage HLE and restore owners; they move with the title module.

# The product's savestate slots (F5 / --set harness.save_at_frame write,
# F8 / --load-state read): pure path/meta/preflight/relaunch-argv pins.
# Needs the real Config and savestate codec (ps2r_core): app-root builds only.
if(PS2R_PRODUCT_TESTS AND TARGET ps2r_core)
    add_library(ps2r_savestate_slots STATIC ../host/savestate_slots.cpp)
    target_include_directories(ps2r_savestate_slots PUBLIC "${PS2R_PRODUCT_ROOT}")
    target_compile_features(ps2r_savestate_slots PUBLIC cxx_std_17)
    target_compile_options(ps2r_savestate_slots PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(ps2r_savestate_slots PUBLIC ps2r_core ps2r_platform)   # host::ExecutablePath
    add_executable(test_savestate_slots "${PS2R_PRODUCT_ROOT}/tests/app/test_savestate_slots.cpp")
    target_compile_options(test_savestate_slots PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_savestate_slots PRIVATE ps2r_savestate_slots)
    add_test(NAME SavestateSlots COMMAND test_savestate_slots)
    set_tests_properties(SavestateSlots PROPERTIES TIMEOUT 30)
endif()
