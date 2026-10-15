# cmake/ps2r_verify_input.cmake -- a user-supplied title input (ELF / IRX) must be the file this
# tree was made from.
#
#   cmake -DLABEL=<name> -DFILE=<path> -DEXPECTED=<64 hex> -DSTAMP=<file> -P ps2r_verify_input.cmake
#
# The configure step checks the same hash once; this runs again at build time (the file is a
# dependency of the generation step) so a swapped file is refused before it is recompiled.
foreach(v LABEL FILE EXPECTED STAMP)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "ps2r_verify_input.cmake: -D${v} is required")
    endif()
endforeach()
if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "${LABEL}: ${FILE} does not exist")
endif()
file(SHA256 "${FILE}" _got)
string(TOLOWER "${EXPECTED}" _want)
if(NOT _got STREQUAL _want)
    file(REMOVE "${STAMP}")
    message(FATAL_ERROR "${LABEL}: ${FILE} is not the file this build recompiles:\n"
        "  SHA-256 ${_got}, expected ${_want}\n"
        "  (another revision / region, a patched file, or a damaged copy -- see games/<title>/TITLE_INPUTS.sha256)")
endif()
get_filename_component(_stamp_dir "${STAMP}" DIRECTORY)
file(MAKE_DIRECTORY "${_stamp_dir}")
file(WRITE "${STAMP}" "${LABEL}: ${FILE} sha256 ${_got}\n")
message(STATUS "${LABEL}: ${FILE} verified (sha256 ${_got})")
