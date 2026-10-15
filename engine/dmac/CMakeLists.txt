# engine/dmac -- the EE DMA controller: channel register file + MMIO map
# (unregistered this phase), the VIF/GIF chain walker and normal transfers,
# the deferred VIF1 kick + END-slot tail chase, the handler registry, and the
# TAG_DMAC section codec (unregistered). Compiled at -O2 like its siblings.

add_library(ps2r_dmac STATIC
    dmac.cpp
    dmac_spr_transfer.cpp
    chain_walker.cpp
    dmac_deferred_vif1.cpp
    dmac_handler_registry.cpp
    dmac_mtvu_feed.cpp
    hle/libdma_bindings.cpp)
target_include_directories(ps2r_dmac PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_dmac PRIVATE -Wall -Wextra -Werror -O2)
target_link_libraries(ps2r_dmac PUBLIC ps2r_vif ps2r_gif ps2r_vu ps2r_ee ps2r_core)
