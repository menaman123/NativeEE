# engine/render/backends/metal -- the Metal adapter (Apple GPUs: programmable blending through
# the framebuffer fetch). macOS only: the directory is entered by engine/render/CMakeLists.txt
# under APPLE; the Objective-C++ bodies (.mm) need the OBJCXX language.
enable_language(OBJCXX)

add_library(ps2r_render_metal STATIC
    metal_shader_sources.cpp
    metal_pipeline_cache.mm
    metal_device.mm
    metal_draw_executor.mm)
target_include_directories(ps2r_render_metal PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_render_metal PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(ps2r_render_metal PUBLIC PS2R_HAS_METAL=1)
# The GL arm's title-neutral helpers (VertexConvert, CdCopyPolicy, BlendFfMap, GpuVertex) are shared.
target_link_libraries(ps2r_render_metal PUBLIC ps2r_render ps2r_render_gl ps2r_gs_ir
    "-framework Metal" "-framework QuartzCore" "-framework Foundation")

# metal_microbench: the GPU roofline microbenchmark (PERF1) -- fill / depth / blend / framebuffer-fetch /
# texture sample / ALU / upload / readback / command-buffer round trip / render-pass switch rates on this
# device at the scale-4 surface. Standalone over Metal (no engine objects, no window); rows print as key=value.
add_executable(metal_microbench metal_microbench.mm)
target_compile_options(metal_microbench PRIVATE -Wall -Wextra -Werror -fobjc-arc)
target_link_libraries(metal_microbench PRIVATE "-framework Metal" "-framework Foundation")
