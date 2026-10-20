# tests/render -- Phase 6 stage 6C suites over ps2r_render_gl + ps2r_platform_sdl.
# GL-needing tests create a hidden SDL window + 4.1 core context and exit 77 (SKIP)
# when no context can be made.

function(ps2r_render_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_render_gl ps2r_platform_sdl ps2r_gs_ir ps2r_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
endfunction()

ps2r_render_test(test_blend_ff_map)
ps2r_render_test(test_cd_copy_policy)
ps2r_render_test(test_vertex_convert)
ps2r_render_test(test_gl_shader_library)
ps2r_render_test(test_gl_state_cache)
ps2r_render_test(test_gl_device_smoke)
ps2r_render_test(test_gl_async_readback)
ps2r_render_test(test_gl_raster_rule)
ps2r_render_test(test_gl_alpha_convention)
ps2r_render_test(test_gl_alpha_fail_split)
ps2r_render_test(test_gl_date_blend)
target_link_libraries(test_gl_alpha_convention PRIVATE ps2r_gs_hle_texsrc ps2r_gs_format)
ps2r_render_test(test_gl_present)
ps2r_render_test(test_blend_equation_pin)
ps2r_render_test(test_sw_blend_integer)
ps2r_render_test(test_s9_stencil_smoke)
ps2r_render_test(test_inherited_uniforms)
ps2r_render_test(test_texture_filter)
if(APPLE)   # ps2r_render_metal (engine/render is configured after tests/: test on the platform)
    add_executable(test_metal_device_smoke test_metal_device_smoke.cpp)
    target_link_libraries(test_metal_device_smoke PRIVATE ps2r_render_metal ps2r_gs_ir ps2r_core)
    target_compile_options(test_metal_device_smoke PRIVATE -Wall -Wextra -Werror -O2)
    # The Metal arm's device smoke (the GL smoke's checks on MetalDevice; SKIP without a device).
    add_test(NAME Render_MetalDeviceSmoke COMMAND test_metal_device_smoke)
    set_tests_properties(Render_MetalDeviceSmoke PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# The rendering pins over a title's frozen PCSX2 baselines (title-screen draws, whole-frame
# replays, the attract-sequence correctness gate) were ONE title's: they keyed on that title's
# dumps and screenshots as private fixtures, and on ps2r_title_renderer_profile. A title module
# brings its own rendering pins. The two script selftests below are title-neutral and stay.
add_test(NAME Render_AttractReplaySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/attract_replay_pin.py --selftest)
set_tests_properties(Render_AttractReplaySelftest PROPERTIES TIMEOUT 60)
add_test(NAME ToolsGsf1ExportSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gsdump/gsf1_export.py --selftest)
set_tests_properties(ToolsGsf1ExportSelftest PROPERTIES TIMEOUT 60)

# G6: the ALPHA -> fixed-function table incl. the REVERSE_SUBTRACT arms (invariant 34),
# the wrap route predicate and the wrap reference.
add_test(NAME Render_BlendFfMapTable COMMAND test_blend_ff_map)
add_test(NAME Gs_Inv34_ReverseSubtract COMMAND test_blend_ff_map)
set_tests_properties(Render_BlendFfMapTable Gs_Inv34_ReverseSubtract PROPERTIES TIMEOUT 120)

# The Cd-copy elision with the dirty-rect list: strips skip past the union, a copy drops the
# rects inside its box, overflow falls back to the union, force_full / Invalidate clear.
add_test(NAME Render_CdCopyPolicyList COMMAND test_cd_copy_policy)
set_tests_properties(Render_CdCopyPolicyList PROPERTIES TIMEOUT 120)

# G6: fused conversion == the straightforward reference loops (3 modes, random verts, bbox rules).
add_test(NAME Render_VertexConvert COMMAND test_vertex_convert)
set_tests_properties(Render_VertexConvert PROPERTIES TIMEOUT 120)

# A2 (Charter 2): the texture-filtering enhancement's pure mapping -- `title`/1 the identity for
# every TEX1, point/bilinear/trilinear forced over the owned chain, anisotropy snapped and clamped.
add_test(NAME Render_TextureFilter COMMAND test_texture_filter)
set_tests_properties(Render_TextureFilter PROPERTIES TIMEOUT 60)

# G6: the baseline shader text hash, both programs compile + link, uniform table complete.
add_test(NAME Render_ShaderCompiles COMMAND test_gl_shader_library)
# 6E': the stencil-composite (S9) shader texts hash-identical to OLD + the three programs link.
add_test(NAME Render_S9ShaderText COMMAND test_gl_shader_library)
set_tests_properties(Render_ShaderCompiles Render_S9ShaderText PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# G6: cached binds vs raw binds leave identical GL state.
add_test(NAME Render_StateCacheEquivalence COMMAND test_gl_state_cache)
set_tests_properties(Render_StateCacheEquivalence PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Device smoke: surface create / clear / readback, one untextured triangle through the executor.
add_test(NAME Render_DeviceSmoke COMMAND test_gl_device_smoke)
# BeginReadback / PollReadback: a pack buffer behind a fence hands back the synchronous read's bytes.
add_test(NAME Render_AsyncReadback COMMAND test_gl_async_readback)
set_tests_properties(Render_AsyncReadback PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
set_tests_properties(Render_DeviceSmoke PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The GS pixel coverage rule (integer sample point, x0 <= i < x1) through the executor.
add_test(NAME Render_RasterRule COMMAND test_gl_raster_rule)
set_tests_properties(Render_RasterRule PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The ONE alpha convention: TEXA'd CT16 texels, the dual-source As factor, the stored byte.
add_test(NAME Render_AlphaConvention COMMAND test_gl_alpha_convention)
set_tests_properties(Render_AlphaConvention PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The per-pixel alpha test's two populations: AFAIL FB_ONLY / RGB_ONLY / ZB_ONLY / KEEP write
# masks for the failing pixels, FBMSK / ZMSK for the passing ones, As untouched by the test.
add_test(NAME Render_AlphaFailSplit COMMAND test_gl_alpha_fail_split)
set_tests_properties(Render_AlphaFailSplit PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# A DATE-only destination copy of a blended draw: the draw's OWN ALPHA tuple, once (fixed-
# function when exact -- the shadow counter's self-overlapping faces accumulate -- else the
# shader arm), the copy tracker channel-aware (alpha-masking strips do not re-copy).
add_test(NAME Render_DateBlend COMMAND test_gl_date_blend)
set_tests_properties(Render_DateBlend PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Window-pixel pins: logical content crop, orientation, allocation scale and
# failure before clear/swap. Includes the unchanged full-image present mapping.
add_test(NAME Render_Present COMMAND test_gl_present)
set_tests_properties(Render_Present PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# The GS alpha blend equation ((A-B)*C>>7)+D on the GL arms for the title's ALPHA tuples,
# plus the MODULATE 0x80=1.0 convention and the texel-alpha chain feeding As (sample pixels).
add_test(NAME Render_BlendEquationPin COMMAND test_blend_equation_pin)
set_tests_properties(Render_BlendEquationPin PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
# The shader blend arm's integer equation ((A - B) * C >> 7 floors the signed product).
add_test(NAME Render_SwBlendInteger COMMAND test_sw_blend_integer)
set_tests_properties(Render_SwBlendInteger PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# 6E'': the stencil-composite primitives on a real context -- D24S8 surface, ClearStencil,
# one strip's three passes (StripCount + two const quads), the 32 materialise quads
# (byte == 8 * stencil), the -8 return, the extent scissor, the frame clear on a strip.
add_test(NAME Render_S9StencilSmoke COMMAND test_s9_stencil_smoke)
set_tests_properties(Render_S9StencilSmoke PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# 6F': DrawParams::inherited_uniforms -- the reference's class-native generic arms never
# write u_ct16q / u_dthe / u_dimx / u_draw_scale / u_uv_native (+ blend a..d, zsrc map per
# shape), so a class-routed CT32 draw after a CT16 frame draw lands 5-bit quantised
# (P6-NATIVE-STALE-UNIFORMS, load_transition vf855).
add_test(NAME Render_InheritedUniforms COMMAND test_inherited_uniforms)
set_tests_properties(Render_InheritedUniforms PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# G6: engine/render/backends/gl includes only the section 3 allowance.
add_test(NAME Render_IncludeDepth
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/include_depth.py ${CMAKE_SOURCE_DIR})
set_tests_properties(Render_IncludeDepth PROPERTIES TIMEOUT 60)

# P8.33-B evidence tooling: the per-draw last-writer partition of a replay-vs-screenshot difference
# (tools/gsdump/replay_diff_partition.py; SKIP 77 without numpy). Runs the PATH's python3 on purpose: the
# configured ${Python3_EXECUTABLE} (the framework 3.14) carries no numpy and would only ever SKIP.
add_test(NAME ToolsReplayDiffPartitionSelftest COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/gsdump/replay_diff_partition.py --selftest)
set_tests_properties(ToolsReplayDiffPartitionSelftest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# Charter 2 A3: FrameReview's re-view arithmetic (the 4x4 inverse, the camera slerp /
# position lerp with the cut thresholds, the per-vertex re-projection K = P^-1 V^-1 C P:
# identity at phase 0, the parallax of a camera translation, a yaw's slerp midway, the STQ
# rescale, the behind-camera refusal). No GL.
add_executable(test_frame_review test_frame_review.cpp)
target_include_directories(test_frame_review PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_frame_review PRIVATE ps2r_render_review)
target_compile_options(test_frame_review PRIVATE -Wall -Wextra -Werror)
add_test(NAME Render_FrameReview COMMAND test_frame_review)
set_tests_properties(Render_FrameReview PROPERTIES TIMEOUT 60)
