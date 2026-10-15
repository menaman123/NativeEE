# engine/gs/xfer -- the transfer engine (BITBLTBUF/TRXPOS/TRXREG/TRXDIR/HWREG).
add_library(ps2r_gs_xfer STATIC transfer_engine.cpp)
target_include_directories(ps2r_gs_xfer PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_xfer PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_xfer PUBLIC ps2r_gs_vram ps2r_gs_format ps2r_gs_ir)
