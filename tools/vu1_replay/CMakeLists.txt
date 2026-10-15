# alone (--mode / --compare) and the title's VU1 HLE library against the
# interpreter (--hle / --catalog). Built at -O2 with the same pins as ps2r_vu;
# the tool itself does no float math.

# The oracle's GIF-side helpers (decode / normalise / diff, the typed-op
# projection), shared with tests/vu.
add_library(ps2r_vu1_replay_lib STATIC
    gs_packet_diff.cpp
    gs_direct_ops_check.cpp
    xgk_capture_sink.cpp)
target_include_directories(ps2r_vu1_replay_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_vu1_replay_lib PRIVATE -Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv)
target_link_libraries(ps2r_vu1_replay_lib PUBLIC ps2r_vu ps2r_gs_ir)

# ps2r_vu carries ps2r_gif, ps2r_ee, ps2r_core and ps2r_gs_format as PUBLIC
# links; naming them again here makes ld warn about duplicate libraries.
