# engine/gs/state -- register file, privileged registers, vertex kick, THE flush,
# the GsState façade and the EE-side MMIO handler of the privileged page; plus
# the GS-owned savestate sections (../savestate: VRAM + GSPR + GSST over GsState),
# which live in this library so the composition root links nothing new for them.
add_library(ps2r_gs_state STATIC
    gs_registers.cpp
    gs_priv_regs.cpp
    vertex_kick.cpp
    draw_flush.cpp
    gs_state.cpp
    gs_mmio_handler.cpp
    ../savestate/gs_sections.cpp)
target_include_directories(ps2r_gs_state PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_state PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_state PUBLIC ps2r_gs_xfer ps2r_gs_vram ps2r_gs_ir ps2r_core)
