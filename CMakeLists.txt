cmake_minimum_required(VERSION 3.21)
project(nativeee LANGUAGES CXX)
# Static libraries reached both directly and through a PUBLIC dependency (ps2r_ee,
# ps2r_sif through ps2r_pad / ps2r_title_hle) would otherwise be repeated on the
# link line, which Apple's linker warns about: de-duplicate per the linker's rules.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()

# ---------------------------------------------------------------------------
# NativeEE tree — greenfield construction (docs/design/CONSTRUCTION_PLAN.md).
# Phase 0 skeleton: an EMPTY host binary plus the two CI lints as ctest targets.
# Subsystem libraries are added per phase under engine/<subsystem>/CMakeLists.txt.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    # The UCRT and <windows.h> as EVERY TU in the tree sees them (Track B2; the cross-compile
    # smoke tools/build/win_cross_smoke.py mirrors this list): no "unsafe" deprecation of the
    # C library (fopen/strncpy/getenv/sscanf -> *_s, an error under -Werror), the POSIX names
    # (fileno, strdup, unlink) without theirs, M_PI from <cmath>, and a <windows.h> that
    # defines neither the min/max macros nor the GDI/winsock surface.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _USE_MATH_DEFINES
                            NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(PS2R_SIMD "BlockDecode: vectorized (NEON) inner loop, proven byte-identical to the scalar path by tests/gs/format/test_block_decode" OFF)
option(PS2R_DIAGNOSTICS "Compile the core/diag mechanism in (harness/dev builds); OFF = Release contains no probes" OFF)
set(PS2R_OG_ROOT "$ENV{PS2R_OLD_ROOT}" CACHE PATH
    "Frozen ORIGINAL tree (read-only oracle; default: $PS2R_OLD_ROOT, else empty). Used ONLY by tests/oracle differential tests; may be absent.")
set(PS2R_TITLE "" CACHE STRING
    "Title module linked by app/host (a directory name under games/). Empty = auto-discover: the
     one title in games/ is taken; with none the title lanes are skipped; with several, name one.")
# Where the title modules live. The default is this tree's games/; a port that keeps the engine
# as a submodule points this at its own games/ directory (the title directory contract is the
# same: games/<serial>_<slug>/hle/CMakeLists.txt marks a title).
set(PS2R_GAMES_DIR "${CMAKE_SOURCE_DIR}/games" CACHE PATH
    "Directory holding the title modules (<dir>/<serial>_<slug>/); default: this tree's games/")
file(REAL_PATH "${PS2R_GAMES_DIR}" PS2R_GAMES_DIR EXPAND_TILDE)
# --- title discovery -------------------------------------------------------------------------
# No serial is hardcoded anywhere in the build. A title directory is one with hle/CMakeLists.txt.
if(NOT PS2R_TITLE)
    file(GLOB _ps2r_title_candidates RELATIVE "${PS2R_GAMES_DIR}" "${PS2R_GAMES_DIR}/*")
    set(_ps2r_titles "")
    foreach(_cand IN LISTS _ps2r_title_candidates)
        if(EXISTS "${PS2R_GAMES_DIR}/${_cand}/hle/CMakeLists.txt")
            list(APPEND _ps2r_titles "${_cand}")
        endif()
    endforeach()
    list(LENGTH _ps2r_titles _ps2r_title_count)
    if(_ps2r_title_count EQUAL 1)
        set(PS2R_TITLE "${_ps2r_titles}")
        message(STATUS "nativeee: title auto-discovered: ${PS2R_TITLE} (in ${PS2R_GAMES_DIR})")
    elseif(_ps2r_title_count EQUAL 0)
        message(STATUS "nativeee: no title in ${PS2R_GAMES_DIR} -- engine, tools and component tests only; "
                       "every title target and its tests are skipped")
    else()
        message(FATAL_ERROR "nativeee: ${_ps2r_title_count} titles in ${PS2R_GAMES_DIR} (${_ps2r_titles}); "
                            "name one with -DPS2R_TITLE=<directory>")
    endif()
elseif(NOT EXISTS "${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle/CMakeLists.txt")
    message(FATAL_ERROR "nativeee: -DPS2R_TITLE=${PS2R_TITLE} names no title: "
                        "${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle/CMakeLists.txt does not exist")
endif()
# The private game-derived fixture contract ($NATIVEEE_PRIVATE_FIXTURES; tests SKIP 77 without it).
include(${CMAKE_SOURCE_DIR}/cmake/Ps2rPrivateFixtures.cmake)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Python3 3.8 COMPONENTS Interpreter REQUIRED)

# --- strict float semantics: ONE rule for GNU ----------------------------------------
# The tree's float results are step-exact by contract: ps2_float / vu_float emulate the PS2's
# unfused pipeline, the title math is volatile-stepped, chop-rounding sections switch the
# dynamic rounding mode (fesetround(FE_TOWARDZERO)), and the identity references were produced
# by Clang. Clang's defaults already match: -ffp-contract=on fuses only within one expression
# (never across the volatile steps) and `#pragma STDC FENV_ACCESS ON` (PS2R_FENV_ACCESS_ON) pins
# the rounding-mode TUs. GCC differs in both: its default -ffp-contract=fast fuses a*b+c ACROSS
# statements at -O2/-O3 on every FMA-capable target (aarch64; x86-64 with -march), and it does
# not implement the FENV pragma -- without -frounding-math it folds and moves float operations
# as if the mode were always round-to-nearest. Per-file -ffp-contract=off covers the TUs a pin
# found; a GNU lane gets both rules everywhere. Verified on GCC 13.3.0 (aarch64, the B1 lane):
# the GCC-only MathLibraryTrigOracle SnapToYaw one-code red under FE_TOWARDZERO is the SECOND
# cause -- -frounding-math alone turns it green (992,000 comparisons, differences=0),
# -ffp-contract=off alone does not; the native arm trig_math_rotations.cpp carries no
# PS2R_FENV_ACCESS_ON. Contraction off stays as the contract rule. GCC is not the shipped
# compiler (the dist presets are Clang); Clang lanes are unchanged -- their binaries are the
# frozen references.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-ffp-contract=off -frounding-math)
endif()

# --- build id: regenerated on EVERY build (custom target with no OUTPUT) ------
set(PS2R_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PS2R_GENERATED_DIR}")
set(PS2R_BUILD_ID_CPP "${PS2R_GENERATED_DIR}/build_id.cpp")
add_custom_target(generate_build_id
    COMMAND ${CMAKE_COMMAND} -DOUT=${PS2R_BUILD_ID_CPP} -P ${CMAKE_SOURCE_DIR}/cmake/gen_build_id.cmake
    BYPRODUCTS ${PS2R_BUILD_ID_CPP}
    COMMENT "Regenerating build id")

# --- engine libraries (per phase) ----------------------------------------------
add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/gs)      # Phase 2: format layer only (ps2r_gs_format)
add_subdirectory(engine/ee)      # Phase 3: cpu state + memory (ps2r_ee)
add_subdirectory(engine/title)    # Phase 3: the title CONTRACT -- TitleProfile + ActiveTitleProfile() (ps2r_title_api)
add_subdirectory(engine/sound)   # Phase 4E'/4E": the SPU2 half -- VagDecoder, Voice, SpuCore, Mixer (ps2r_sound)
add_subdirectory(engine/iop)     # Phase 4: the cooperative IOP kernel core (ps2r_iop; libsd over ps2r_sound)
add_subdirectory(engine/gif)      # Phase 5: GIFtag parse + GsSink + PATH arbiter (ps2r_gif)
add_subdirectory(engine/vu)       # Phase 5: VU state, decode, flags, stalls, XGKICK, tiers (ps2r_vu)
add_subdirectory(engine/vif)      # Phase 5: VIF0/1 command processor + UNPACK (ps2r_vif)
add_subdirectory(engine/sif)     # Phase 4C: the SIF register block (ps2r_sif)
add_subdirectory(engine/dmac)     # Phase 5: EE DMAC channels + chain walker (ps2r_dmac)
add_subdirectory(engine/cdvd)    # Phase 4D: the disc -- IsoFs, ReadEngine, libcdvd rows (ps2r_cdvd)
add_subdirectory(engine/mc)      # Phase 4E: the memory card -- McHostStore, McCommandModel, libmc rows (ps2r_mc)
add_subdirectory(engine/pad)     # Phase 4E/4E': the pad -- PadProtocol, libpad rows, PadmanModule, PadStampDuty (ps2r_pad)
add_subdirectory(tools/vu1_replay)   # Phase 5: VU1 corpus replay tool
add_subdirectory(tools/vif_replay)   # Phase 5: VIF chain replay + equivalence arms tool
add_subdirectory(tools/vu1_aot_pack)  # the pre-built VU1 library pack builder / checker (perf60)

# --- the EE recompiler (DP-23: NEW owns it; P8.8-T transplanted OLD's recompiler_tool_v2
# byte-identically and proved it regenerates games/<title>/recomp/ byte-for-byte). The
# dev preset builds it; a tree that only needs the frozen output may turn it OFF.
option(PS2R_BUILD_RECOMPILER "Build tools/recompiler_v2 (ps2r_recompiler_v2; needs the C language for third_party/rabbitizer)" ON)
if(PS2R_BUILD_RECOMPILER)
    enable_language(C)
    add_subdirectory(tools/recompiler_v2)
    add_subdirectory(tools/iop_recompiler)   # the IOP (R3000A) recompiler: STREAM.IRX -> iop_recompiled.{h,cpp}
endif()

# --- the title's inputs come from the USER'S disc (cmake/TitleInputs.cmake) --------------
# tools/disc_extract pulls the ELF / IOP module out of a disc image (NATIVEEE_DISC); a loose
# ELF / IRX is taken as given (NATIVEEE_TITLE_ELF / NATIVEEE_STREAM_IRX). Everything is SHA-256
# gated against games/<title>/TITLE_INPUTS.sha256. Without an input the title and its tests
# are skipped (STATUS, not an error): PS2R_TITLE_RECOMP_AVAILABLE / PS2R_TITLE_IRX_AVAILABLE.
add_subdirectory(tools/disc_extract)
add_subdirectory(tools/elf_embedded)   # an IOP module embedded in the title ELF -> a build input (TitleInputs `<ELF>!<module>` rows)
# TitleInputs reads games/${PS2R_TITLE}/TITLE_INPUTS.sha256 and FATALs without it, so it is
# only meaningful once a title is configured. With none, the generated-code variables stay
# unset and every target that would consume them is skipped below.
if(PS2R_TITLE)
    include(${CMAKE_SOURCE_DIR}/cmake/TitleInputs.cmake)
endif()

# --- the title's generated code: regenerated at BUILD TIME into ${PS2R_RECOMP_GEN_DIR} from the
# user's ELF (PHASE3_PLAN 3.3; the directory returns early, with a STATUS line, without one) ---
# Skipped entirely when no title is configured (PS2R_TITLE empty): the targets below never
# exist, and every consumer already gates on if(TARGET ps2r_title_*).
# The binary-dir argument is required when PS2R_GAMES_DIR is outside this tree; it keeps the
# in-tree layout (<build>/games/<title>/...) in both cases.
if(PS2R_TITLE)
    set(_ps2r_title_src "${PS2R_GAMES_DIR}/${PS2R_TITLE}")
    set(_ps2r_title_bin "${CMAKE_BINARY_DIR}/games/${PS2R_TITLE}")
    add_subdirectory(${_ps2r_title_src}/recomp ${_ps2r_title_bin}/recomp)
    # Phase 4B': the recompiled IOP module(s), BEFORE hle/ (a play composition may link them). A
    # title with ONE disc-side module keeps the iop/stream_irx layout; a title hosting several
    # (embedded) modules owns iop/CMakeLists.txt and adds its per-module directories itself.
    if(EXISTS "${_ps2r_title_src}/iop/CMakeLists.txt")
        add_subdirectory(${_ps2r_title_src}/iop ${_ps2r_title_bin}/iop)
    else()
        add_subdirectory(${_ps2r_title_src}/iop/stream_irx ${_ps2r_title_bin}/iop/stream_irx)
    endif()
    add_subdirectory(${_ps2r_title_src}/hle ${_ps2r_title_bin}/hle)     # Phase 3C2: the TitleProfile definition + sdk_bindings.tsv
    add_subdirectory(${_ps2r_title_src}/vu1_library ${_ps2r_title_bin}/vu1_library)   # Phase 5: the VU1 HLE program library (title data)
    add_subdirectory(${_ps2r_title_src}/renderer_profile ${_ps2r_title_bin}/renderer_profile)  # Phase 6
endif()

# --- the host binary -----------------------------------------------------------
# app/host is title-NEUTRAL: the generic host services plus a composition root that links
# whatever title is configured. It therefore always builds -- a bare clone with no title and
# no disc still produces nativeee, which reports its build identity and says there is nothing
# to run. A title module's play composition is linked in by app/host/CMakeLists.txt when its
# targets exist.
add_subdirectory(app/host)

add_subdirectory(tools/drawlist_tool)  # Phase 6
add_subdirectory(tools/gs_replay)  # Phase 6

# --- tests: lints are ctest targets from the first commit (ARCH section 13) -----
enable_testing()
add_subdirectory(app/gui)
if(NOT TARGET ps2r_session_runtime)
    message(FATAL_ERROR "The game application requires PS2R_PRODUCT_GUI=ON for its session runtime")
endif()
# The session runtime the GUI owns. ps2r_input_save came from app/gui/input_save.cmake, a
# title save-HLE suite that moved out with the title module.
if(TARGET nativeee)
    target_link_libraries(nativeee PRIVATE ps2r_session_runtime ps2r_session_checkpoint
        ps2r_session_restart ps2r_session_clock)
endif()
add_subdirectory(tests)

message(STATUS "nativeee: build type ${CMAKE_BUILD_TYPE}, PS2R_DIAGNOSTICS=${PS2R_DIAGNOSTICS}, PS2R_SIMD=${PS2R_SIMD}, title ${PS2R_TITLE}")
if(EXISTS "${PS2R_OG_ROOT}/host_app")
    message(STATUS "nativeee: frozen OLD tree found (read-only oracle available to tests/oracle)")
else()
    message(STATUS "nativeee: frozen OLD tree NOT found; tests/oracle will SKIP")
endif()

add_subdirectory(engine/render)  # Phase 6
# Phase 7 -- the IPU register model + MPEG intra decode arithmetic (ps2r_ipu; stage 7C).
# Stage 7D adds the ipu/hle FMV pipeline sources (and cmake/FindFFmpeg.cmake) to the
# same target from engine/ipu/CMakeLists.txt. Nothing here is linked by app/host yet:
# registration over the IPU I/O window is an integration item (PHASE7_PLAN section 8.2).
add_subdirectory(engine/ipu)
# tools/fmv_layout_ppm -- the FMV plane check's layout helper (verify/fmv_plane_check.py):
# a decoded picture through the real FrameWriter strip layout.
add_subdirectory(tools/fmv_layout_ppm)
