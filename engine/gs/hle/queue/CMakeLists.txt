# engine/gs/hle/queue -- the DrawIR consumer boundary: DrawIrQueueConsumer lowers
# the GS state machine's DrawIrSink stream into DrawPipeline in stream order (the
# in-thread form) and DrawIrThreadQueue carries that stream to a consumer sink on
# its own host thread (the cross-thread ring; lockstep or pipelined). The RUNDRAW
# merge action is a later module of this dir.
add_library(ps2r_gs_hle_queue STATIC
    draw_ir_queue_consumer.cpp
    draw_ir_thread_queue.cpp)
target_include_directories(ps2r_gs_hle_queue PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_queue PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_queue PUBLIC ps2r_gs_hle_classrdr ps2r_core)
