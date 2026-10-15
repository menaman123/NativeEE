# tools/disc_extract/test_disc_extract.cmake -- DiscExtractSynthetic (cmake -P).
#   -DEXTRACT=<ps2r_disc_extract> -DMAKE_ISO=<make_synthetic_iso> -DSCRATCH=<dir>
# A synthetic plain .iso and a raw .bin, each carrying ELF.BIN in the root and
# SUB/DIR/MODULE.IRX nested; both files come back byte-exact, the right hash is accepted,
# the wrong hash refused with exit 6 and no output left, a missing path refused with exit 4.
foreach(v EXTRACT MAKE_ISO SCRATCH)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "test_disc_extract.cmake: -D${v} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")
set(elf_text "fake ELF bytes 0123456789 -- the volume root file")
set(irx_text "fake IRX bytes -- nested under SUB/DIR")
file(WRITE "${SCRATCH}/elf.src" "${elf_text}")
file(WRITE "${SCRATCH}/irx.src" "${irx_text}")
file(SHA256 "${SCRATCH}/elf.src" elf_sha)
file(SHA256 "${SCRATCH}/irx.src" irx_sha)

function(run_expect code)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL code)
        message(FATAL_ERROR "expected exit ${code}, got ${rc}:\n  ${ARGN}\n${out}${err}")
    endif()
endfunction()

foreach(layout plain raw)
    set(image "${SCRATCH}/disc_${layout}.img")
    run_expect(0 "${MAKE_ISO}" "${image}" ${layout} --volume TESTVOL
        "ELF.BIN=${SCRATCH}/elf.src" "SUB/DIR/MODULE.IRX=${SCRATCH}/irx.src")
    # Root file, verified.
    run_expect(0 "${EXTRACT}" "${image}" ELF.BIN "${SCRATCH}/${layout}_elf.out" --sha256 "${elf_sha}")
    file(READ "${SCRATCH}/${layout}_elf.out" got)
    if(NOT got STREQUAL elf_text)
        message(FATAL_ERROR "${layout}: ELF.BIN came back different: '${got}'")
    endif()
    # Nested path (with and without the ;1 version, lower case too), verified.
    run_expect(0 "${EXTRACT}" "${image}" "SUB/DIR/MODULE.IRX" "${SCRATCH}/${layout}_irx.out" --sha256 "${irx_sha}")
    run_expect(0 "${EXTRACT}" "${image}" "sub/dir/module.irx;1" "${SCRATCH}/${layout}_irx2.out")
    file(READ "${SCRATCH}/${layout}_irx.out" got)
    if(NOT got STREQUAL irx_text)
        message(FATAL_ERROR "${layout}: MODULE.IRX came back different: '${got}'")
    endif()
    # Wrong hash: refused, nothing written.
    run_expect(6 "${EXTRACT}" "${image}" ELF.BIN "${SCRATCH}/${layout}_bad.out" --sha256 "${irx_sha}")
    if(EXISTS "${SCRATCH}/${layout}_bad.out")
        message(FATAL_ERROR "${layout}: a refused extraction left an output file")
    endif()
    # Missing path.
    run_expect(4 "${EXTRACT}" "${image}" "NOPE.BIN" "${SCRATCH}/${layout}_none.out")
endforeach()
# Not a disc image at all.
run_expect(3 "${EXTRACT}" "${SCRATCH}/elf.src" ELF.BIN "${SCRATCH}/notiso.out")
message(STATUS "DiscExtractSynthetic: ok")
