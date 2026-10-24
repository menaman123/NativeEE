# engine/gs/vram -- the single-writer VRAM mirror (VramImage + PageRecord).
add_library(ps2r_gs_vram STATIC page_record.cpp page_occupancy.cpp vram_image.cpp vram_op_log.cpp)
target_include_directories(ps2r_gs_vram PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_vram PRIVATE -Wall -Wextra -Werror)
