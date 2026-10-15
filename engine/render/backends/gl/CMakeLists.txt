# engine/render/backends/gl -- the OpenGL 4.1 core adapter (GlDevice and its parts).
# Includes only engine/render/backend.h, engine/gs/hle/ir/*.h and
# engine/gs/hle/texsrc/texture_source.h (ctest Render_IncludeDepth).
include(${CMAKE_SOURCE_DIR}/cmake/OpenGlDependency.cmake)

add_library(ps2r_render_gl STATIC
    gl_device.cpp
    gl_shader_library.cpp
    gl_shader_sources.cpp
    gl_uniform_cache.cpp
    gl_state_cache.cpp
    gl_vertex_stream.cpp
    vertex_convert.cpp
    gl_texture_pool.cpp
    blend_ff_map.cpp
    cd_copy_policy.cpp
    gl_draw_executor.cpp)
target_include_directories(ps2r_render_gl PUBLIC ${CMAKE_SOURCE_DIR})
# The GL header is PS2R_GL_HEADER from engine/platform/host/platform.h (the ONE
# host header; it also defines GL_SILENCE_DEPRECATION / GL_GLEXT_PROTOTYPES). The
# definition stays PUBLIC for a dependent that includes the header before ours.
target_compile_definitions(ps2r_render_gl PUBLIC GL_SILENCE_DEPRECATION)
target_compile_options(ps2r_render_gl PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_render_gl PUBLIC ps2r_render ps2r_platform OpenGL::GL)
