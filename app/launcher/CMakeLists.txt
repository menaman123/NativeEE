# app/launcher -- the engine's launcher: one application a player downloads, opens, picks the game in and presses
# Play. Qt 6 Widgets. Title-neutral: everything about a game comes from the
# manifest a title ships (`title.json`, read at run time); nothing of a title is compiled in.
#
# Built in two ways:
#   * alone:  cmake -S engine/app/launcher -B build-launcher -DCMAKE_PREFIX_PATH=<Qt 6 prefix> && cmake --build build-launcher
#   * by the engine root with -DPS2R_LAUNCHER=ON (default OFF: no existing target, option or default changes when it is off).
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
    launcher_theme.cpp launcher_style.cpp title_manifest.cpp app_data.cpp iso_fingerprint.cpp
    disc_scanner.cpp game_library.cpp ini_settings.cpp download_pins.cpp process_runner.cpp
    installer.cpp build_runner.cpp game_process.cpp host_tools.cpp
    launcher_app.cpp main_window.cpp sidebar.cpp page_header.cpp flat_button.cpp segmented_control.cpp
    switch_control.cpp folder_row.cpp generated_artwork.cpp game_card.cpp games_page.cpp empty_state.cpp
    settings_row.cpp settings_page.cpp target_card.cpp targets_page.cpp about_page.cpp
    checklist_row.cpp progress_panel.cpp details_sheet.cpp sheet_overlay.cpp first_run_sheet.cpp
    launcher_theme.h launcher_style.h title_manifest.h app_data.h iso_fingerprint.h disc_scanner.h game_library.h
    ini_settings.h download_pins.h process_runner.h installer.h build_runner.h game_process.h host_tools.h
    launcher_app.h main_window.h sidebar.h page_header.h flat_button.h segmented_control.h switch_control.h
    folder_row.h generated_artwork.h game_card.h games_page.h empty_state.h settings_row.h settings_page.h
    target_card.h targets_page.h about_page.h checklist_row.h progress_panel.h details_sheet.h sheet_overlay.h
    first_run_sheet.h)
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

# The tests: the unit test of the models (manifest, fingerprint, ini, pins, ninja progress) and the cold-run driver a
# judge points at a manifest (tests/app/launcher/). The driver is a Qt Test program that opens the real window and
# presses its buttons; it needs a manifest, a disc image and the network, so it is registered only when
# PS2R_LAUNCHER_COLD_MANIFEST names one.
add_executable(test_launcher_models "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_models.cpp")
target_link_libraries(test_launcher_models PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_models PRIVATE ${PS2R_LAUNCHER_WARNINGS})
add_test(NAME LauncherModels COMMAND test_launcher_models -platform offscreen)
set_tests_properties(LauncherModels PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_launcher_cold "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_cold.cpp")
target_link_libraries(test_launcher_cold PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_cold PRIVATE ${PS2R_LAUNCHER_WARNINGS})

add_executable(test_launcher_settings "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_settings.cpp")
target_link_libraries(test_launcher_settings PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_settings PRIVATE ${PS2R_LAUNCHER_WARNINGS})

add_executable(test_launcher_targets "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_targets.cpp")
target_link_libraries(test_launcher_targets PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_targets PRIVATE ${PS2R_LAUNCHER_WARNINGS})

add_executable(test_launcher_screens "${PS2R_LAUNCHER_ROOT}/tests/app/launcher/test_launcher_screens.cpp")
target_link_libraries(test_launcher_screens PRIVATE ps2r_launcher_lib Qt6::Test)
target_compile_options(test_launcher_screens PRIVATE ${PS2R_LAUNCHER_WARNINGS})
add_test(NAME LauncherScreens COMMAND test_launcher_screens)
set_tests_properties(LauncherScreens PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(Python3_EXECUTABLE)
    add_test(NAME LauncherOneClass
        COMMAND ${Python3_EXECUTABLE} ${PS2R_LAUNCHER_ROOT}/tests/lint/lint_one_class.py --root ${PS2R_LAUNCHER_ROOT} --scan app/launcher tests/app/launcher)
endif()
