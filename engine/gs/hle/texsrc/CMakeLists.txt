# engine/gs/hle/texsrc -- texture-source resolution above the backend interface:
# ContentKey, DecodeCache, ClutSource, ClutHealCache, UvBbox, UvRegion, WindowInTarget,
# MipSampler, TextureDecoder (over gs/format), TexresTrace (the source-composition
# census), TextureDumpSink (the texture-oracle observer of the decode fallback), OwnedTextureSlots (slots an owner uploaded without bytes) and TextureResolver
# (ResolveRead).
add_library(ps2r_gs_hle_texsrc STATIC
    content_key.cpp
    decode_cache.cpp
    clut_source.cpp
    clut_heal_cache.cpp
    uv_bbox.cpp
    uv_region.cpp
    window_in_target.cpp
    mip_sampler.cpp
    texture_decoder.cpp
    texres_trace.cpp
    texture_dump_sink.cpp
    owned_texture_slots.cpp
    texture_resolver.cpp)
target_include_directories(ps2r_gs_hle_texsrc PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gs_hle_texsrc PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gs_hle_texsrc PUBLIC ps2r_gs_hle_targets ps2r_gs_format ps2r_core)
