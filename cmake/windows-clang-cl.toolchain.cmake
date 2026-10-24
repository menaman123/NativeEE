# cmake/windows-clang-cl.toolchain.cmake -- the Windows lane's toolchain file.
#
# Loaded ONLY by the windows-clang-cl-* presets in CMakePresets.json (`cmake --preset windows-clang-cl-release`); the
# macOS and Linux configurations never read it. It is also what a port (a title repository that keeps this engine as a
# submodule, e.g. sly2recomp: `cmake -S engine --preset windows-clang-cl-release -DPS2R_GAMES_DIR=...`) forwards, so
# the engine's archives and the title's final link are built by the same front end with the same flag spelling and the
# same vcpkg libraries -- the shape of the engine's own `windows-release` preset (CMakePresets.json), reused rather
# than restated. [Copied verbatim from TwinsanityEvolved/cmake/windows-clang-cl.toolchain.cmake (234718f); only the
# shim's path (this tree's tools/build/) and the message prefix differ.]
#
#   compiler   clang-cl (LLVM's cl-compatible driver: VS 2022 Build Tools' "C++ Clang tools for Windows", or an LLVM
#              release on PATH). cl.exe is NOT a lane: the recompiled TUs' arithmetic contract needs -fwrapv /
#              -ffp-contract=off (the engine's windows-msvc-dev is compile-check only).
#   launcher   tools/build/clang_cl_shim.py -- the trees say `-Wall -Wextra -O2 -ffp-contract=off ...` (GNU
#              spelling); the shim rewrites each flag for the cl front end (`-Wall` -> `/clang:-Wall`, `-O2` -> `/O2`,
#              `-f*` -> `/clang:-f*`) so that it keeps its meaning instead of being "unknown argument ignored". Needs
#              `python` on PATH (python.org / winget Python 3.8+; `python3` is not a Windows name).
#   libraries  vcpkg in manifest mode (vcpkg.json: sdl2, opengl-registry, ffmpeg[avcodec,swscale]) through its
#              toolchain file, chain-loaded from here -- VCPKG_ROOT must point at a vcpkg checkout
#              (`git clone https://github.com/microsoft/vcpkg; .\bootstrap-vcpkg.bat`). Triplet x64-windows (dynamic:
#              the DLLs are copied beside the .exe by VCPKG_APPLOCAL_DEPS; tools/ci/release_dist.ps1 ships them).
#
# Rejected on sight: a non-Windows host (the macOS build must never pick this file up by accident; the macOS->Windows
# CROSS build is engine/platform/windows/toolchain_cross_macos.cmake, a different file on purpose).
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "nativeee: cmake/windows-clang-cl.toolchain.cmake is the Windows lane's toolchain; on this host configure without a preset (README) or use a macOS/Linux shape")
endif()
include_guard(GLOBAL)

# A native build: CMAKE_SYSTEM_NAME is left to CMake (setting it here would mark the build as cross-compiling and change
# how try_run / vcpkg behave).

# The front end. A full path on PATH resolution is left to CMake (it records the resolved path in the cache).
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang-cl)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang-cl)
endif()

# The shim: this tree's copy (TwinsanityEvolved reaches the same file through its vendored engine/ or TE_ENGINE_DIR).
get_filename_component(_ps2r_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_ps2r_shim "${_ps2r_root}/tools/build/clang_cl_shim.py")
if(NOT EXISTS "${_ps2r_shim}")
    message(FATAL_ERROR "nativeee: ${_ps2r_shim} missing -- the clone is incomplete (the engine tree carries the clang-cl shim)")
endif()
set(CMAKE_C_COMPILER_LAUNCHER "python;${_ps2r_shim}")
set(CMAKE_CXX_COMPILER_LAUNCHER "python;${_ps2r_shim}")

# vcpkg, chain-loaded. (Setting CMAKE_TOOLCHAIN_FILE to vcpkg's file and VCPKG_CHAINLOAD_TOOLCHAIN_FILE to this one would
# invert the order; including vcpkg's file from here keeps ONE toolchain file to forward to a port.)
if(NOT DEFINED ENV{VCPKG_ROOT} OR NOT EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "nativeee: VCPKG_ROOT must point at a vcpkg checkout (git clone https://github.com/microsoft/vcpkg; .\\bootstrap-vcpkg.bat)")
endif()
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet (the Windows lane: dynamic x64)")
endif()
if(NOT DEFINED VCPKG_APPLOCAL_DEPS)
    set(VCPKG_APPLOCAL_DEPS ON CACHE BOOL "copy the vcpkg DLLs beside every built executable")
endif()
include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
