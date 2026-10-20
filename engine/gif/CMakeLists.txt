# engine/gif -- the GIF: GIFtag parse, the GsSink seam to the GS (its recording
# implementation RegisterOpStream lives in engine/gs/hle/ir), the register
# file, the PATH1/2/3 parser, the arbiter (inline + MTVU markers / inbox /
# boundary slots), the drain thread and the GIFS section codec (savestate/). Links
# ps2r_core (hash), ps2r_ee (MmioHandler) and ps2r_gs_format (the only
# engine/gs dependency the GIF may take).

add_library(ps2r_gif STATIC
    gs_sink.cpp
    null_gs_sink.cpp
    gif_tag.cpp
    gif_regs.cpp
    gif_register_dispatch.cpp
    gif_parser.cpp
    gif_arbiter.cpp
    gif_finish_scan.cpp
    gif_drain_thread.cpp
    savestate/gifs_section.cpp)
target_include_directories(ps2r_gif PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gif PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gif PUBLIC ps2r_core ps2r_ee ps2r_gs_format)

# PS2R_SIMD (root option): the strip3 PACKED lane's NEON twin (StageTriple).
# The scalar body is compiled when it is off; GifParserEquivalence proves
# either body identical to the per-quadword path.
if(PS2R_SIMD)
    target_compile_definitions(ps2r_gif PUBLIC PS2R_SIMD=1)
endif()

# engine/gif/hle -- the GsSink implementations the composition root hands the
# parser: GsStateSink (the GS façade, Phase 6's ps2r_gs_state) and GsSinkFanout
# (one stream to N sinks, so the register-op recorder stays attached). Kept out
# of ps2r_gif so the GIF proper never depends on the GS state machine.
add_library(ps2r_gif_hle STATIC
    hle/gs_state_sink.cpp
    hle/gs_sink_fanout.cpp)
target_include_directories(ps2r_gif_hle PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gif_hle PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gif_hle PUBLIC ps2r_gif ps2r_gs_state)
