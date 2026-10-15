# tools/recompiler_v2 -- the EE recompiler (DP-23: NEW owns it).
#
# The sources under this directory, third_party/elfio and third_party/rabbitizer are
# byte-identical copies of OLD's `recompiler_tool_v2` target (OLD/CMakeLists.txt:78-100 plus
# the `recomp_v2_engine`, `EEAnalyze`, `elfio` and `rabbitizer` targets it links); see the
# PROVENANCE.md in each tree for the OLD path + md5 of every file. Stage P8.8-T proved that
# this build of the unmodified copy regenerates games/<title>/recomp/ byte-for-byte
# (tools/regen_from_frozen.sh --tool). From P8.9-A on generator fixes are made here.
#
# Layout is OLD's: OLD's PROJECT_ROOT-relative includes (`host_app/cpu_state.h`,
# `include/json.hpp`, `EEAnalyze/analyze.h`) resolve against this directory, and the
# `recompiler_tool/` include dir is OLD's second include root. OLD also linked SDL2::SDL2;
# no source in the closure includes an SDL header, so it is not linked here (proved by the
# build). Transplanted code is compiled WITHOUT -Werror (warning count recorded in the stage
# record); it is not NEW engine code and the lints exempt it.

set(RV2 "${CMAKE_CURRENT_SOURCE_DIR}")
set(TP "${CMAKE_SOURCE_DIR}/third_party")

# --- elfio (header-only) --------------------------------------------------------
add_library(ps2r_tp_elfio INTERFACE)
target_include_directories(ps2r_tp_elfio INTERFACE "${TP}/elfio")

# --- rabbitizer (MIPS disassembler, static) -----------------------------------------
set(RABBITIZER_ROOT "${TP}/rabbitizer")
file(GLOB_RECURSE PS2R_RABBITIZER_SOURCES CONFIGURE_DEPENDS
    "${RABBITIZER_ROOT}/cplusplus/src/*.cpp"
    "${RABBITIZER_ROOT}/src/*.c"
)
add_library(ps2r_tp_rabbitizer STATIC ${PS2R_RABBITIZER_SOURCES})
target_include_directories(ps2r_tp_rabbitizer PUBLIC
    "${RABBITIZER_ROOT}/cplusplus/include"
    "${RABBITIZER_ROOT}/include"
    "${RABBITIZER_ROOT}/include/generated"
    "${RABBITIZER_ROOT}/tables"
)
target_compile_definitions(ps2r_tp_rabbitizer PUBLIC RABBITIZER_STATIC)
# rabbitizer's src/*.c are C: the root CMakeLists enables the C language before adding this
# directory (the rest of the tree is CXX only).

# --- EEAnalyze (OLD's static analysis library) --------------------------------------
add_library(ps2r_rv2_eeanalyze STATIC "${RV2}/EEAnalyze/analyze.cpp")
target_include_directories(ps2r_rv2_eeanalyze PUBLIC "${RV2}")
target_link_libraries(ps2r_rv2_eeanalyze PUBLIC ps2r_tp_rabbitizer ps2r_tp_elfio)

# --- recomp_v2_engine (OLD's v2 discovery) --------------------------------------------
add_library(ps2r_rv2_engine STATIC
    "${RV2}/recompiler_tool/discovery/leaders.cpp"
    "${RV2}/recompiler_tool/discovery/jumptables.cpp"
    "${RV2}/recompiler_tool/discovery/functions.cpp"
    "${RV2}/recompiler_tool/discovery/report.cpp"
    "${RV2}/recompiler_tool/elf/elf_image.cpp"
)
target_include_directories(ps2r_rv2_engine PRIVATE "${RV2}" "${RV2}/recompiler_tool")
target_link_libraries(ps2r_rv2_engine PRIVATE ps2r_tp_elfio)
target_compile_features(ps2r_rv2_engine PRIVATE cxx_std_17)

# --- the tool ---------------------------------------------------------------------------
add_executable(ps2r_recompiler_v2
    "${RV2}/recompiler_tool/main_v2.cpp"
    "${RV2}/recompiler_tool/recompiler.cpp"
    "${RV2}/recompiler_tool/emit/driver_v2.cpp"
    "${RV2}/recompiler_tool/translate/control_flow_v2.cpp"
    "${RV2}/recompiler_tool/corpus/recorder_gen.cpp"
    "${RV2}/recompiler_tool/corpus/audit.cpp"
    "${RV2}/EEAnalyze/analyze.cpp"
)
target_link_libraries(ps2r_recompiler_v2 PRIVATE ps2r_rv2_engine ps2r_tp_elfio ps2r_rv2_eeanalyze)
target_include_directories(ps2r_recompiler_v2 PRIVATE "${RV2}" "${RV2}/recompiler_tool")
target_compile_features(ps2r_recompiler_v2 PRIVATE cxx_std_17)
