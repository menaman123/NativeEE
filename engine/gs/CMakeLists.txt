# engine/gs -- Graphics Synthesizer. Phase 2 builds ONLY the format layer
# (gs/format: pure math, no SDL/GL, no runtime consumer). state/, vram/, xfer/
# and hle/ are Phase 6 and are added here by that phase.

add_library(ps2r_gs_format STATIC
    format/psm_layout.cpp
    format/swizzle.cpp
    format/clut_layout.cpp
    format/texa_expand.cpp
    format/pixel_convert.cpp
    format/block_decode.cpp
    format/mip_chain.cpp)
target_include_directories(ps2r_gs_format PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_format PRIVATE -Wall -Wextra -Werror)

# PS2R_SIMD (root option): BlockDecode's vectorized 16-bit expansion (NEON). The
# scalar path is always compiled and the property test proves both byte-identical.
if(PS2R_SIMD)
    target_compile_definitions(ps2r_gs_format PUBLIC PS2R_SIMD=1)
endif()

# Stage 3D2: the boundary OWNER (hle/sce/gs_sync) -- the sceGsSyncV / WaitForVblank
# bodies and the generated TU's row-15 symbols. Separate from the format layer: it
# depends on ps2r_ee (BoundarySequencer, SdkBindingManifest).
# hle/sce/libgraph_bindings.{h,cpp}: the two other libgraph rows (sceGsResetGraph,
# sceGsSyncVCallback) over ps2r::gs::GsPrivRegs, which is compiled ONLY in
# ps2r_gs_state (linked here, never re-listed: one definition of the class).
add_library(ps2r_gs_sync STATIC hle/sce/gs_sync.cpp hle/sce/libgraph_bindings.cpp)
target_include_directories(ps2r_gs_sync PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_sync PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_sync PUBLIC ps2r_ee ps2r_gs_state)

add_subdirectory(hle/ir)  # Phase 6
add_subdirectory(vram)  # Phase 6
add_subdirectory(xfer)  # Phase 6
add_subdirectory(state)  # Phase 6

add_subdirectory(hle/targets)  # Phase 6
add_subdirectory(hle/present)  # Phase 6
add_subdirectory(hle/texsrc)  # Phase 6
add_subdirectory(hle/classrdr)  # Phase 6
add_subdirectory(hle/queue)  # Phase 6 (integration: the DrawIR consumer)
add_subdirectory(hle/capture)  # the .gsstream consumer-stream capture (the throughput harness)
