# cmake/ps2r_verify_manifest.cmake -- the generated output must be the bytes the tree was developed against.
#
#   cmake -DLABEL=<name> -DMANIFEST=<MANIFEST.sha256> -DGEN_DIR=<dir> -DSOURCE_DIR=<dir>
#         -DSTRICT=ON|OFF -DSTAMP=<file> -P ps2r_verify_manifest.cmake
#
# MANIFEST.sha256 is `shasum -a 256` output: "<64 hex>  <relative path>". Each row is looked up in
# GEN_DIR first (the files the build just generated), then SOURCE_DIR (the committed inputs and
# hand-kept files the manifest also covers). A missing file or a differing hash is reported per
# row. STRICT=ON (the default build): the generated files are REMOVED (so the next build regenerates
# and re-checks instead of compiling unverified output) and the build fails with the list and what
# to do; STRICT=OFF (a developer changing the generator): a WARNING, the build goes on.
# On success STAMP is written (the consumers' dependency).
foreach(v LABEL MANIFEST GEN_DIR SOURCE_DIR STAMP)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "ps2r_verify_manifest.cmake: -D${v} is required")
    endif()
endforeach()
if(NOT DEFINED STRICT)
    set(STRICT ON)
endif()
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "${LABEL}: no manifest at ${MANIFEST}")
endif()
file(STRINGS "${MANIFEST}" _rows REGEX "^[0-9a-fA-F]+ ")
set(_bad "")
set(_generated "")
set(_checked 0)
foreach(_row IN LISTS _rows)
    if(NOT _row MATCHES "^([0-9a-fA-F]+) [ *]?(.+)$")
        continue()
    endif()
    set(_want "${CMAKE_MATCH_1}")
    set(_rel "${CMAKE_MATCH_2}")
    string(TOLOWER "${_want}" _want)
    if(EXISTS "${GEN_DIR}/${_rel}")
        set(_file "${GEN_DIR}/${_rel}")
        list(APPEND _generated "${_file}")
    elseif(EXISTS "${SOURCE_DIR}/${_rel}")
        set(_file "${SOURCE_DIR}/${_rel}")
    else()
        list(APPEND _bad "${_rel}: MISSING (neither ${GEN_DIR} nor ${SOURCE_DIR})")
        continue()
    endif()
    file(SHA256 "${_file}" _got)
    math(EXPR _checked "${_checked} + 1")
    if(NOT _got STREQUAL _want)
        list(APPEND _bad "${_rel}: sha256 ${_got}, manifest ${_want}")
    endif()
endforeach()
if(_checked EQUAL 0)
    message(FATAL_ERROR "${LABEL}: ${MANIFEST} lists no files")
endif()
if(_bad)
    list(JOIN _bad "\n  " _bad_text)
    list(LENGTH _bad _n_bad)
    set(_msg "${LABEL}: ${_n_bad} of ${_checked} manifest rows differ from ${MANIFEST}:\n  ${_bad_text}\n"
             "  The generated code is not the bytes this tree was developed and tested against: either the\n"
             "  input (ELF / IRX) is another revision -- the input hash check should have caught that -- or\n"
             "  the generator (tools/recompiler_v2, tools/iop_recompiler, continuations.py) changed. A deliberate\n"
             "  generator change regenerates the manifest (tools/regen_from_frozen.sh); to build anyway configure\n"
             "  with -DPS2R_RECOMP_CHECK_MANIFEST=OFF (the check then only warns).")
    string(REPLACE ";" "" _msg "${_msg}")
    if(STRICT)
        foreach(_f IN LISTS _generated)
            file(REMOVE "${_f}")
        endforeach()
        file(REMOVE "${STAMP}")
        message(FATAL_ERROR "${_msg}\n  (the ${LABEL} generated files were removed; the next build regenerates them)")
    endif()
    message(WARNING "${_msg}")
else()
    message(STATUS "${LABEL}: ${_checked} files match ${MANIFEST}")
endif()
get_filename_component(_stamp_dir "${STAMP}" DIRECTORY)
file(MAKE_DIRECTORY "${_stamp_dir}")
string(TIMESTAMP _now UTC)
file(WRITE "${STAMP}" "${LABEL}: ${_checked} files checked against ${MANIFEST} at ${_now}\n")
