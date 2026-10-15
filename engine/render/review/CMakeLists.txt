# engine/render/review -- FrameReview: the display-rate re-view of a completed
# frame (a Backend proxy that records one frame's commands and re-issues them with
# the camera interpolated; video.render_rate=display). Includes engine/render/backend.h
# and the DrawIr / TextureSource records only.
add_library(ps2r_render_review STATIC
    frame_review.cpp)
target_include_directories(ps2r_render_review PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_render_review PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_render_review PUBLIC ps2r_render ps2r_gs_ir)
