# `engine/render/backends/gl/`

**Phase:** 6  
**Purpose:** OpenGL 4.1 core adapter: device, single-source shader library, uniform cache, draw executor, vertex stream, texture pool, state cache (device member), blend map (REVERSE_SUBTRACT), vertex convert, pipeline cache.

## Contents (stage 6C; lib `ps2r_render_gl`, `find_package(OpenGL)`, `GL_SILENCE_DEPRECATION` public)

- `gl_device.h/.cpp` -- `GlDevice : render::Backend` (surfaces = texture [+ fbo], pool-backed plain textures, dummy texture on units 1-4, sync readback, immediate staging API, scanout quad present, run brackets; `CopyRect(kCopyPack5551)` = the Pack5551 program over the NDC triangle into the destination FBO, scissored -- the 16-bit companion refresh without a GPU -> CPU round trip; `Caps::copy_pack5551` advertises it) + `GlDeviceSettings{scale=1}` + the `GlSurface` record.
- `gl_shader_library.h/.cpp` -- `GlShaderLibrary` (compile/link, `GlUniformLocations` table, sampler unit priming) ; `gl_shader_sources.h/.cpp` -- `GlShaderSources`: the ONE copy of the GLSL text (baseline arm: `GSMIP_AAF 1`, `S83_IDXSCALE 1` after `#version`).
- `gl_uniform_cache.h/.cpp` -- `GlUniformCache` (per-location value shadow, 1i/1f/2f/2i/3f/4f).
- `gl_state_cache.h/.cpp` -- `GlStateCache` (bind + fixed-function shadows, delete-reverts-to-0).
- `gl_vertex_stream.h/.cpp` -- `GlVertexStream` (VAO, 16 MiB ring, run brackets); `gpu_vertex.h` -- the 32-byte `GpuVertex`.
- `vertex_convert.h/.cpp` -- `VertexConvert` (Untextured / UvFixed / Stq + fused bboxes).
- `gl_texture_pool.h/.cpp` -- `GlTexturePool` (fenced recycle, caps 128 / 96 MiB, `BoxHalve`).
- `blend_ff_map.h/.cpp` -- `BlendFfMap` (ALPHA -> GL table incl. REVERSE_SUBTRACT and the `(Cs-Cd)*FIX` SUBTRACT arm, wrap route, wrap reference; pinned against the PS2 equation on GL by `Render_BlendEquationPin`).
- `cd_copy_policy.h/.cpp` -- `CdCopyPolicy` (union-rect lazy Cd-copy elision over `render::CdCopyState`).
- `gl_draw_executor.h/.cpp` -- `GlDrawExecutor` (one DrawIr -> one glDrawArrays; steps in the pixel-defining order).

## Belongs here

- GlDevice, GlShaderLibrary, GlUniformCache, GlDrawExecutor, GlVertexStream, GlTexturePool, GlStateCache, BlendFfMap, VertexConvert, GlPipelineCache

## Does not belong here

- compute-raster arm (deleted), TargetFBO / serve logic (gs/hle)

## Transplanted from (OLD, read-only)

- `opengl_backend.{h,cpp} GL regions, gl_state_cache.h, blend_ff_map.h, graphics/vconv.h`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
