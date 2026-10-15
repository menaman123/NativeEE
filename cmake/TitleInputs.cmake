# cmake/TitleInputs.cmake -- the title's game-derived inputs come from the USER'S disc, never the tree.
#
# The recompiled EE code (games/<title>/recomp/recompiled*.{h,cpp}) and the recompiled IOP module
# (games/<title>/iop/stream_irx/iop_recompiled.{h,cpp}) are derived from the game's own files and are
# NOT committed. The build regenerates them into the BUILD directory from inputs the user supplies:
#
#   -DNATIVEEE_DISC=<image.iso|.bin>   a disc image of the user's own copy: the build extracts the
#                                      title ELF (volume root) and the IOP module -- both paths come from
#                                      games/<title>/TITLE_INPUTS.sha256 -- with tools/disc_extract,
#                                      SHA-256 gated; everything is built
#   -DNATIVEEE_TITLE_ELF=<SLUS_123.45>  the ELF alone: the play binary (nativeee) and the EE-side title
#                                      tests; the IOP module (oracle binary, IOP tests) stays skipped
#   -DNATIVEEE_STREAM_IRX=<STREAM.IRX>  the IOP module alone (with the ELF above: everything)
#
# Each is also read from the environment variable of the same name when the cache entry is empty.
# The expected hashes are games/<title>/TITLE_INPUTS.sha256 (shasum -a 256 format; the ELF's is the
# one the first-run launcher checks, TitleProfile::elf_sha256). A file with another hash is REFUSED
# with the reason (another revision / region, patched, damaged) -- at configure time for a loose file,
# at build time for an extraction.
#
# Without any input: STATUS lines, PS2R_TITLE_RECOMP_AVAILABLE=OFF, PS2R_TITLE_IRX_AVAILABLE=OFF; the
# engine, the recompilers, the tools and the component tests configure and build; every target and
# ctest that needs the title's generated code is skipped by its CMakeLists (if(TARGET ps2r_title_recomp)
# / if(TARGET ps2r_title_stream_irx)) -- not an error.
#
# Variables for the rest of the tree (normal variables, root scope):
#   PS2R_TITLE_GEN_ROOT        ${CMAKE_BINARY_DIR}/generated -- the generated tree mirrors the source layout
#   PS2R_TITLE_INPUTS_DIR      where extracted inputs land
#   PS2R_RECOMP_GEN_DIR        the EE recompiler's output directory (recompiled.h/.cpp, shards, continuations.h)
#   PS2R_IOP_GEN_DIR           the IOP recompiler's output directory (iop_recompiled.h/.cpp)
#   PS2R_TITLE_ELF / PS2R_TITLE_ELF_READY      the ELF path (given or extracted) / the file the generation
#                                              step depends on (the verification stamp or the extraction)
#   PS2R_TITLE_IRX / PS2R_TITLE_IRX_READY      the same for STREAM.IRX
#   PS2R_TITLE_RECOMP_AVAILABLE / PS2R_TITLE_IRX_AVAILABLE
#   PS2R_RECOMP_ENV            `${CMAKE_COMMAND} -E env PS2R_RECOMP_DIR=... PS2R_IOP_RECOMP_DIR=...` -- prefix for
#                              custom commands running the python generators that read the generated TUs
#   PS2R_RECOMP_TEST_ENV       the same two assignments as a ctest ENVIRONMENT property value
#   ps2r_title_inputs          a custom target: every extraction / verification stamp
include_guard(GLOBAL)

set(NATIVEEE_DISC "" CACHE FILEPATH
    "Disc image (.iso / raw .bin) of your own copy of the title; the build extracts the ELF and the IOP module from it")
set(NATIVEEE_TITLE_ELF "" CACHE FILEPATH
    "The title ELF (the serial-named executable, e.g. SLUS_123.45) as a loose file, when no disc image is given")
set(NATIVEEE_STREAM_IRX "" CACHE FILEPATH
    "The title's STREAM.IRX as a loose file, when no disc image is given")
option(PS2R_RECOMP_CHECK_MANIFEST
    "Fail the build when the generated code differs from games/<title>/*/MANIFEST.sha256 (OFF: warn only -- generator development)" ON)

set(PS2R_TITLE_GEN_ROOT "${CMAKE_BINARY_DIR}/generated")
set(PS2R_TITLE_INPUTS_DIR "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/inputs")
set(PS2R_RECOMP_GEN_DIR "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/recomp")
set(PS2R_IOP_GEN_DIR "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/iop/stream_irx")
set(PS2R_RECOMP_ENV ${CMAKE_COMMAND} -E env
    "PS2R_RECOMP_DIR=${PS2R_RECOMP_GEN_DIR}" "PS2R_IOP_RECOMP_DIR=${PS2R_IOP_GEN_DIR}")
set(PS2R_RECOMP_TEST_ENV "PS2R_RECOMP_DIR=${PS2R_RECOMP_GEN_DIR};PS2R_IOP_RECOMP_DIR=${PS2R_IOP_GEN_DIR}")
file(MAKE_DIRECTORY "${PS2R_TITLE_INPUTS_DIR}")

# --- the expected hashes -------------------------------------------------------------------
set(_ps2r_inputs_table "${PS2R_GAMES_DIR}/${PS2R_TITLE}/TITLE_INPUTS.sha256")
if(NOT EXISTS "${_ps2r_inputs_table}")
    message(FATAL_ERROR "TitleInputs: ${_ps2r_inputs_table} is missing (the title's input hashes)")
endif()
set(PS2R_TITLE_ELF_DISC_PATH "")
set(PS2R_TITLE_ELF_SHA256 "")
set(PS2R_TITLE_IRX_DISC_PATH "")
set(PS2R_TITLE_IRX_SHA256 "")
file(STRINGS "${_ps2r_inputs_table}" _ps2r_input_rows REGEX "^[0-9a-fA-F]+ ")
foreach(_row IN LISTS _ps2r_input_rows)
    if(_row MATCHES "^([0-9a-fA-F]+) [ *]?(.+)$")
        set(_sha "${CMAKE_MATCH_1}")
        set(_path "${CMAKE_MATCH_2}")
        string(TOLOWER "${_sha}" _sha)
        if(_path MATCHES "\\.IRX$")
            set(PS2R_TITLE_IRX_DISC_PATH "${_path}")
            set(PS2R_TITLE_IRX_SHA256 "${_sha}")
        elseif(NOT _path MATCHES "/")
            set(PS2R_TITLE_ELF_DISC_PATH "${_path}")
            set(PS2R_TITLE_ELF_SHA256 "${_sha}")
        endif()
    endif()
endforeach()
if(NOT PS2R_TITLE_ELF_SHA256)
    message(FATAL_ERROR "TitleInputs: ${_ps2r_inputs_table} has no title ELF row")
endif()

# --- the user's inputs: cache entry, else environment ---------------------------------------
function(_ps2r_input_from var)
    if("${${var}}" STREQUAL "" AND DEFINED ENV{${var}} AND NOT "$ENV{${var}}" STREQUAL "")
        set(_v "$ENV{${var}}")
    else()
        set(_v "${${var}}")
    endif()
    if(NOT _v STREQUAL "")
        file(REAL_PATH "${_v}" _v BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" EXPAND_TILDE)
        if(NOT EXISTS "${_v}")
            message(FATAL_ERROR "TitleInputs: ${var}=${_v} does not exist")
        endif()
    endif()
    set(_PS2R_${var} "${_v}" PARENT_SCOPE)
endfunction()
_ps2r_input_from(NATIVEEE_DISC)
_ps2r_input_from(NATIVEEE_TITLE_ELF)
_ps2r_input_from(NATIVEEE_STREAM_IRX)

set(_ps2r_input_stamps "")

# _ps2r_resolve_input(<ELF|IRX> <loose path or ""> <disc path on the volume> <sha256> <label>)
#   sets PS2R_TITLE_<X>, PS2R_TITLE_<X>_READY, PS2R_TITLE_<X>_AVAILABLE in the caller's scope.
function(_ps2r_resolve_input kind loose disc_path sha label)
    set(_file "")
    set(_ready "")
    if(NOT loose STREQUAL "")
        # A loose file: hash it now (refuse early), and again at build time through a stamp.
        file(SHA256 "${loose}" _got)
        if(NOT _got STREQUAL sha)
            message(FATAL_ERROR "TitleInputs: ${label} ${loose} is not the file this tree was made from:\n"
                "  SHA-256 ${_got}, expected ${sha} (games/${PS2R_TITLE}/TITLE_INPUTS.sha256)\n"
                "  -- another revision / region, a patched file, or a damaged copy.")
        endif()
        set(_file "${loose}")
        set(_ready "${PS2R_TITLE_INPUTS_DIR}/${kind}.verified")
        add_custom_command(OUTPUT "${_ready}"
            COMMAND ${CMAKE_COMMAND} "-DLABEL=${label}" "-DFILE=${_file}" "-DEXPECTED=${sha}" "-DSTAMP=${_ready}"
                    -P "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_input.cmake"
            DEPENDS "${_file}" "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_input.cmake"
            COMMENT "Verifying ${label} (${_file})"
            VERBATIM)
        message(STATUS "TitleInputs: ${label} = ${_file} (sha256 verified)")
    elseif(NOT _PS2R_NATIVEEE_DISC STREQUAL "")
        if(NOT TARGET ps2r_disc_extract)
            message(FATAL_ERROR "TitleInputs: NATIVEEE_DISC needs tools/disc_extract (ps2r_disc_extract) added before this file")
        endif()
        get_filename_component(_name "${disc_path}" NAME)
        set(_file "${PS2R_TITLE_INPUTS_DIR}/${_name}")
        set(_ready "${_file}")
        add_custom_command(OUTPUT "${_file}"
            COMMAND ps2r_disc_extract "${_PS2R_NATIVEEE_DISC}" "${disc_path}" "${_file}" --sha256 "${sha}"
            DEPENDS ps2r_disc_extract "${_PS2R_NATIVEEE_DISC}"
            COMMENT "Extracting ${label} (${disc_path}) from ${_PS2R_NATIVEEE_DISC}"
            VERBATIM)
        message(STATUS "TitleInputs: ${label} = ${disc_path} extracted from ${_PS2R_NATIVEEE_DISC} at build time (sha256 gated)")
    endif()
    if(_file STREQUAL "")
        set(PS2R_TITLE_${kind} "" PARENT_SCOPE)
        set(PS2R_TITLE_${kind}_READY "" PARENT_SCOPE)
        set(PS2R_TITLE_${kind}_AVAILABLE OFF PARENT_SCOPE)
    else()
        set(PS2R_TITLE_${kind} "${_file}" PARENT_SCOPE)
        set(PS2R_TITLE_${kind}_READY "${_ready}" PARENT_SCOPE)
        set(PS2R_TITLE_${kind}_AVAILABLE ON PARENT_SCOPE)
        list(APPEND _ps2r_input_stamps "${_ready}")
        set(_ps2r_input_stamps "${_ps2r_input_stamps}" PARENT_SCOPE)
    endif()
endfunction()

_ps2r_resolve_input(ELF "${_PS2R_NATIVEEE_TITLE_ELF}" "${PS2R_TITLE_ELF_DISC_PATH}" "${PS2R_TITLE_ELF_SHA256}"
    "title ELF ${PS2R_TITLE_ELF_DISC_PATH}")
set(PS2R_TITLE_RECOMP_AVAILABLE ${PS2R_TITLE_ELF_AVAILABLE})
if(PS2R_TITLE_IRX_SHA256)
    _ps2r_resolve_input(IRX "${_PS2R_NATIVEEE_STREAM_IRX}" "${PS2R_TITLE_IRX_DISC_PATH}" "${PS2R_TITLE_IRX_SHA256}"
        "IOP module ${PS2R_TITLE_IRX_DISC_PATH}")
else()
    set(PS2R_TITLE_IRX "")
    set(PS2R_TITLE_IRX_READY "")
    set(PS2R_TITLE_IRX_AVAILABLE OFF)
endif()

add_custom_target(ps2r_title_inputs DEPENDS ${_ps2r_input_stamps})

if(NOT PS2R_TITLE_RECOMP_AVAILABLE)
    message(STATUS "TitleInputs: no title ELF -- give -DNATIVEEE_DISC=<your disc image> (or -DNATIVEEE_TITLE_ELF=<${PS2R_TITLE_ELF_DISC_PATH}>;"
                   " the same names work as environment variables).")
    message(STATUS "TitleInputs: the title (nativeee, nativeee_oracle, ps2r_title_recomp) and its tests are SKIPPED;"
                   " the engine, the recompilers and the component tests build.")
elseif(NOT PS2R_TITLE_IRX_AVAILABLE)
    message(STATUS "TitleInputs: no ${PS2R_TITLE_IRX_DISC_PATH} -- the recompiled IOP module (ps2r_title_stream_irx), the oracle binary"
                   " and the IOP tests are SKIPPED (give -DNATIVEEE_DISC or -DNATIVEEE_STREAM_IRX).")
endif()
if((PS2R_TITLE_RECOMP_AVAILABLE OR PS2R_TITLE_IRX_AVAILABLE) AND NOT PS2R_BUILD_RECOMPILER)
    message(FATAL_ERROR "TitleInputs: a title input is given but PS2R_BUILD_RECOMPILER is OFF -- the generated code is produced by the in-tree recompilers; set it ON")
endif()
