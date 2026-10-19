// render/backends/metal/metal_shader_sources.h -- MetalShaderSources: the ONE copy of the
// MSL text of the Metal backend (the PS2 register program ported function by function
// from the GL arm's GLSL, the scanout quad, the pack-5551 / alpha-lane / half-word
// shuffle passes, the constant quad), plus the C++ mirror of the programs' uniform
// blocks (scalar members only, so the host struct and the MSL struct pack identically).
#pragma once

#include <cstdint>

namespace ps2r {
namespace render {

// The register program's fragment uniforms (MSL `FragUniforms`, buffer(0) of the fragment
// stage). Scalar int32 / float members in this order; the host writes the whole block
// per draw (setFragmentBytes). Field meanings = the GL arm's u_* uniforms of the same name.
struct MetalFragUniforms {
    int32_t use_texture = 0;
    float tex_size_x = 1.f, tex_size_y = 1.f;
    int32_t blend_a = 0, blend_b = 0, blend_c = 0, blend_d = 0;
    float fix_alpha = 0.f;
    int32_t blend_enabled = 0;
    int32_t sw_blend = 0;
    int32_t flip_v = 0;
    int32_t tfx = 0, tcc = 0;
    int32_t mip_levels = 0, mip_lcm = 0;
    float mip_k = 0.f, mip_l = 1.f;
    int32_t tex_lod_hw = 0;
    int32_t fba = 0, colclamp = 1, scanmsk = 0;
    int32_t ate = 0, atst = 0, aref = 0, afail = 0, afail_pass = 0;
    int32_t date = 0, datm = 0;
    int32_t fog_enabled = 0;
    float fogcol_r = 0.f, fogcol_g = 0.f, fogcol_b = 0.f;
    int32_t fbmsk16_en = 0, fbmsk16 = 0;
    int32_t fbmsk32_en = 0, fbmsk32 = 0;
    int32_t depth_fmt = 0, depth_zoff = 0;
    int32_t idx_fmt = 0, idx_self = 0;
    int32_t uv_native = 0, draw_scale = 1;
    int32_t fb16 = 0, ct16q = 0, dthe = 0, dimx_lo = 0, dimx_hi = 0;
    int32_t zsrc_en = 0, zsrc_num = 1, zsrc_den = 1;
    int32_t clut_raw = 0;
    // The colour write mask applied IN the program (bit 0 R, 1 G, 2 B, 3 A): a masked channel
    // writes the destination value back (the attachment is always fully writable).
    int32_t color_mask = 0xF;
};

struct MetalVertUniforms {
    float screen_size_x = 1.f, screen_size_y = 1.f;
};

// The scanout quad's uniforms: the source rectangle in normalised texels and the
// window-row order (flip_rows = 1 stores row 0 at the window's bottom, the GL readback order).
struct MetalScanoutUniforms {
    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    int32_t flip_rows = 0;
};

struct MetalHalfwordUniforms {
    int32_t dest_hi = 0, mask16 = 0, ta0 = 0, ta1 = 0, aem = 0;
};

struct MetalAlphaLaneUniforms {
    int32_t origin_x = 0, origin_y = 0, scale = 1;
    int32_t ex0 = 0, ey0 = 0, ex1 = 0, ey1 = 0;
};

struct MetalConstUniforms {
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
};

class MetalShaderSources {
public:
    // The whole MSL library (every program in one source: one newLibraryWithSource).
    static const char* Library();
    // Function names inside the library.
    static constexpr const char* kPs2Vertex = "ps2_vertex";
    static constexpr const char* kPs2Fragment = "ps2_fragment";
    static constexpr const char* kFullscreenVertex = "fullscreen_vertex";
    static constexpr const char* kScanoutVertex = "scanout_vertex";
    static constexpr const char* kScanoutFragment = "scanout_fragment";
    static constexpr const char* kPack5551Fragment = "pack5551_fragment";
    static constexpr const char* kAlphaLaneFragment = "alpha_lane_fragment";
    static constexpr const char* kHalfwordFragment = "halfword_fragment";
    static constexpr const char* kConstFragment = "const_fragment";
};

}  // namespace render
}  // namespace ps2r
