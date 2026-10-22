# tests/platform -- the host shim header's contract (engine/platform/host/platform.h): the
# same executable runs on the POSIX arm here and on the Windows arm in CI; the two arms are
# identical exactly when both runs are green.
set(PS2R_TEST_SCRATCH_PLATFORM "${CMAKE_BINARY_DIR}/test_scratch/platform")
file(MAKE_DIRECTORY "${PS2R_TEST_SCRATCH_PLATFORM}")

add_executable(test_host_platform test_host_platform.cpp)
target_link_libraries(test_host_platform PRIVATE ps2r_platform)
target_compile_options(test_host_platform PRIVATE -Wall -Wextra -Werror)
add_test(NAME PlatformHostShims COMMAND test_host_platform ${PS2R_TEST_SCRATCH_PLATFORM})
set_tests_properties(PlatformHostShims PROPERTIES TIMEOUT 120)

# PlatformClangClShimContract (Charter 2 B2/B3): tools/build/clang_cl_shim.py is the compiler
# launcher of the windows presets (clang-cl reads `-Wall` as cl's /Wall = -Weverything and drops
# `-ffp-contract=off` as an unknown argument). The contract: the mapping table's selftest, then
# every per-target flag set spelled in the tree's CMake files compiles through the mapping in a
# clang's cl driver mode for the Windows target (Apple clang / Linux clang / clang-cl all have it)
# with no "unknown argument"/"argument unused" line. SKIP 77 without such a clang.
add_test(NAME PlatformClangClShimContract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/clang_cl_contract.py --tree ${CMAKE_SOURCE_DIR}
            --clang ${CMAKE_CXX_COMPILER} --scratch ${PS2R_TEST_SCRATCH_PLATFORM}/clang_cl)
set_tests_properties(PlatformClangClShimContract PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

# PlatformWindowsCrossSmoke (Charter 2 B2): tools/build/win_cross_smoke.py compiles the shim
# header, every engine/core TU, the host arm with host_windows.cpp swapped in, the Windows
# target folder and one recompiled shard for x86_64-pc-windows-msvc against an `xwin splat` of
# the MSVC CRT + Windows SDK headers -- the Windows-only compile errors before CI. Needs the
# splat at $PS2R_XWIN_ROOT and a clang with the Windows target (PS2R_WIN_CROSS_CLANG, default
# Homebrew LLVM's clang++; Apple clang has the target too); SKIP 77 without them.
set(PS2R_WIN_CROSS_CLANG "/opt/homebrew/opt/llvm/bin/clang++" CACHE FILEPATH
    "clang++ used by PlatformWindowsCrossSmoke for the x86_64-pc-windows-msvc target")
add_test(NAME PlatformWindowsCrossSmoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build/win_cross_smoke.py --skip-if-missing
            --build ${CMAKE_BINARY_DIR} --clang ${PS2R_WIN_CROSS_CLANG}
            --out ${PS2R_TEST_SCRATCH_PLATFORM}/win_cross_smoke -j 4)
set_tests_properties(PlatformWindowsCrossSmoke PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# PlatformGlWindowsHeaderCurrent: the Windows arm's GL header + entry-point table
# (engine/platform/host/gl_windows.{h,cpp}) are GENERATED from the GL names the tree uses
# (tools/build/gen_gl_windows.py) and committed. A new glFoo( call or GL_FOO token anywhere a TU
# includes PS2R_GL_HEADER makes them stale -> this test fails with the regenerate command, so the
# Windows build never discovers the missing name first. SKIP 77 without a registry header
# (the macOS SDK's gl3.h or /usr/include/GL/glcorearb.h).
add_test(NAME PlatformGlWindowsHeaderCurrent
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build/gen_gl_windows.py --tree ${CMAKE_SOURCE_DIR}
            --check --skip-if-missing)
set_tests_properties(PlatformGlWindowsHeaderCurrent PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
