# engine/vif -- the VIF0/VIF1 command processors: register file, word latch +
# quadword FIFO state machine, the whole-span parser, UNPACK (scalar reference
# + the VIF1 bulk lanes), MSCAL/MPG/DIRECT into the VU units and the GIF
# arbiter; savestate/: the VIFS section codec (VifsSection). Compiled at -O2 -fno-strict-aliasing -fwrapv like ps2r_vu: the
# UNPACK lanes are bit-identity code proven against an -O2 OLD build.

add_library(ps2r_vif STATIC
    vif.cpp
    vif_fast_span.cpp
    vif_mmio_gate.cpp
    vif_unpack.cpp
    vif_unpack_bulk.cpp
    vif_unpack_table.cpp
    savestate/vifs_section.cpp)
target_include_directories(ps2r_vif PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_vif PRIVATE -Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv)
target_link_libraries(ps2r_vif PUBLIC ps2r_vu ps2r_gif ps2r_ee ps2r_core)

# PS2R_SIMD (root option): the NEON bodies of the two bulk UNPACK lanes and
# the table lane. The
# scalar bodies are compiled when it is off; the equivalence arm proves either
# body byte-identical to the scalar reference loop.
if(PS2R_SIMD)
    target_compile_definitions(ps2r_vif PUBLIC PS2R_SIMD=1)
endif()
