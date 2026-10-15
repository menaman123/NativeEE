# engine/gs/hle/ir -- the DrawIR record family: the .gsdl on-disk format, DrawIr,
# the DirectOps / DrawIrLift dual converters, the DrawIrVerifier (kick equivalence)
# the DrawIrSink boundary, the counting / forwarding CountingDrawIrSink, the
# bounded DrawlistRing (the crash path's last-200-frame drawlist) and the
# Phase-5 RegisterOpStream (the recording GsSink; GsSink itself stays in
# engine/gif). Links the GS format layer and ps2r_core (the FNV-1a hash); the
# engine/gif headers it includes are header-only.

add_library(ps2r_gs_ir STATIC
    gsdl_format.cpp
    draw_ir.cpp
    direct_ops.cpp
    draw_ir_lift.cpp
    draw_ir_verifier.cpp
    counting_draw_ir_sink.cpp
    drawlist_ring.cpp
    register_op_stream.cpp)
target_include_directories(ps2r_gs_ir PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_ir PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_ir PUBLIC ps2r_gs_format ps2r_core)
