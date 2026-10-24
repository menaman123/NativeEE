# tools/prepare -- "Preparing your game" (decision D-A): ps2r_prepare, the library the launcher links, and
# nativeee-prepare, the headless CLI over it (the judge drives the CLI; the launcher the library).
#
# ps2r_prepare links only the platform's host arm (the process spawn) and core/hash (SHA-256): no
# engine seams, so it builds with or without a title and before any generated code exists. The JSON
# reader is the in-tree nlohmann copy (tools/recompiler_v2/include/json.hpp).
add_library(ps2r_prepare STATIC
    prepare.cpp)
target_include_directories(ps2r_prepare PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_prepare PRIVATE -Wall -Wextra -Werror)
target_compile_features(ps2r_prepare PUBLIC cxx_std_17)
target_link_libraries(ps2r_prepare PUBLIC ps2r_host)
target_sources(ps2r_prepare PRIVATE ${CMAKE_SOURCE_DIR}/engine/core/hash/sha256.cpp)
find_package(Threads REQUIRED)
target_link_libraries(ps2r_prepare PUBLIC Threads::Threads)

add_executable(nativeee-prepare main_prepare.cpp)
target_include_directories(nativeee-prepare PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(nativeee-prepare PRIVATE -Wall -Wextra -Werror)
target_link_libraries(nativeee-prepare PRIVATE ps2r_prepare)

# The UI-string lint: every string a player sees is in ui_strings.h and uses no developer vocabulary.
add_test(NAME PrepareUiStringsLint
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/lint_ui_strings.py ${CMAKE_CURRENT_SOURCE_DIR}/ui_strings.h)
set_tests_properties(PrepareUiStringsLint PROPERTIES TIMEOUT 30 LABELS "lint")
# Substitute() + IsPrepared() + the Impl's manifest reader on a synthetic tree.
add_executable(test_prepare_units test_prepare_units.cpp)
target_include_directories(test_prepare_units PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_prepare_units PRIVATE ps2r_prepare)
add_test(NAME PrepareUnits COMMAND test_prepare_units ${CMAKE_BINARY_DIR}/test_scratch/prepare_units)
set_tests_properties(PrepareUnits PROPERTIES TIMEOUT 60 LABELS "prepare")
