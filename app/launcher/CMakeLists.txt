# app/launcher -- the engine's launcher: one application a player downloads, opens, picks the game in and presses
# Play. Qt 6 Widgets. Title-neutral: everything about a game comes from the manifest a title ships (`launcher/title.json`,
# read at run time); nothing of a title is compiled in. The step between the disc and Play is behind the PrepareRunner
# interface (prepare_runner.h); the title's prepare pipeline plugs in there.
#
# Built in two ways:
#   * alone:  cmake -S engine/app/launcher -B build-launcher -DCMAKE_PREFIX_PATH=<Qt 6 prefix> && cmake --build build-launcher
#   * by the engine root with PS2R_PRODUCT_LAUNCHER (default ON when a Qt 6 is found on CMAKE_PREFIX_PATH).
cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NativeEELauncher LANGUAGES CXX)
    enable_testing()
    find_package(Python3 3.8 COMPONENTS Interpreter)
endif()
get_filename_component(PS2R_LAUNCHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)
# cl.exe (the Qt MSVC kit's compiler) reads -Wall/-Wextra as malformed /W options: its own spelling there.
if(MSVC)
    set(PS2R_LAUNCHER_WARNINGS /W4 /WX)
else()
    set(PS2R_LAUNCHER_WARNINGS -Wall -Wextra -Werror)
endif()

# The launcher's classes as a library (the application and the tests link it).
add_library(ps2r_launcher_lib STATIC
    launcher_theme.cpp launcher_style.cpp title_manifest.cpp app_data.cpp iso_fingerprint.cpp disc_identity.cpp disc_identifier.cpp
    disc_scanner.cpp game_library.cpp ini_settings.cpp process_runner.cpp game_process.cpp game_folder.cpp
    host_platform.cpp prepare_runner.cpp fake_prepare_runner.cpp unavailable_prepare_runner.cpp save_state_list.cpp
    launcher_app.cpp main_window.cpp sidebar.cpp page_header.cpp flat_button.cpp segmented_control.cpp
    switch_control.cpp folder_row.cpp generated_artwork.cpp game_card.cpp disc_list.cpp games_page.cpp empty_state.cpp
    settings_row.cpp settings_page.cpp save_state_card.cpp save_states_page.cpp about_page.cpp
    progress_panel.cpp details_sheet.cpp sheet_overlay.cpp first_run_flow.cpp
    launcher_theme.h launcher_style.h title_manifest.h app_data.h iso_fingerprint.h disc_identity.h disc_identifier.h disc_scanner.h
    game_library.h ini_settings.h process_runner.h game_process.h game_folder.h host_platform.h prepare_runner.h
    fake_prepare_runner.h unavailable_prepare_runner.h save_state_list.h
    launcher_app.h main_window.h sidebar.h page_header.h flat_button.h segmented_control.h switch_control.h
    folder_row.h generated_artwork.h game_card.h disc_list.h games_page.h empty_state.h settings_row.h settings_page.h
    save_state_card.h save_states_page.h about_page.h progress_panel.h details_sheet.h sheet_overlay.h first_run_flow.h)
target_include_directories(ps2r_launcher_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ps2r_launcher_lib PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent)
target_compile_options(ps2r_launcher_lib PRIVATE ${PS2R_LAUNCHER_WARNINGS})
if(DEFINED PS2R_LAUNCHER_VERSION)
    target_compile_definitions(ps2r_launcher_lib PUBLIC PS2R_LAUNCHER_VERSION="${PS2R_LAUNCHER_VERSION}")
endif()

# The application. Named by the engine (ps2r_launcher); a title's packaging wraps it in a bundle carrying the
# title's name and manifest (Contents/Resources/titles/*.json on macOS, titles/*.json beside the .exe).
add_executable(ps2r_launcher MACOSX_BUNDLE main.cpp)
target_link_libraries(ps2r_launcher PRIVATE ps2r_launcher_lib)
target_compile_options(ps2r_launcher PRIVATE ${PS2R_LAUNCHER_WARNINGS})
set_target_properties(ps2r_launcher PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "NativeEE Launcher"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.nativeee.launcher"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")

# The tests: the unit test of the models (manifest, fingerprint, identity, folder, ini) and the screens test: every
# page and sheet rendered offscreen at 1024x640 with a neutral manifest and the fake prepare runner, no lint hit in any
# visible string, the PNGs written (PS2R_LAUNCHER_SCREENS_DIR, else <build>/launcher_screens/).
add_executable(test_launcher_models "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_models.cpp")
target_link_libraries(test_launcher_models PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_models PRIVATE ${PS2R_LAUNCHER_WARNINGS})
add_test(NAME LauncherModels COMMAND test_launcher_models -platform offscreen)
set_tests_properties(LauncherModels PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_launcher_screens "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_screens.cpp")
target_link_libraries(test_launcher_screens PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_screens PRIVATE ${PS2R_LAUNCHER_WARNINGS})
add_test(NAME LauncherScreens COMMAND test_launcher_screens -platform offscreen)
set_tests_properties(LauncherScreens PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PS2R_LAUNCHER_SCREENS_DIR=${CMAKE_CURRENT_BINARY_DIR}/launcher_screens")

if(Python3_EXECUTABLE)
    # LauncherLint: no codenames in app/launcher at all; no developer vocabulary, placeholder text or emoji in any
    # string literal; also over the visible strings the screens test dumped (strings.txt) when it has run.
    add_test(NAME LauncherLint
        COMMAND ${Python3_EXECUTABLE} ${PS2R_LAUNCHER_ROOT}/tools/launcher_lint.py --root ${PS2R_LAUNCHER_ROOT}
                --strings ${CMAKE_CURRENT_BINARY_DIR}/launcher_screens/strings.txt)
    add_test(NAME LauncherOneClass
        COMMAND ${Python3_EXECUTABLE} ${PS2R_LAUNCHER_ROOT}/tests/lint/lint_one_class.py --root ${PS2R_LAUNCHER_ROOT} --scan app/launcher tests/app/launcher)
endif()
