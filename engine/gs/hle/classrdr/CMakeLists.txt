# engine/gs/hle/classrdr -- the per-draw pipeline above the backend interface
# (DrawPipeline), the draw-class taxonomy / registry / dispatch, the state-shape
# vocabulary (DrawShapeSpec), the replica class renderers, the stencil-composite
# sequence FSM and the column-shuffle snap. Backend-only GPU access.
add_library(ps2r_gs_hle_classrdr STATIC
    class_classifier.cpp
    class_registry.cpp
    draw_shape_spec.cpp
    texture_shuffle_snap.cpp
    sequence_recipe.cpp
    draw_pipeline.cpp
    draw_route_trace.cpp
    target_dump.cpp
    skinned_renderer.cpp
    world_tristrip_renderer.cpp
    stencil_composite_renderer.cpp
    sprite_hud_renderer.cpp
    particles_renderer.cpp
    fmv_blit_renderer.cpp)
target_include_directories(ps2r_gs_hle_classrdr PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_classrdr PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_classrdr PUBLIC ps2r_gs_hle_present ps2r_gs_hle_texsrc ps2r_gs_hle_targets)
