# engine/gs/hle/capture -- the `.gsstream` consumer-stream capture: the format, the
# recorder (a DrawIrSink + VramMirror decorator the host puts in front of its queue)
# and the streaming reader (tools/gs_replay stream, the throughput harness).
add_library(ps2r_gs_hle_capture STATIC
    gs_stream_recorder.cpp
    gs_stream_reader.cpp)
target_include_directories(ps2r_gs_hle_capture PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_capture PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_capture PUBLIC ps2r_gs_ir ps2r_gs_vram ps2r_gs_hle_present)
