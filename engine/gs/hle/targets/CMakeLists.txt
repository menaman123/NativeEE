# engine/gs/hle/targets -- render targets above the backend interface: VramLayout
# (the title's VRAM map as a struct), RenderTarget (identity / views / coherence),
# ReadbackEngine (GPU -> mirror deposits), RenderTargetCache, TargetRefresher,
# ScalePolicy, TargetDims (the per-draw render size rule). Talks to the GPU only through render::Backend.
add_library(ps2r_gs_hle_targets STATIC
    vram_layout.cpp
    logical_rect.cpp
    coherence_state.cpp
    render_target.cpp
    readback_engine.cpp
    render_target_cache.cpp
    target_refresher.cpp
    scale_policy.cpp
    alias_page_set.cpp
    pending_rt_sync_ledger.cpp
    target_dims.cpp)
target_include_directories(ps2r_gs_hle_targets PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_targets PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_targets PUBLIC ps2r_gs_ir ps2r_gs_vram ps2r_render)
