# tests/oracle -- OG-oracle differential tests (OLD read-only; SKIP loudly when absent).

# The lifted OLD address tables: the checked-in fixture must match its sha256 (no OLD
# needed), and -- when the OLD tree is present -- a fresh read-only lift must reproduce it.
add_test(NAME OgTablesFixtureSha
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/oracle/lift_og_tables.py
            --out ${CMAKE_SOURCE_DIR}/tests/oracle/og_tables.bin --check)
add_test(NAME OgTablesReLift
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/oracle/lift_og_tables.py
            --out ${CMAKE_SOURCE_DIR}/tests/oracle/og_tables.bin --lift --og-root "${PS2R_OG_ROOT}")

# PsmLayout vs OLD F1 (lifted tables) / F2 (gs_deswizzle.h included read-only) / the
# manual formula, one bounded ctest case per PSM. -O2: the grid is ~15 M samples per PSM.
add_executable(og_gs_format_oracle og_gs_format_oracle.cpp)
target_link_libraries(og_gs_format_oracle PRIVATE ps2r_gs_format)
target_compile_options(og_gs_format_oracle PRIVATE -Wall -Wextra -O2)
set(PS2R_OG_DESWIZZLE_H "${PS2R_OG_ROOT}/host_app/gs_deswizzle.h")
if(EXISTS "${PS2R_OG_DESWIZZLE_H}")
    target_compile_definitions(og_gs_format_oracle PRIVATE "PS2R_OG_DESWIZZLE_H=\"${PS2R_OG_DESWIZZLE_H}\"")
endif()
foreach(psm PSMCT32 PSMCT24 PSMCT16 PSMCT16S PSMT8 PSMT4 PSMT8H PSMT4HL PSMT4HH PSMZ32 PSMZ24 PSMZ16 PSMZ16S F3F4 CLUT TEXA)
    add_test(NAME OgGsFormatOracle_${psm}
        COMMAND og_gs_format_oracle ${CMAKE_SOURCE_DIR}/tests/oracle/og_tables.bin ${psm})
    set_tests_properties(OgGsFormatOracle_${psm} PROPERTIES TIMEOUT 60)
endforeach()

# Port of OLD tests/test_page_invariance.cpp at the VRAM level: the same page
# image written through Swizzle at two page addresses decodes identically, never
# touches the other page, and Z rows never alias colour rows (NEW-only).
add_executable(og_page_invariance og_page_invariance.cpp)
target_link_libraries(og_page_invariance PRIVATE ps2r_gs_format)
target_compile_options(og_page_invariance PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME OgPageInvariance COMMAND og_page_invariance)
set_tests_properties(OgPageInvariance PROPERTIES TIMEOUT 60)

# Port of OLD tests/test_block_decode_equivalence.cpp as a differential oracle: the
# original's exact case list replayed against NEW BlockDecode with every differing byte
# attributed to a registered EQUIVALENCE_PLAN 7.1 row (see the TU header).
add_executable(og_block_decode og_block_decode.cpp)
target_link_libraries(og_block_decode PRIVATE ps2r_gs_format)
target_compile_options(og_block_decode PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME OgBlockDecode COMMAND og_block_decode ${CMAKE_SOURCE_DIR}/tests/oracle/og_tables.bin)
set_tests_properties(OgBlockDecode PROPERTIES TIMEOUT 120)
