// render/backends/metal/metal_shader_sources.cpp -- the ONE copy of the Metal backend's MSL text.
//
// The PS2 register program is the GL arm's kPs2FragSrc (gl_shader_sources.cpp) ported
// function by function: the same integer GS blend equation, TFX, alpha-test / DATE /
// FBA / FBMSK / COLCLAMP / 16-bit frame arms, the same stored-alpha convention and the
// same exact window depth (v_z * 2^-32 as the fragment's depth output). Where the GL text
// reads the destination copy at the fragment's own position (u_rt_texture at
// gl_FragCoord) this text reads the framebuffer-fetch input `dst` ([[color(0)]]): the
// attachment's current value, so the blend / DATE / mask merges need no copy and no
// render-pass break; the UV-addressed self read (idx fmt 4, u_idx_self) still reads the
// copy texture (texture 1). The colour write mask is applied here (masked channels
// write `dst` back) so one pipeline state serves every FBMSK. GLSL `mod` is spelled
// x - y * floor(x / y) (Metal's fmod truncates); everything else maps 1:1.
#include "engine/render/backends/metal/metal_shader_sources.h"

namespace ps2r {
namespace render {

namespace {

const char* const kLibrary = R"MSL(
#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------- the PS2 register program
struct GpuVertexIn {
    float x, y, z;        // PS2 window pixels, 32-bit depth as float
    float u, v, q;        // texel-space S*w, T*h (or UV/16) and the raw Q
    uchar4 rgba;          // colour bytes
    uchar fog;
    uchar pad0, pad1, pad2;
};

struct VertUniforms {
    float screen_size_x, screen_size_y;
};

struct RasterData {
    float4 position [[position]];
    float3 texcoord [[center_no_perspective]];   // S, T (texel space) + Q, screen-linear
    float4 color;
    float fog;
    float z [[center_no_perspective]];           // the PS2 z itself, screen-linear
};

vertex RasterData ps2_vertex(uint vid [[vertex_id]],
                             const device GpuVertexIn* verts [[buffer(0)]],
                             constant VertUniforms& U [[buffer(1)]]) {
    const GpuVertexIn v = verts[vid];
    RasterData o;
    // The GS coverage rule (integer sample point, x0 <= i < x1) against Metal's pixel
    // centres: translate by +0.5 px minus 1/256 px (the GL arm's constant).
    float2 gs_px = float2(v.x, v.y) + float2(0.49609375);
    float2 ndc = (gs_px / float2(U.screen_size_x, U.screen_size_y)) * 2.0 - 1.0;
    // PS2 row 0 at the TOP of the attachment (texture row 0): the same memory order as the
    // GL arm's bottom-origin attachments, so every readback / upload / position agrees.
    ndc.y = -ndc.y;
    // Clip z = z / 2^32 in Metal's [0, 1] clip space (clipping only; the fragment writes the
    // exact window depth).
    o.position = float4(ndc, v.z * exp2(-32.0), 1.0);
    o.z = v.z;
    o.texcoord = float3(v.u, v.v, v.q);
    o.color = float4(v.rgba) / 255.0;
    o.fog = float(v.fog) / 255.0;
    return o;
}

struct FragUniforms {
    int use_texture;
    float tex_size_x, tex_size_y;
    int blend_a, blend_b, blend_c, blend_d;
    float fix_alpha;
    int blend_enabled;
    int sw_blend;
    int flip_v;
    int tfx, tcc;
    int mip_levels, mip_lcm;
    float mip_k, mip_l;
    int tex_lod_hw;
    int fba, colclamp, scanmsk;
    int ate, atst, aref, afail, afail_pass;
    int date, datm;
    int fog_enabled;
    float fogcol_r, fogcol_g, fogcol_b;
    int fbmsk16_en, fbmsk16;
    int fbmsk32_en, fbmsk32;
    int depth_fmt, depth_zoff;
    int idx_fmt, idx_self;
    int uv_native, draw_scale;
    int fb16, ct16q, dthe, dimx_lo, dimx_hi;
    int zsrc_en, zsrc_num, zsrc_den;
    int clut_raw;
    int color_mask;
};

struct FragOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

static inline float3 GlslMod(float3 x, float y) { return x - y * floor(x / y); }

// Requantise an interpolated GS texel coordinate to its value at the NATIVE pixel centre
// (the GL arm's S83NativeCenter): exact for the affine mappings the aliasing serves carry.
static inline float2 NativeCenter(float2 rt, int draw_scale, float2 fc) {
    if (draw_scale <= 1) return rt;
    float s = float(draw_scale);
    float2 d = (floor(fc / s) + 0.5) * s - fc;
    return rt + dfdx(rt) * d.x + dfdy(rt) * d.y;
}

static inline uint ZFromDepth(float df) {
    return (df >= 1.0) ? 0xFFFFFFFFu : uint(max(df, 0.0) * exp2(32.0));
}

static inline float4 Expand5551(uint z16) {
    uint r5 = z16 & 0x1Fu;
    uint g5 = (z16 >> 5) & 0x1Fu;
    uint b5 = (z16 >> 10) & 0x1Fu;
    return float4(float((r5 << 3u) | (r5 >> 2u)),
                  float((g5 << 3u) | (g5 >> 2u)),
                  float((b5 << 3u) | (b5 >> 2u)),
                  ((z16 >> 15) != 0u) ? 255.0 : 0.0);
}

fragment FragOut ps2_fragment(RasterData in [[stage_in]],
                              float4 dst [[color(0)]],
                              constant FragUniforms& U [[buffer(0)]],
                              texture2d<float> u_texture [[texture(0)]],
                              texture2d<float> u_rt_copy [[texture(1)]],
                              texture2d<float> u_clut [[texture(2)]],
                              texture2d<float> u_idx_tex2 [[texture(3)]],
                              texture2d<float> u_zsrc_tex [[texture(4)]],
                              depth2d<float> u_texture_depth [[texture(5)]],
                              sampler u_sampler [[sampler(0)]]) {
    FragOut out;
    const float2 frag_xy = in.position.xy;
    // The window depth: d = PS2 z / 2^32 exactly. Written on every path, before any discard.
    float frag_z = in.z * exp2(-32.0);
    out.depth = frag_z;
    // --- PS2 SCANMSK ---
    if (U.scanmsk == 2) {
        if (int(frag_xy.y) % 2 == 0) discard_fragment();
    } else if (U.scanmsk == 3) {
        if (int(frag_xy.y) % 2 != 0) discard_fragment();
    }

    float4 result;
    const float2 tex_size = float2(U.tex_size_x, U.tex_size_y);

    if (U.use_texture != 0) {
        float inv_q = (in.texcoord.z != 0.0) ? (1.0 / in.texcoord.z) : 1.0;
        float2 uvn_rt = in.texcoord.xy * inv_q;
        if (U.uv_native != 0) uvn_rt = NativeCenter(uvn_rt, U.draw_scale, frag_xy);
        float2 uv = uvn_rt / tex_size;
        if (U.flip_v != 0) uv.y = 1.0 - uv.y;

        float4 vc = floor(in.color * 255.0 + 0.5);
        float4 tex;
        if (U.depth_fmt == 2) {
            // Z16-family view of a depth attachment.
            float df = u_texture_depth.sample(u_sampler, uv);
            uint d = ZFromDepth(df);
            uint z16 = min(d, 0xFFFFu);
            tex = Expand5551(z16);
        } else if (U.depth_fmt == 1) {
            // Native PSMZ32 read of a Z32 depth attachment: raw z bytes as RGBA.
            float df = u_texture_depth.sample(u_sampler, uv);
            uint z = ZFromDepth(df);
            tex = float4(float( z         & 0xFFu),
                         float((z >>  8u) & 0xFFu),
                         float((z >> 16u) & 0xFFu),
                         0.0);
            uint a8 = (z >> 24u) & 0xFFu;
            tex.a = float(a8);
        } else if (U.depth_fmt == 3) {
            // PSMZ16 view of a Z32 depth attachment: the closed-form aliasing remap.
            float2 rt = in.texcoord.xy * inv_q;
            rt = NativeCenter(rt, U.draw_scale, frag_xy);
            int2 t = int2(floor(rt));
            int lx = t.x & 63, ly = t.y & 63;
            int PX = (lx & 7) | (ly & 0x38);
            int PY = (ly & 7) | ((lx & 0x30) >> 1);
            int hw = (lx >> 3) & 1;
            int PXg = (t.x & ~63) | PX;
            int PYg = (t.y >> 6) * 32 + PY + U.depth_zoff;
            float2 uvz = (float2(PXg, PYg) + 0.5) / tex_size;
            if (U.flip_v != 0) uvz.y = 1.0 - uvz.y;
            float df = u_texture_depth.sample(u_sampler, uvz);
            uint z = ZFromDepth(df);
            uint z16 = (hw != 0) ? ((z >> 16u) & 0xFFFFu) : (z & 0xFFFFu);
            tex = Expand5551(z16);
        } else if (U.idx_fmt == 7) {
            // Depth-byte palette serve: this fragment's own depth texel, one byte of the GS Z.
            int2 ztc = int2(frag_xy);
            int2 zdim = int2(u_texture_depth.get_width(), u_texture_depth.get_height());
            ztc = clamp(ztc, int2(0), zdim - 1);
            float df = u_texture_depth.read(uint2(ztc));
            uint z = ZFromDepth(df);
            uint idx = (z >> uint(U.idx_self & 31)) & 0xFFu;
            tex = floor(u_clut.read(uint2(idx, 0u)) * 255.0 + 0.5);
        } else if (U.idx_fmt == 4) {
            // CLASS IDX-A: PSMT8H read of a CT32-class target: index = the stored alpha byte at
            // the same (x, y); u_idx_self reads the pre-draw destination COPY (texture 1).
            float2 rtc = in.texcoord.xy * inv_q;
            rtc = NativeCenter(rtc, U.draw_scale, frag_xy);
            int2 tdim4 = int2(tex_size);
            int2 tc = clamp(int2(floor(rtc)), int2(0), tdim4 - 1);
            if (U.flip_v != 0) tc.y = tdim4.y - 1 - tc.y;
            {
                int2 ts4 = (U.idx_self != 0) ? int2(u_rt_copy.get_width(), u_rt_copy.get_height())
                                             : int2(u_texture.get_width(), u_texture.get_height());
                tc *= max(ts4.x / max(tdim4.x, 1), 1);
            }
            float4 src = (U.idx_self != 0) ? u_rt_copy.read(uint2(tc)) : u_texture.read(uint2(tc));
            uint idx = uint(floor(src.a * 255.0 + 0.5));
            tex = floor(u_clut.read(uint2(idx, uint(U.clut_raw))) * 255.0 + 0.5);
        } else if (U.idx_fmt == 5) {
            // CLASS IDX-B: PSMT8H read over CT16-rendered pages (base + partner at +height rows).
            float2 rtc = in.texcoord.xy * inv_q;
            rtc = NativeCenter(rtc, U.draw_scale, frag_xy);
            int2 tc = int2(floor(rtc));
            int ppr = int(tex_size.x) >> 6;
            int page = (tc.y >> 5) * ppr + (tc.x >> 6);
            int lx = tc.x & 63, ly = tc.y & 31;
            int lX = (lx & 7) | 8 | ((ly & 0x18) << 1);
            int lY = (lx & 0x38) | (ly & 7);
            int X = (page % ppr) * 64 + lX;
            int Y = (page / ppr) * 64 + lY;
            int h = int(tex_size.y);
            int wmax = int(tex_size.x) - 1;
            float4 src;
            if (Y < h) {
                int2 t0 = clamp(int2(X, Y), int2(0), int2(wmax, h - 1));
                if (U.flip_v != 0) t0.y = h - 1 - t0.y;
                t0 *= max(int(u_texture.get_width()) / max(int(tex_size.x), 1), 1);
                src = u_texture.read(uint2(t0));
            } else {
                int2 t1 = clamp(int2(X, Y - h), int2(0), int2(wmax, h - 1));
                if (U.flip_v != 0) t1.y = h - 1 - t1.y;
                t1 *= max(int(u_idx_tex2.get_width()) / max(int(tex_size.x), 1), 1);
                src = u_idx_tex2.read(uint2(t1));
            }
            uint3 c = uint3(floor(src.rgb * 255.0 + 0.5));
            uint a = uint(floor(src.a * 255.0 + 0.5));
            uint idx = ((c.g >> 6u) & 3u) | ((c.b >> 3u) << 2u) | ((a > 127u) ? 0x80u : 0u);
            tex = floor(u_clut.read(uint2(idx, uint(U.clut_raw))) * 255.0 + 0.5);
        } else if (U.idx_fmt == 6) {
            // CLASS IDX-C: plain PSMT8 read of a CT16 render target (the channel shuffle).
            float2 rtc = in.texcoord.xy * inv_q;
            rtc = NativeCenter(rtc, U.draw_scale, frag_xy);
            int ppr8 = max(U.idx_self & 15, 1);
            int twl = (U.idx_self >> 4) & 15;
            int thl = (U.idx_self >> 8) & 15;
            if ((U.idx_self & 0x1000) == 0) {
                rtc *= float2(float(1 << twl), float(1 << thl)) / tex_size;
            }
            int2 tc = int2(floor(rtc));
            tc.x &= (1 << twl) - 1;
            tc.y &= (1 << thl) - 1;
            int ppr16 = int(tex_size.x) >> 6;
            int pgoff = (U.idx_self >> 16) & 63;
            int page = pgoff + (tc.y >> 6) * ppr8 + (tc.x >> 7);
            int br = (tc.x >> 4) & 7;
            int bc = (tc.y >> 4) & 3;
            int lx = tc.x & 15, ly = tc.y & 15;
            int lane = (ly >> 1) & 1;
            int cr = ((ly >> 2) << 1) | (ly & 1);
            int cc = lx ^ ((((ly + 2) >> 2) & 1) << 2);
            int X = (page % ppr16) * 64 + bc * 16 + cc;
            int Y = (page / ppr16) * 64 + br * 8 + cr;
            int wmax6 = int(tex_size.x) - 1;
            int h6 = int(tex_size.y);
            int2 t6 = clamp(int2(X, Y), int2(0), int2(wmax6, h6 - 1));
            if (U.flip_v != 0) t6.y = h6 - 1 - t6.y;
            t6 *= max(int(u_texture.get_width()) / max(int(tex_size.x), 1), 1);
            float4 src = u_texture.read(uint2(t6));
            uint3 c = uint3(floor(src.rgb * 255.0 + 0.5));
            uint a = uint(floor(src.a * 255.0 + 0.5));
            uint hw = (c.r >> 3u) | ((c.g >> 3u) << 5u) | ((c.b >> 3u) << 10u) | ((a > 127u) ? 0x8000u : 0u);
            uint idx = (lane != 0) ? ((hw >> 8u) & 0xFFu) : (hw & 0xFFu);
            tex = floor(u_clut.read(uint2(idx, uint(U.clut_raw))) * 255.0 + 0.5);
        } else if (U.mip_levels > 0) {
            // Retail per-pixel LOD over the uploaded chain, floored by the footprint lambda and
            // clamped at the authored chain end (the GL arm's GSMIP_AAF 1 arm).
            float q_abs = abs(in.texcoord.z);
            float lod = (U.mip_lcm != 0) ? U.mip_k : log2(1.0 / max(q_abs, 1e-30)) * U.mip_l + U.mip_k;
            float2 gsm_tc = in.texcoord.xy * inv_q;
            float gsm_r2 = max(dot(dfdx(gsm_tc), dfdx(gsm_tc)), dot(dfdy(gsm_tc), dfdy(gsm_tc)));
            float gsm_lam = 0.5 * log2(max(gsm_r2, 1e-30));
            lod = max(min(lod, float(U.mip_levels)), gsm_lam);
            float4 tex_norm;
            if (U.tex_lod_hw != 0) tex_norm = u_texture.sample(u_sampler, uv);
            else tex_norm = u_texture.sample(u_sampler, uv, level(lod));
            tex = floor(tex_norm * 255.0 + 0.5);
        } else {
            float4 tex_norm = u_texture.sample(u_sampler, uv);
            tex = floor(tex_norm * 255.0 + 0.5);
        }

        // PS2 TFX in integer space: (A * B) >> 7 = trunc(A * B / 128).
        float4 FxT = trunc((vc * tex) / 128.0);
        if (U.tfx == 0) {
            result = FxT;
            if (U.tcc == 0) result.a = vc.a;
        } else if (U.tfx == 1) {
            result.rgb = tex.rgb;
            result.a = (U.tcc == 1) ? tex.a : vc.a;
        } else if (U.tfx == 2) {
            result.rgb = FxT.rgb + vc.a;
            result.a = (U.tcc == 1) ? tex.a + vc.a : vc.a;
        } else {
            result.rgb = FxT.rgb + vc.a;
            result.a = (U.tcc == 1) ? tex.a : vc.a;
        }
        result = min(result, 255.0) / 255.0;
    } else {
        result = in.color;
    }

    result = clamp(result, 0.0, 1.0);

    // --- PS2 fog (PRIM.FGE + FOGCOL) ---
    if (U.fog_enabled != 0) {
        float f = in.fog;
        result.rgb = mix(float3(U.fogcol_r, U.fogcol_g, U.fogcol_b), result.rgb, f);
    }

    // --- PS2 alpha test (TEST: ATE, ATST, AREF, AFAIL) ---
    if (U.ate != 0) {
        int alpha_int = int(result.a * 255.0 + 0.5);
        bool alpha_pass = true;
        if (U.atst == 0) alpha_pass = false;
        else if (U.atst == 1) alpha_pass = true;
        else if (U.atst == 2) alpha_pass = (alpha_int < U.aref);
        else if (U.atst == 3) alpha_pass = (alpha_int <= U.aref);
        else if (U.atst == 4) alpha_pass = (alpha_int == U.aref);
        else if (U.atst == 5) alpha_pass = (alpha_int >= U.aref);
        else if (U.atst == 6) alpha_pass = (alpha_int > U.aref);
        else alpha_pass = (alpha_int != U.aref);
        if (!alpha_pass && U.afail == 0) discard_fragment();
        if (U.afail_pass == 1 && !alpha_pass) discard_fragment();
        if (U.afail_pass == 2 && alpha_pass) discard_fragment();
    }

    // --- PS2 destination alpha test (DATE / DATM): the attachment's current alpha MSB ---
    if (U.date != 0) {
        int dst_alpha_int = int(dst.a * 255.0 + 0.5);
        bool dst_msb = (dst_alpha_int & 0x80) != 0;
        if (U.datm == 0) {
            if (dst_msb) discard_fragment();
        } else {
            if (!dst_msb) discard_fragment();
        }
    }

    // --- depth test against colour-authored ZBUF VRAM (texture 4) ---
    if (U.zsrc_en != 0) {
        int2 zdim = int2(u_zsrc_tex.get_width(), u_zsrc_tex.get_height());
        int zn = max(U.zsrc_num, 1), zd = max(U.zsrc_den, 1);
        int2 zc = clamp(int2(frag_xy) * zn / zd, int2(0), zdim - int2(1));
        float4 zpix = u_zsrc_tex.read(uint2(zc));
        float4 zb = floor(zpix * 255.0 + 0.5);
        float dstz = (zb.a * 16777216.0 + zb.b * 65536.0 + zb.g * 256.0 + zb.r) / 4294967296.0;
        if (U.zsrc_en == 3) {
            if (frag_z <= dstz) discard_fragment();
        } else {
            if (frag_z < dstz) discard_fragment();
        }
    }

    // --- the GS blend equation, integer: Cv = ((A - B) * C >> 7) + D, Cd = the attachment ---
    if (U.blend_enabled != 0 && U.sw_blend != 0) {
        float4 RT = dst;
        float3 Cs = floor(result.rgb * 255.0 + 0.5);
        float As = min(floor(result.a * 255.0 + 0.5), 128.0);
        float3 Cd = floor(RT.rgb * 255.0 + 0.5);
        float Ad = min(floor(RT.a * 255.0 + 0.5), 128.0);
        float3 A_val;
        if (U.blend_a == 0) A_val = Cs;
        else if (U.blend_a == 1) A_val = Cd;
        else A_val = float3(0.0);
        float3 B_val;
        if (U.blend_b == 0) B_val = Cs;
        else if (U.blend_b == 1) B_val = Cd;
        else B_val = float3(0.0);
        float C_val;
        if (U.blend_c == 0) C_val = As;
        else if (U.blend_c == 1) C_val = Ad;
        else C_val = floor(U.fix_alpha * 128.0 + 0.5);
        float3 D_val;
        if (U.blend_d == 0) D_val = Cs;
        else if (U.blend_d == 1) D_val = Cd;
        else D_val = float3(0.0);
        float3 blended = floor((A_val - B_val) * C_val / 128.0) + D_val;
        if (U.colclamp != 0) {
            result.rgb = clamp(blended, float3(0.0), float3(255.0)) / 255.0;
        } else {
            result.rgb = GlslMod(blended, 256.0) / 255.0;
        }
    }

    // --- COLCLAMP after the blend ---
    if (U.colclamp != 0) {
        result.rgb = clamp(result.rgb, float3(0.0), float3(1.0));
    } else if (U.blend_enabled == 0 || U.sw_blend == 0) {
        result.rgb = fract(result.rgb);
    }
    result.a = clamp(result.a, 0.0, 1.0);

    // --- FBA: bit 7 OR-ed into the stored alpha ---
    if (U.fba != 0) {
        int a_int = int(result.a * 255.0 + 0.5);
        a_int = a_int | 0x80;
        result.a = float(min(a_int, 255)) / 255.0;
    }

    // --- 16-bit colour frames store the A-bit ---
    if (U.fb16 != 0 && (U.blend_enabled == 0 || U.sw_blend != 0)) {
        result.a = (int(result.a * 255.0 + 0.5) >= 128) ? 1.0 : 0.0;
    }

    // --- retail 16-bit frame write rule: dither + 5-bit truncation (zero-fill expansion) ---
    if (U.ct16q != 0 && (U.blend_enabled == 0 || U.sw_blend != 0)) {
        int3 cq = int3(result.rgb * 255.0 + 0.5);
        if (U.dthe != 0) {
            int nx = (int(frag_xy.x) / U.ct16q) & 3;
            int ny = (int(frag_xy.y) / U.ct16q) & 3;
            int sh = ny * 16 + nx * 4;
            uint w = uint((ny < 2) ? U.dimx_lo : U.dimx_hi);
            int dm = int((w >> uint(sh & 31)) & 7u);
            if (dm >= 4) dm -= 8;
            cq = clamp(cq + int3(dm), int3(0), int3(255));
        }
        int3 q5 = cq >> 3;
        result.rgb = float3(q5 << 3) / 255.0;
    }

    // --- exact-bit FBMSK merge, 16-bit frames (5551 layout) ---
    if (U.fbmsk16_en != 0) {
        int4 s = int4(result * 255.0 + 0.5);
        int src_bits = ((s.r >> 3) & 0x1F) | (((s.g >> 3) & 0x1F) << 5) | (((s.b >> 3) & 0x1F) << 10) |
                       ((s.a > 127 ? 1 : 0) << 15);
        int4 d = int4(dst * 255.0 + 0.5);
        int dst_bits = ((d.r >> 3) & 0x1F) | (((d.g >> 3) & 0x1F) << 5) | (((d.b >> 3) & 0x1F) << 10) |
                       ((d.a > 127 ? 1 : 0) << 15);
        int merged = (src_bits & ~U.fbmsk16) | (dst_bits & U.fbmsk16);
        int r5 = merged & 0x1F;
        int g5 = (merged >> 5) & 0x1F;
        int b5 = (merged >> 10) & 0x1F;
        result.r = float((r5 << 3) | (r5 >> 2)) / 255.0;
        result.g = float((g5 << 3) | (g5 >> 2)) / 255.0;
        result.b = float((b5 << 3) | (b5 >> 2)) / 255.0;
        result.a = ((merged & 0x8000) != 0) ? 1.0 : 0.0;
    }

    // --- exact-bit FBMSK merge, 32-bit frames ---
    if (U.fbmsk32_en != 0) {
        int4 s32 = int4(result * 255.0 + 0.5);
        int4 d32 = int4(dst * 255.0 + 0.5);
        int4 m32 = int4(U.fbmsk32 & 0xFF, (U.fbmsk32 >> 8) & 0xFF, (U.fbmsk32 >> 16) & 0xFF, (U.fbmsk32 >> 24) & 0xFF);
        int4 merged32 = (s32 & ~m32) | (d32 & m32);
        result = float4(merged32) / 255.0;
    }

    // The colour write mask: a masked channel writes the attachment's value back.
    bool4 wm = bool4((U.color_mask & 1) != 0, (U.color_mask & 2) != 0, (U.color_mask & 4) != 0, (U.color_mask & 8) != 0);
    out.color = select(dst, result, wm);
    return out;
}

// ---------------------------------------------------------------- fullscreen passes
struct FsData {
    float4 position [[position]];
};

vertex FsData fullscreen_vertex(uint vid [[vertex_id]]) {
    const float2 tri[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    FsData o;
    o.position = float4(tri[vid], 0.0, 1.0);
    return o;
}

struct ConstUniforms {
    float r, g, b, a;
};

fragment float4 const_fragment(FsData in [[stage_in]], constant ConstUniforms& U [[buffer(0)]]) {
    return float4(U.r, U.g, U.b, U.a);
}

// The 16-bit companion refresh: Truncate32To16 then Expand16To32 per texel.
fragment float4 pack5551_fragment(FsData in [[stage_in]], texture2d<float> u_texture [[texture(0)]]) {
    int2 p = int2(in.position.xy);
    int4 b = int4(u_texture.read(uint2(p)) * 255.0 + 0.5);
    int3 c5 = b.rgb >> 3;
    int3 c8 = (c5 << 3) | (c5 >> 2);
    int a = (b.a >= 128) ? 255 : 0;
    return float4(float3(c8), float(a)) / 255.0;
}

// The alpha-lane write: the attachment's alpha := the lane byte at the fragment's logical
// texel of the rect; RGB written back from the framebuffer fetch; the exclude rect discards.
struct AlphaLaneUniforms {
    int origin_x, origin_y, scale;
    int ex0, ey0, ex1, ey1;
};

fragment float4 alpha_lane_fragment(FsData in [[stage_in]], float4 dst [[color(0)]],
                                    constant AlphaLaneUniforms& U [[buffer(0)]],
                                    texture2d<float> u_lane [[texture(0)]]) {
    int2 p = int2(in.position.xy);
    if (p.x >= U.ex0 && p.x < U.ex1 && p.y >= U.ey0 && p.y < U.ey1) discard_fragment();
    int2 l = (p - int2(U.origin_x, U.origin_y)) / max(U.scale, 1);
    int2 ldim = int2(u_lane.get_width(), u_lane.get_height());
    l = clamp(l, int2(0), ldim - 1);
    float a = u_lane.read(uint2(l)).r;
    return float4(dst.rgb, a);
}

// The half-word shuffle over the 32-bit image: per pixel, dest half := CT16-frame-write
// (TEXA-expand(src other half)) merged under FBMSK16 with the dest half. texture 0 = the
// source image (the sibling target or the destination copy for a self read); the
// destination is the framebuffer-fetch value.
struct HalfwordUniforms {
    int dest_hi, mask16, ta0, ta1, aem;
};

static inline int Word(float4 c) {
    int4 b = int4(c * 255.0 + 0.5);
    return b.r | (b.g << 8) | (b.b << 16) | (b.a << 24);
}

fragment float4 halfword_fragment(RasterData in [[stage_in]], float4 dst_in [[color(0)]],
                                  constant HalfwordUniforms& U [[buffer(0)]],
                                  texture2d<float> u_texture [[texture(0)]],
                                  texture2d<float> u_rt_copy [[texture(1)]]) {
    int2 p = int2(in.position.xy);
    int2 sdim = int2(u_texture.get_width(), u_texture.get_height());
    int2 ddim = int2(u_rt_copy.get_width(), u_rt_copy.get_height());
    int2 sp = clamp(p * sdim / max(ddim, int2(1)), int2(0), sdim - int2(1));
    int dst = Word(dst_in);
    int src = Word(u_texture.read(uint2(sp)));
    int src_half = (U.dest_hi != 0) ? (src & 0xFFFF) : ((src >> 16) & 0xFFFF);
    int abit = (src_half >> 15) & 1;
    int rgb15 = src_half & 0x7FFF;
    int alpha = (abit != 0) ? U.ta1 : ((U.aem != 0 && rgb15 == 0) ? 0 : U.ta0);
    int out_half = rgb15 | ((alpha >= 0x80) ? 0x8000 : 0);
    int dst_half = (U.dest_hi != 0) ? ((dst >> 16) & 0xFFFF) : (dst & 0xFFFF);
    int new_half = (out_half & ~U.mask16) | (dst_half & U.mask16);
    int word = (U.dest_hi != 0) ? ((dst & 0xFFFF) | (new_half << 16)) : ((dst & int(0xFFFF0000u)) | new_half);
    return float4(float(word & 0xFF), float((word >> 8) & 0xFF), float((word >> 16) & 0xFF),
                  float((word >> 24) & 0xFF)) / 255.0;
}

// ---------------------------------------------------------------- the scanout quad
struct ScanoutUniforms {
    float u0, v0, u1, v1;
    int flip_rows;
};

struct ScanoutData {
    float4 position [[position]];
    float2 texcoord;
};

vertex ScanoutData scanout_vertex(uint vid [[vertex_id]], constant ScanoutUniforms& U [[buffer(0)]]) {
    // Two triangles over the viewport; corner k: x = (k & 1), y = (k >> 1).
    const int corner[6] = {0, 1, 2, 1, 3, 2};
    const int k = corner[vid];
    const float fx = float(k & 1), fy = float(k >> 1);
    ScanoutData o;
    // fy = 0 is the window's TOP row (PS2 top shows at the top; the texture's row 0 = PS2 top =
    // v0). flip_rows stores the top row LAST in memory (the GL readback order).
    float ndc_y = (U.flip_rows != 0) ? (fy * 2.0 - 1.0) : (1.0 - fy * 2.0);
    o.position = float4(fx * 2.0 - 1.0, ndc_y, 0.0, 1.0);
    o.texcoord = float2(mix(U.u0, U.u1, fx), mix(U.v0, U.v1, fy));
    return o;
}

fragment float4 scanout_fragment(ScanoutData in [[stage_in]], texture2d<float> u_texture [[texture(0)]],
                                 sampler u_sampler [[sampler(0)]]) {
    float4 c = u_texture.sample(u_sampler, in.texcoord);
    return float4(c.rgb, 1.0);
}
)MSL";

}  // namespace

const char* MetalShaderSources::Library() { return kLibrary; }

}  // namespace render
}  // namespace ps2r
