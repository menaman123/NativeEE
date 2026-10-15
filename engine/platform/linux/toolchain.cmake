# engine/platform/linux/toolchain.cmake -- the Linux target's toolchain file
# (ARCH section 16: one per target folder), named by the linux-* presets.
#
# The only setting: GNU ld and lld walk static archives ONCE, in command-line
# order, while Apple's ld resolves across all of them. The tree's archives are
# mutually dependent by design -- the engine's EE seams (engine/ee/cpu/seams.cpp,
# dispatch_table.cpp, shadow_call_stack.cpp) reference the generated code's
# providers in ps2r_title_recomp, which links ps2r_ee back -- so every executable
# link puts its libraries in one --start-group/--end-group: the Linux spelling of
# what the macOS link already does. Resolution only; no code changes.
# Set as a NORMAL variable too: a build directory configured before this rule existed keeps
# its cache, and CMakeCXXInformation's `if(NOT CMAKE_CXX_LINK_EXECUTABLE)` default would win
# over an absent cache entry (seen on the GCC lane: every test link failed with undefined
# references from libps2r_ee.a into libps2r_title_recomp.a listed earlier on the line).
set(_ps2r_link_group_rule
    "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
set(CMAKE_CXX_LINK_EXECUTABLE "${_ps2r_link_group_rule}")
set(CMAKE_CXX_LINK_EXECUTABLE "${_ps2r_link_group_rule}"
    CACHE STRING "Linux executable link rule: archives resolved as one group (see engine/platform/linux/toolchain.cmake)" FORCE)
