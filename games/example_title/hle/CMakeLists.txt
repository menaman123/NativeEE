# games/example_title/hle -- the title's hand-written HLE and its TitleProfile.
#
# The presence of THIS file is what marks a directory under games/ as a title: the root
# CMakeLists discovers titles by looking for games/*/hle/CMakeLists.txt.
#
# A real title grows this target: the native replacements for its EE functions, its loader
# sync, its graphics / audio / FMV / streaming services, its workarounds -- everything the
# engine dispatches to instead of running a hardware model. See README.md ("Per-title HLE")
# for what that work is and docs/design/HLE_COVERAGE.md for tracking it.
add_library(ps2r_title_hle STATIC
    title_profile.cpp
)
target_include_directories(ps2r_title_hle PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_title_hle PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_title_hle PUBLIC ps2r_title_api)   # the TitleProfile contract
