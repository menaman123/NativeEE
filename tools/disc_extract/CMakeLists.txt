# tools/disc_extract -- ps2r_disc_extract: one file out of a disc image, SHA-256 gated.
# Compiled from the engine's IsoFs and Sha256 sources directly (not ps2r_cdvd: that archive
# links ps2r_ee, whose seams need the title's generated code, and this tool runs BEFORE the
# title is generated -- it is what produces the generator's input).
add_executable(ps2r_disc_extract
    main.cpp
    ${CMAKE_SOURCE_DIR}/engine/cdvd/iso_fs.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/hash/sha256.cpp)
target_include_directories(ps2r_disc_extract PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_disc_extract PRIVATE -Wall -Wextra -Werror)

# DiscExtractSynthetic: a synthetic volume (tests/lib make_synthetic_iso) carrying a fake
# SLUS_123.45 and a nested DATA/SYS/STREAM.IRX; the extractor copies both byte-exact,
# accepts the right hash, refuses the wrong one (exit 6, no output left) and a missing path (4).
if(TARGET make_synthetic_iso)
    add_test(NAME DiscExtractSynthetic
        COMMAND ${CMAKE_COMMAND}
            -DEXTRACT=$<TARGET_FILE:ps2r_disc_extract> -DMAKE_ISO=$<TARGET_FILE:make_synthetic_iso>
            -DSCRATCH=${CMAKE_BINARY_DIR}/test_scratch/disc_extract
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_disc_extract.cmake)
    set_tests_properties(DiscExtractSynthetic PROPERTIES TIMEOUT 60)
endif()
