# ps2r_replay_host -- the tier-1 verification harness (recomp-agent): replays one function's recorded calls
# (a RACORP1 corpus) against a candidate body on a scratch memory image. Built only when the title's generated
# code exists (ps2r_title_recomp) and the shards observe their accesses (PS2R_TRACE_ACCESS=ON: the replay's
# coverage and its cheap per-record reset both need every load and store reported).
#
# Targets:
#   ps2r_replay_host   the executable: the generated bodies (libps2r_title_recomp.a, whole archive) + the engine's
#                      memory objects (FastMem / MainMemory / AddressMap / MmioRouter / Scratchpad) + the PS2 float
#                      helpers + this directory's seams. Exports its symbols so a dlopen'd candidate resolves
#                      memory::, recomp_call, the float helpers against THE SAME objects the generated code uses.
#   ps2r_replay_pch    <build>/tools/replay_host/replay_abi.pch: the precompiled candidate prologue (replay_abi.h =
#                      recompiled.h + the eleven ABI forwarders + the standard headers), compiled with the shards'
#                      flags; and replay_toolchain.json, the recipe (compiler, flags, includes, defines, pch, host)
#                      a driver follows to compile a candidate in one step.
if(NOT TARGET ps2r_title_recomp)
    message(STATUS "tools/replay_host: no title generated code (ps2r_title_recomp) -- ps2r_replay_host skipped")
    return()
endif()
if(NOT PS2R_TRACE_ACCESS)
    message(STATUS "tools/replay_host: PS2R_TRACE_ACCESS is OFF -- ps2r_replay_host skipped (configure with -DPS2R_TRACE_ACCESS=ON)")
    return()
endif()
find_package(ZLIB REQUIRED)

set(_gen "${PS2R_RECOMP_GEN_DIR}")
set(_anchor "${CMAKE_SOURCE_DIR}/engine/ee/recomp_abi/anchor")

add_executable(ps2r_replay_host
    replay_host.cpp
    racorp.cpp
    seams.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/main_memory.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/scratchpad.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/address_map.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/fast_mem.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/mmio_router.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/mem/mmio_sink.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/cpu/ps2_float.cpp
    ${CMAKE_SOURCE_DIR}/engine/ee/recomp_abi/recomp_abi.cpp
    ${CMAKE_SOURCE_DIR}/engine/vu/vu_float.cpp)
target_include_directories(ps2r_replay_host PRIVATE ${_anchor} ${CMAKE_SOURCE_DIR} ${_gen})
target_compile_options(ps2r_replay_host PRIVATE -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -ffp-contract=off)
set_target_properties(ps2r_replay_host PROPERTIES ENABLE_EXPORTS ON)   # -export_dynamic / -rdynamic: the candidate binds to us
# The generated bodies, every member (the init tables reference all 6,575 bodies, but be explicit). Linked by file,
# not by target, so the title library's PUBLIC engine dependencies (ps2r_ee, ps2r_gs_sync, ps2r_vu) are NOT pulled
# in: this directory's seams.cpp stands in for the engine behind the generated code.
if(APPLE)
    target_link_options(ps2r_replay_host PRIVATE "LINKER:-force_load,$<TARGET_FILE:ps2r_title_recomp>")
else()
    target_link_options(ps2r_replay_host PRIVATE
        "LINKER:--whole-archive,$<TARGET_FILE:ps2r_title_recomp>,--no-whole-archive")
endif()
target_link_libraries(ps2r_replay_host PRIVATE ZLIB::ZLIB ${CMAKE_DL_LIBS})
add_dependencies(ps2r_replay_host ps2r_title_recomp)

# ---- the candidate prologue PCH + the compile recipe ------------------------------------------------------------
# The flags are the shard flags (games/<title>/recomp/CMakeLists.txt: PS2R_RECOMP_SEMANTIC_FLAGS + -O2) plus the
# configuration's own (CMAKE_CXX_FLAGS_<CONFIG>), spelled out so the driver can repeat them exactly: clang refuses a
# PCH whose language or macro options differ from the TU's.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg)
set(_cfg_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_cfg}}")
separate_arguments(_cfg_flags_list NATIVE_COMMAND "${_cfg_flags}")
set(_cand_flags ${_cfg_flags_list} -std=c++17 -w -fno-strict-aliasing -fwrapv -ffp-contract=off -O2 -fPIC)
if(CMAKE_OSX_ARCHITECTURES)
    foreach(_a ${CMAKE_OSX_ARCHITECTURES})
        list(APPEND _cand_flags -arch ${_a})
    endforeach()
endif()
if(CMAKE_OSX_SYSROOT)
    list(APPEND _cand_flags -isysroot ${CMAKE_OSX_SYSROOT})
endif()
set(_cand_defines -DPS2R_TRACE_ACCESS=1)
set(_cand_includes -I${_anchor} -I${CMAKE_SOURCE_DIR} -I${_gen} -I${CMAKE_CURRENT_SOURCE_DIR})
set(_pch "${CMAKE_CURRENT_BINARY_DIR}/replay_abi.pch")
add_custom_command(
    OUTPUT "${_pch}"
    COMMAND ${CMAKE_CXX_COMPILER} ${_cand_flags} ${_cand_defines} ${_cand_includes}
            -x c++-header "${CMAKE_CURRENT_SOURCE_DIR}/replay_abi.h" -o "${_pch}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/replay_abi.h" "${_gen}/recompiled.h" ps2r_title_recomp_generate
    COMMENT "Precompiling the tier-1 candidate prologue (replay_abi.h) -> ${_pch}"
    VERBATIM)
add_custom_target(ps2r_replay_pch DEPENDS "${_pch}")
add_dependencies(ps2r_replay_host ps2r_replay_pch)

# The recipe. JSON by hand (lists of strings), written at configure time.
set(_recipe "${CMAKE_CURRENT_BINARY_DIR}/replay_toolchain.json")
set(_recipe_in "${CMAKE_CURRENT_BINARY_DIR}/replay_toolchain.json.in")
function(_ps2r_json_list out)
    set(_s "")
    foreach(_x ${ARGN})
        string(REPLACE "\\" "\\\\" _x "${_x}")
        string(REPLACE "\"" "\\\"" _x "${_x}")
        if(_s)
            set(_s "${_s}, \"${_x}\"")
        else()
            set(_s "\"${_x}\"")
        endif()
    endforeach()
    set(${out} "[${_s}]" PARENT_SCOPE)
endfunction()
_ps2r_json_list(_j_flags ${_cand_flags})
_ps2r_json_list(_j_defines ${_cand_defines})
_ps2r_json_list(_j_includes ${_cand_includes})
if(APPLE)
    set(_j_shared "[\"-shared\", \"-undefined\", \"dynamic_lookup\"]")
    set(_j_ext ".dylib")
else()
    set(_j_shared "[\"-shared\"]")
    set(_j_ext ".so")
endif()
file(WRITE "${_recipe_in}" "{
  \"format\": \"ps2r_replay_toolchain/1\",
  \"cxx\": \"${CMAKE_CXX_COMPILER}\",
  \"flags\": ${_j_flags},
  \"defines\": ${_j_defines},
  \"includes\": ${_j_includes},
  \"pch\": \"${_pch}\",
  \"pch_header\": \"${CMAKE_CURRENT_SOURCE_DIR}/replay_abi.h\",
  \"shared_flags\": ${_j_shared},
  \"shared_ext\": \"${_j_ext}\",
  \"host\": \"$<TARGET_FILE:ps2r_replay_host>\",
  \"generated_dir\": \"${_gen}\",
  \"engine\": \"${CMAKE_SOURCE_DIR}\"
}
")
# $<TARGET_FILE:> is not expanded by file(WRITE); write the final form through file(GENERATE).
file(GENERATE OUTPUT "${_recipe}" INPUT "${_recipe_in}")
message(STATUS "tools/replay_host: ps2r_replay_host + replay_abi.pch (recipe ${_recipe})")
