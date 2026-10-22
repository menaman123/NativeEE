# engine/platform/windows/toolchain_cross_macos.cmake -- build the WHOLE tree for x86-64 Windows
# from a macOS (or Linux) host: the same compiler front end (clang-cl), the same flag mapping
# (tools/build/clang_cl_shim.py) and the same linker (lld-link) as the windows-release preset on
# a Windows runner -- only the host OS differs. The result (nativeee.exe, gs_replay.exe, the tool
# .exes, the test .exes) is linked, never run here: running is the Windows host's job
# (docs/design/PLATFORMS.md section 4; the WINDOWS_PERF_RUNBOOK).
#
#   cmake -S <tree> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=<tree>/engine/platform/windows/toolchain_cross_macos.cmake \
#         -DPS2R_WINSYSROOT=<dir> -DPS2R_WIN_SDL2_ROOT=<dir> -DPS2R_WIN_FFMPEG_ROOT=<dir> \
#         -DPS2R_HOST_TOOLS_BUILD=<native build of the same tree> \
#         -DNATIVEEE_DISC=<the user's disc image> -DPS2R_GAMES_DIR=<games tree> ...
#
# Inputs (cache variables; each falls back to the environment variable of the same name):
#   PS2R_WINSYSROOT        a Visual Studio-shaped root: VC/Tools/MSVC/<ver>/{include,lib/x64} and
#                          "Windows Kits/10/{Include,Lib}/<ver>/..." -- what clang-cl's and lld-link's
#                          /winsysroot expect. From an `xwin splat` (MSVC 14.44 CRT + SDK 10.0.26100,
#                          the headers clang-cl sees on the runner) it is a symlink tree:
#                            VC/Tools/MSVC/14.44.35207/include -> splat/crt/include
#                            VC/Tools/MSVC/14.44.35207/lib/x64 -> splat/crt/lib/x86_64
#                            Windows Kits/10/Include/10.0.26100.0/{ucrt,um,shared,winrt,cppwinrt} -> splat/sdk/include/<d>
#                            Windows Kits/10/Lib/10.0.26100.0/{ucrt,um}/x64 -> splat/sdk/lib/<d>/x86_64
#                          (or `xwin splat --use-winsysroot-style`, which writes this layout directly).
#   PS2R_WIN_LLVM_BIN      the LLVM bin dir holding clang-cl, llvm-lib, llvm-rc, llvm-mt
#                          (default /opt/homebrew/opt/llvm/bin; LLVM >= 18).
#   PS2R_WIN_LLD_LINK      the COFF linker (default ${PS2R_WIN_LLVM_BIN}/lld-link, else `lld-link` on
#                          PATH -- e.g. a one-line wrapper over `python3 -m ziglang lld-link`, the
#                          ziglang wheel bundles LLD; Homebrew's llvm formula ships no lld).
#   PS2R_WIN_SDL2_ROOT     SDL2-devel-<ver>-VC unpacked (include/, lib/x64/SDL2.lib, cmake/) -- the
#                          same binary vcpkg's sdl2 port builds; find_package(SDL2 CONFIG) reads its cmake/.
#   PS2R_WIN_FFMPEG_ROOT   a Windows FFmpeg shared build (include/, lib/{avcodec,avutil,swscale}.lib,
#                          bin/*.dll) -- cmake/FindFFmpeg.cmake finds it through CMAKE_FIND_ROOT_PATH.
#   PS2R_HOST_TOOLS_BUILD  a configured + built NATIVE build of this tree: the build-time tools
#                          (disc_extract, elf_embedded, the recompilers) run from there through
#                          CMAKE_CROSSCOMPILING_EMULATOR = tools/build/win_cross_emulator.py, their
#                          Windows .exes are still built. Required when a title input is given.
#
# Everything else is the windows-release preset verbatim: Ninja, Release, PS2R_DIAGNOSTICS OFF,
# the clang_cl_shim launcher, the root CMakeLists' WIN32 definitions.
cmake_minimum_required(VERSION 3.21)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_CROSSCOMPILING TRUE)
# The toolchain is re-read by every try_compile project with its own cache: carry the inputs over.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PS2R_WINSYSROOT PS2R_WIN_LLVM_BIN PS2R_WIN_LLD_LINK
     PS2R_WIN_SDL2_ROOT PS2R_WIN_FFMPEG_ROOT PS2R_HOST_TOOLS_BUILD)

function(_ps2r_cross_input var default doc)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        if(DEFINED ENV{${var}} AND NOT "$ENV{${var}}" STREQUAL "")
            set(${var} "$ENV{${var}}" CACHE PATH "${doc}")
        else()
            set(${var} "${default}" CACHE PATH "${doc}")
        endif()
    endif()
endfunction()

_ps2r_cross_input(PS2R_WIN_LLVM_BIN "/opt/homebrew/opt/llvm/bin" "LLVM bin dir with clang-cl, llvm-lib, llvm-rc, llvm-mt")
_ps2r_cross_input(PS2R_WINSYSROOT "" "VS-shaped sysroot (VC/Tools/MSVC/<ver>, Windows Kits/10) for clang-cl /winsysroot")
_ps2r_cross_input(PS2R_WIN_SDL2_ROOT "" "SDL2-devel-<ver>-VC root (include/, lib/x64/, cmake/)")
_ps2r_cross_input(PS2R_WIN_FFMPEG_ROOT "" "Windows FFmpeg shared build root (include/, lib/*.lib, bin/*.dll)")
_ps2r_cross_input(PS2R_HOST_TOOLS_BUILD "" "native build dir of this tree whose tools run at build time")
if(DEFINED ENV{PS2R_WIN_LLD_LINK} AND NOT "$ENV{PS2R_WIN_LLD_LINK}" STREQUAL "" AND NOT DEFINED PS2R_WIN_LLD_LINK)
    set(PS2R_WIN_LLD_LINK "$ENV{PS2R_WIN_LLD_LINK}" CACHE FILEPATH "COFF linker (lld-link)")
endif()
if(NOT DEFINED PS2R_WIN_LLD_LINK OR "${PS2R_WIN_LLD_LINK}" STREQUAL "")
    if(EXISTS "${PS2R_WIN_LLVM_BIN}/lld-link")
        set(PS2R_WIN_LLD_LINK "${PS2R_WIN_LLVM_BIN}/lld-link" CACHE FILEPATH "COFF linker (lld-link)")
    else()
        find_program(PS2R_WIN_LLD_LINK NAMES lld-link lld-link-18 lld-link-19 lld-link-20 DOC "COFF linker (lld-link)")
    endif()
endif()

if(NOT IS_DIRECTORY "${PS2R_WINSYSROOT}/VC/Tools/MSVC")
    message(FATAL_ERROR "toolchain_cross_macos: PS2R_WINSYSROOT=${PS2R_WINSYSROOT} has no VC/Tools/MSVC -- see the header of this file")
endif()
if(NOT PS2R_WIN_LLD_LINK)
    message(FATAL_ERROR "toolchain_cross_macos: no lld-link (set PS2R_WIN_LLD_LINK, or put a lld-link on PATH)")
endif()

# --- the compiler: clang-cl for x86_64-pc-windows-msvc, flags mapped by the tree's shim ---------
set(CMAKE_C_COMPILER "${PS2R_WIN_LLVM_BIN}/clang-cl" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${PS2R_WIN_LLVM_BIN}/clang-cl" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_LINKER "${PS2R_WIN_LLD_LINK}" CACHE FILEPATH "")
set(CMAKE_AR "${PS2R_WIN_LLVM_BIN}/llvm-lib" CACHE FILEPATH "")
set(CMAKE_RC_COMPILER "${PS2R_WIN_LLVM_BIN}/llvm-rc" CACHE FILEPATH "")
set(CMAKE_MT "${PS2R_WIN_LLVM_BIN}/llvm-mt" CACHE FILEPATH "")
# The shim (CMakePresets.json windows-*: python;tools/build/clang_cl_shim.py) -- the tree's GNU
# per-target flags (-Wall -Wextra -Werror, -ffp-contract=off, -fwrapv ...) spelled for clang-cl.
get_filename_component(_ps2r_tree "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_program(_ps2r_py3 NAMES python3 python REQUIRED)
set(CMAKE_C_COMPILER_LAUNCHER "${_ps2r_py3};${_ps2r_tree}/tools/build/clang_cl_shim.py" CACHE STRING "")
set(CMAKE_CXX_COMPILER_LAUNCHER "${_ps2r_py3};${_ps2r_tree}/tools/build/clang_cl_shim.py" CACHE STRING "")

# /winsysroot: the MSVC CRT + Windows SDK headers for every compile, the import libraries for
# every link (clang-cl forwards the lib dirs; lld-link takes its own spelling).
# (set, not appended: this file is read once per try_compile project too.)
set(CMAKE_C_FLAGS_INIT "/winsysroot \"${PS2R_WINSYSROOT}\"")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot \"${PS2R_WINSYSROOT}\"")
file(GLOB _ps2r_sdk_inc LIST_DIRECTORIES true "${PS2R_WINSYSROOT}/Windows Kits/10/Include/*")
list(GET _ps2r_sdk_inc 0 _ps2r_sdk_inc)
set(CMAKE_RC_FLAGS_INIT "/I \"${_ps2r_sdk_inc}/um\" /I \"${_ps2r_sdk_inc}/shared\"")
foreach(_kind EXE SHARED MODULE)
    set(CMAKE_${_kind}_LINKER_FLAGS_INIT "/winsysroot:\"${PS2R_WINSYSROOT}\"")
endforeach()
# An `xwin splat` carries the release CRT only (no msvcrtd / vcruntimed): every configuration
# links /MD, and CMake's compiler probes build as Release.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "")
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# --- dependencies: only the Windows binaries, never the host's -----------------------------------
set(CMAKE_FIND_ROOT_PATH "${PS2R_WIN_SDL2_ROOT}" "${PS2R_WIN_FFMPEG_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)    # python3, the host tools: the host's
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
if(PS2R_WIN_SDL2_ROOT AND NOT DEFINED SDL2_DIR)
    set(SDL2_DIR "${PS2R_WIN_SDL2_ROOT}/cmake" CACHE PATH "sdl2-config.cmake of the SDL2 VC development package")
endif()
set(CMAKE_FIND_LIBRARY_SUFFIXES .lib)

# --- build-time tools: the host twins run (tools/build/win_cross_emulator.py) ---------------------
if(PS2R_HOST_TOOLS_BUILD)
    set(CMAKE_CROSSCOMPILING_EMULATOR
        "${CMAKE_COMMAND};-E;env;PS2R_HOST_TOOLS_BUILD=${PS2R_HOST_TOOLS_BUILD};${_ps2r_py3};${_ps2r_tree}/tools/build/win_cross_emulator.py"
        CACHE STRING "runs each build-time tool's host twin from PS2R_HOST_TOOLS_BUILD")
endif()

# The windows-release preset's own choices.
set(PS2R_DIAGNOSTICS OFF CACHE BOOL "")
set(PS2R_SIMD OFF CACHE BOOL "")   # NEON only (the root CMakeLists forces it off on x86-64 anyway)
