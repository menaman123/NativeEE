# cmake/EngineBundle.cmake -- `cmake --build <build> --target engine_bundle` stages the redistributable
# engine bundle (tools/dist/engine_bundle.py has the layout) into ${PS2R_BUNDLE_DIR}/engine-bundle-<platform>/.
#
# Decision D-A (the Sly 2 launcher stage): a release carries the launcher, this bundle and a pinned
# portable compiler per platform; the player's own disc is recompiled on the player's machine by
# tools/prepare (ps2r_prepare / nativeee-prepare), which reads the bundle's BUNDLE.json for the exact
# commands this build used. Nothing game-derived is in the bundle: the on-machine targets (the
# generated EE shards, the recompiled IOP modules, and whatever a title lists in the global property
# PS2R_ON_MACHINE_TARGETS -- its one TU that includes generated headers) are described, not shipped.
#
# Requires the Ninja generator (the script reads build.ninja + compile_commands.json). Title-neutral:
# the title names its binary through the global property PS2R_TITLE_BINARY_NAME (else derived from
# TitleProfile::display_name).
include_guard(GLOBAL)

set(PS2R_RELEASE_TAG "" CACHE STRING "The release tag written into the engine bundle's BUNDLE.json (CI sets it; empty = a development bundle)")
set(PS2R_BUNDLE_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Where engine_bundle stages engine-bundle-<platform>/")

if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "nativeee: engine_bundle needs the Ninja generator (build.ninja is its input) -- target not defined")
    return()
endif()
if(NOT PS2R_TITLE OR NOT TARGET ps2r_title_recomp)
    message(STATUS "nativeee: engine_bundle needs a configured title with its recompiled code -- target not defined")
    return()
endif()

# <os>-<arch>: macos-arm64, macos-x86_64, windows-x86_64, linux-x86_64 (the D-B list).
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _ps2r_bundle_os)
if(_ps2r_bundle_os STREQUAL "darwin")
    set(_ps2r_bundle_os "macos")
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _ps2r_bundle_arch)
if(_ps2r_bundle_arch MATCHES "^(aarch64|arm64)$")
    set(_ps2r_bundle_arch "arm64")
elseif(_ps2r_bundle_arch MATCHES "^(amd64|x86_64|x64)$")
    set(_ps2r_bundle_arch "x86_64")
endif()
set(PS2R_BUNDLE_PLATFORM "${_ps2r_bundle_os}-${_ps2r_bundle_arch}" CACHE STRING "The bundle's platform id")

get_property(_ps2r_on_machine GLOBAL PROPERTY PS2R_ON_MACHINE_TARGETS)
# Every recompiled IOP module target is on-machine too (games/<title>/iop defines ps2r_title_iop_<id>).
foreach(_id fileio lgaud snd989 spiop stream_irx)
    if(TARGET ps2r_title_iop_${_id})
        list(APPEND _ps2r_on_machine ps2r_title_iop_${_id})
    endif()
endforeach()
list(REMOVE_DUPLICATES _ps2r_on_machine)
string(REPLACE ";" "," _ps2r_on_machine_csv "${_ps2r_on_machine}")
get_property(_ps2r_binary_name GLOBAL PROPERTY PS2R_TITLE_BINARY_NAME)

set(_ps2r_bundle_out "${PS2R_BUNDLE_DIR}/engine-bundle-${PS2R_BUNDLE_PLATFORM}")
set(_ps2r_bundle_deps nativeee ps2r_disc_extract ps2r_elf_embedded ps2r_recompiler_v2 ps2r_continuations
    ps2r_iop_recompiler vu1_aot_pack vu1_aot_prelude_stage)
if(TARGET nativeee-prepare)
    list(APPEND _ps2r_bundle_deps nativeee-prepare)
endif()
set(_ps2r_bundle_iso "")
if(DEFINED _PS2R_NATIVEEE_DISC AND NOT _PS2R_NATIVEEE_DISC STREQUAL "")
    set(_ps2r_bundle_iso "${_PS2R_NATIVEEE_DISC}")
endif()
add_custom_target(engine_bundle
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/dist/engine_bundle.py"
            --build "${CMAKE_BINARY_DIR}" --source "${CMAKE_SOURCE_DIR}" --games-dir "${PS2R_GAMES_DIR}"
            --title "${PS2R_TITLE}" --out "${_ps2r_bundle_out}" --platform "${PS2R_BUNDLE_PLATFORM}"
            --release-tag "${PS2R_RELEASE_TAG}" --on-machine-targets "${_ps2r_on_machine_csv}"
            --binary-name "${_ps2r_binary_name}" --cxx "${CMAKE_CXX_COMPILER}" --sysroot "${CMAKE_OSX_SYSROOT}"
            --iso "${_ps2r_bundle_iso}"
    # The asset guard over the staged folder (artifact mode: names + magic + ELF windows; the live
    # ELF when the build has one -- the stronger, local form).
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/ci/asset_scan.py" "${_ps2r_bundle_out}"
            --report "${_ps2r_bundle_out}.asset_scan.md"
    DEPENDS ${_ps2r_bundle_deps}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Staging the engine bundle -> ${_ps2r_bundle_out} (+ asset scan)"
    VERBATIM USES_TERMINAL)
message(STATUS "nativeee: engine_bundle -> ${_ps2r_bundle_out} (on-machine targets: ${_ps2r_on_machine_csv}; binary ${_ps2r_binary_name})")
