# tools/gs_replay -- the GS replay tool over the NEW GS HLE layers and the GL backend.
# On this tree (no renderer profile): `stream` = the renderer throughput harness (the
# consumer alone over a .gsstream capture, engine/gs/hle/capture).
# A composition root like app/host: it may link engine/*. Reads no environment.
add_executable(gs_replay
    gs_replay.cpp
    stream_replay_driver.cpp)
target_include_directories(gs_replay PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(gs_replay PRIVATE -Wall -Wextra -Werror -O2)
target_link_libraries(gs_replay PRIVATE ps2r_gs_hle_capture ps2r_gs_hle_queue ps2r_render_gl ps2r_platform_sdl)
