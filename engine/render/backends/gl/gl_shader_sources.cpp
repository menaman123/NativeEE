// render/backends/gl/gl_shader_sources.cpp -- the ONE copy of the GLSL text.
//
// The three programs' sources are stored verbatim as raw string literals; the
// fragment text carries the two compile-time arms of the peak configuration
// (GSMIP_AAF 1 = retail LOD floor over every texture class, S83_IDXSCALE 1 =
// scale-aware indexed-serve fetches) as #defines directly after the #version
// line. The text -- including its comments -- is kept byte-identical to the
// compiled baseline so the driver produces identical code (PHASE6_PLAN
// P6-SHADER-TEXT-EXACT). Nothing else in the tree holds GLSL.
#include "engine/render/backends/gl/gl_shader_sources.h"

namespace ps2r {
namespace render {

namespace {

const char* const kPs2VertSrc = R"GLSL(
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_texcoord;  // S, T (texel space) + Q
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_fog;

uniform vec2 u_screen_size;

// [R169-GLRUN] P2.1 (PS2_GL_VSUV): raw-attribute UV mode. The CPU texcoord
// scaling (one fp32 multiply per component, or an exact power-of-two divide)
// moves HERE, per-vertex, PRE-interpolation:
//   0 = passthrough (legacy prescaled attributes; also all gate-off draws)
//   1 = UV-fixed:  uv' = raw * 0.0625 (exact: multiply by 2^-4), q' = 1
//   2 = STQ:       uv' = raw * u_uv_scale (single IEEE multiply -- same
//                  operands as the legacy CPU multiply, correctly rounded on
//                  both sides, no fusion opportunity => identical bits), q' = raw q
//   3 = untextured: uv' = 0, q' = 1
// Region-rebase / RT-regen / shuffle-scale draws take the legacy CPU path
// with mode 0 (amendment A5). Keep in sync with shaders/ps2_vert.glsl.
uniform vec2 u_uv_scale;
uniform int u_uv_mode;

// GS-accurate texture coordinates: the GS interpolates S, T and Q LINEARLY
// in screen space across the triangle and performs the perspective divide
// (S/Q, T/Q) PER PIXEL. `noperspective` forces screen-linear interpolation
// (gl_Position.w is 1.0 here so smooth would match, but be explicit); the
// fragment shader does the per-pixel divide, mirroring the compute
// rasterizer path (cs_rasterize_tri.glsl STQ interpolation).
noperspective out vec3 v_texcoord;
out vec4 v_color;
out float v_fog;

void main() {
    // PS2 screen-space coords to NDC: x in [0, width] -> [-1, 1]
    // Y: [0, height] -> [-1, 1] (PS2 top at GL bottom, scanout flips for display)
    vec2 ndc = (a_position.xy / u_screen_size) * 2.0 - 1.0;
    // [R112-Z-PRECISION] Z mapping: z_ndc = z/2^31 - 1  =>  window
    // d = 0.5*z_ndc + 0.5 = z/2^32 (Sterbenz-exact viewport transform,
    // default glDepthRange [0,1]). The old mapping (z/(2^32-1), NDC z in
    // [0,1]) parked the whole scene at window d ~= 0.5 where the float32
    // ULP is 2^-24; the readback expansion (* (2^33-2)) then quantized z
    // to 512-unit steps landing on k*512-1 after the truncating cast —
    // wiping fog-index bits 8-15 (odd-only indices moving in +2 steps:
    // the haze checkerboard banding + seagull-shaped index noise).
    // Zero-based d = z/2^32 uses the 2^-24-dense NDC region near -1:
    // z quantizes to 128*Z (|err| <= 64), so the 256-unit fog index is
    // exact away from 256-boundaries and both parities are reachable.
    // PCSX2 grounding: tfx_vgs.glsl:43,:86 gl_Position.z = z*exp2(-32)
    // with glClipControl(GL_ZERO_TO_ONE) (GSDeviceOGL.cpp:629,:785) —
    // macOS GL4.1 lacks clip control, hence the 128-step floor here.
    float z_ndc = a_position.z / 2147483648.0 - 1.0;
    gl_Position = vec4(ndc, z_ndc, 1.0);
    // [R169-GLRUN] P2.1 raw-attribute UV transform (see uniform docs above).
    vec3 tc = a_texcoord;
    if (u_uv_mode == 1)      tc = vec3(a_texcoord.xy * 0.0625, 1.0);
    else if (u_uv_mode == 2) tc = vec3(a_texcoord.xy * u_uv_scale, a_texcoord.z);
    else if (u_uv_mode == 3) tc = vec3(0.0, 0.0, 1.0);
    v_texcoord = tc;
    v_color = a_color;
    v_fog = a_fog;
}
)GLSL";

const char* const kPs2FragSrc = R"GLSL(
#version 410 core
#define GSMIP_AAF 1
#define S83_IDXSCALE 1

// S, T (texel space) + Q, interpolated screen-linearly (noperspective) —
// the GS-accurate per-pixel perspective divide happens below.
noperspective in vec3 v_texcoord;
in vec4 v_color;
in float v_fog;

uniform sampler2D u_texture;    // Texture sampler (binding 0)
uniform sampler2D u_rt_texture; // Render target copy for shader-side blending (binding 1)
uniform int u_use_texture;
uniform vec2 u_tex_size;       // Actual texture dimensions for UV normalization

// PS2 alpha blend equation: (A - B) * C + D
// PCSX2-style: computed entirely in the fragment shader using a copy of the
// current render target as Cd source. GL blending is DISABLED when sw_blend=1.
uniform int u_blend_a;      // 0=Cs, 1=Cd, 2=0
uniform int u_blend_b;      // 0=Cs, 1=Cd, 2=0
uniform int u_blend_c;      // 0=As, 1=Ad, 2=FIX
uniform int u_blend_d;      // 0=Cs, 1=Cd, 2=0
uniform float u_fix_alpha;  // FIX value (0-1 range, PS2 128=1.0)
uniform int u_blend_enabled; // ABE flag from PRIM
uniform int u_sw_blend;      // 1=shader-side blend active (RT copy bound)
uniform int u_flip_v;        // 1=flip V coordinate (for RT texture reads)

// TFX texture function parameters
uniform int u_tfx;           // 0=MODULATE, 1=DECAL, 2=HIGHLIGHT, 3=HIGHLIGHT2
uniform int u_tcc;           // 0=RGB only, 1=RGBA

// [S82-MIP] retail GS LOD (GS_SKILL 3.4.12). x=mip levels present on the
// bound texture (0 = no mips -> legacy implicit sampling), y=LCM.
// u_mip_lodp: x=K (s7.4 decoded float), y=2^L. LCM=0: LOD =
// log2(1/|Q|)*2^L + K per pixel (Q = v_texcoord.z, the same interpolant
// the perspective divide uses); LCM=1: LOD = K. textureLod: lambda<=0
// magnifies with MAG filter, >0 minifies with MIN filter -- the exact
// retail LOD<=0 / >0 rule.
uniform ivec2 u_mip_ctl;
uniform vec2 u_mip_lodp;
// The player's filtering enhancement (video.texture_filter / video.anisotropy)
// over an OWNED texture's chain: 1 = sample with the HARDWARE footprint LOD
// (texture(), the implicit derivatives the driver's anisotropic / level-blended
// fetch needs) instead of the retail per-pixel formula below. 0 on every draw
// under the title's own choice -- the deterministic lanes' identity.
uniform int u_tex_lod_hw;

// Post-processing
uniform int u_fba;           // Force Bit Alpha: set bit 7 of output alpha
uniform int u_colclamp;      // Color clamp: 1=clamp [0,255], 0=wrap (mask lower 8 bits)
uniform int u_scanmsk;       // Scanline mask: 0=normal, 2=even Y prohibited, 3=odd Y prohibited

// Depth test params
uniform int u_zte;
uniform int u_ztst;

// Alpha test params (PS2 TEST register)
uniform int u_ate;           // Alpha test enable
uniform int u_atst;          // Alpha test method: 0=NEVER,1=ALWAYS,2=LESS,3=LEQUAL,4=EQUAL,5=GEQUAL,6=GREATER,7=NOTEQUAL
uniform int u_aref;          // Alpha reference value (0-255)
uniform int u_afail;         // Alpha fail action: 0=KEEP,1=FB_ONLY,2=ZB_ONLY,3=RGB_ONLY

// Destination alpha test params (PS2 TEST register)
uniform int u_date;          // Destination alpha test enable
uniform int u_datm;          // Dest alpha test mode: 0=pass if MSB==0, 1=pass if MSB==1

// Fog params (PRIM.FGE + FOGCOL register)
uniform int u_fog_enabled;   // FGE flag from PRIM (0=off, 1=on)
uniform vec3 u_fogcol;       // Fog distant color (RGB, 0-1 range)

// Exact-bit FBMSK for 16-bit frame formats (PSMCT16/PSMCT16S).
// When enabled, the shader packs both the computed result and the
// destination pixel (from u_rt_texture) to the PS2 5551 layout,
// merges bit-exactly: merged = (src & ~mask) | (dst & mask), and
// unpacks back to RGBA8. glColorMask is fully open for these draws.
// u_fbmsk16 uses PS2 5551 bit layout: R[0:4] G[5:9] B[10:14] A[15];
// a 1 bit means PRESERVE the destination bit (GS convention).
uniform int u_fbmsk16_en;
uniform int u_fbmsk16;

// [R163-ZTEX2] Depth-attachment sampling mode. 0 = normal color fetch.
// Nonzero = u_texture is a GL_DEPTH_COMPONENT32F depth attachment being
// read through a Z-psm TEX0 view; reconstruct the PS2 z value from the
// depth float and convert exactly like OUR legacy CPU chain (SyncZToVRAM
// encode -> TextureDecoder decode -> dispatcher alpha scale), NOT PCSX2's:
//   1 = native PSMZ32 read of a Z32 target (DecodePSMZ32 chain): raw z
//       bytes as RGB, alpha = ScaleAlphaPS2ToGPU(z>>24) (a8>=128 ? 255 :
//       2*a8) -- the one alpha scale the Decode() dispatcher applies.
//   2 = Z16-family read of a Z16-NATIVE target (R162 chain, unchanged):
//       z16 = min(z, 0xFFFF) SATURATES like SyncZToVRAM's Z16 encode
//       (PCSX2 bit-masks), then 5551 bit-replicate expansion, a = msb ?
//       255 : 0 (Expand16to32, no TEXA).
//   3 = PSMZ16 VIEW of a Z32-NATIVE target: remap each reader texel to
//       the one Z32 pixel + halfword whose bytes it aliases (closed-form
//       inversion of GetAddressPSMZ16-over-GetAddressPSMZ32, verified
//       byte-exact over all 344064 live-footprint texels), halfword
//       SELECT (NO saturation -- the live chain reads 2 raw bytes of the
//       unsaturated z32), then the same 5551 expansion as fmt 2.
// Common z reconstruction: z = uint(df * 2^32) with df>=1.0 pre-clamped
// (power-of-two exponent shift, exact in float32; mirrors SyncZToVRAM's
// (uint32)((double)d * 4294967296.0) for every representable df).
uniform int u_depth_fmt;
// [R163-ZTEX2] fmt=3 only: page-row offset of the reader tbp into the Z32
// buffer, in Z32 rows ((tbp - zbp) / (8192*ppr) * 32; live: 0 or 224).
uniform int u_depth_zoff;

// [R164-IDXTEX] Indexed-read (PSMT8H channel-shuffle) serve mode. 0 =
// normal fetch. Nonzero = the bound texture is a live COLOR render target
// being read through a PSMT8H TEX0 view; reconstruct the index byte the
// legacy CPU chain (SyncRTToVRAM encode -> DecodePSMT8H -> CLUT) would
// have produced and look it up in u_clut -- a 256x1 RGBA8 CLUT texture
// uploaded PRE-FOLDED (the ReadCLUT32 CSM1 storage walk composed with
// DecodePSMT8H's lookup swizzle) and PRE-SCALED (ScaleAlphaPS2ToGPU), so
// the lookup result equals the legacy uploaded texel byte-for-byte.
//   4 = CLASS IDX-A: PSMT8H over a CT32-class render. PSMT8H shares the
//       CT32 page layout; index(x,y) = byte3 of the CT32 word at the SAME
//       (x,y). Legacy chain: encode ps2_a = (a_gpu>=255)?128:a_gpu/2 into
//       byte3; decode reads it as the index. u_idx_self=1 samples the
//       pre-draw destination copy (u_rt_texture) instead of u_texture.
//   5 = CLASS IDX-B: PSMT8H over CT16-rendered pages (base target +
//       partner at +height rows in u_idx_tex2). Closed-form remap of each
//       PSMT8H texel to the ONE CT16 pixel whose HIGH byte aliases the
//       CT32 word's byte3 (byte-exact vs the address-table walk for all
//       229376 live texels), then the CT16 5551 pack's high byte.
//   7 = depth-byte palette (an owned effect's intent, no aliasing view):
//       the bound texture is a Depth32f attachment; index = the GS Z byte
//       at bit u_idx_self (8 = the retail colour filter's lane: a Z16 view
//       of the Z32 buffer written to a 16-bit frame under FBMSK 0x3FFF
//       keeps Z[15:8], then read as PSMT8H) through u_clut row 0.
uniform int u_idx_fmt;
uniform int u_idx_self;
// [S103-UVNATIVE] native-center requantization for LEGACY-path samples of
// indexed-view (T8/T4/T8H/T4HL/T4HH) reads of RT-authored bytes (the fog
// composite class). The aliasing view is defined on NATIVE pixels (GS
// page/block/column tables); at draw scale > 1 the physical fragment phases
// straddle the exact-boundary texel interpolant (u = x + 1.0) and adjacent
// phases resolve different texels — 8px-block-quantized fog (CAVERN_
// DEPTHPATCH [DZ5]). Set nonzero only by the FindTargetTexture indexed-
// read legacy branch under PS2_GL_UVNATIVE (default ON). Consumed only
// under the S83_IDXSCALE define (compiled at scale > 1); identity at
// scale 1 by S83NativeCenter's early return.
uniform int u_uv_native;
// [S83-IDXSCALE] draw-target resolution scale (physical px per native px;
// 1 at PS2_GS_SCALE=1). The aliasing serve arms (u_depth_fmt=3, u_idx_fmt
// 4/5/6) are NATIVE-texel-exact contracts: each output pixel reads exactly
// one aliased source texel. At scale>1 the physical fragments of one
// native pixel carry texcoord phases X+-0.25 (not the native center
// X+0.5), so fragments straddle source-texel boundaries and half of them
// read the aliasing NEIGHBOR (a different halfword/byte of the aliased
// word) — the checkerboard fog mask. Requantize the interpolant to the
// native pixel center with screen-space derivatives (exact for the affine
// sprite/tri mappings these chains use).
uniform int u_draw_scale;
uniform sampler2D u_clut;      // 256x2 pre-folded CLUT (binding 2): row 0 =
                               // legacy prescaled bytes, row 1 = raw PS2
                               // texel ([S9-CLUTRAW])
uniform sampler2D u_idx_tex2;  // fmt=5 partner target (binding 3)

// [S9-SHADOW21] loop-21 blob-shadow arms (s9_shadow.h / SHADOW2_IMPL.md):
// u_fb16: FRAME is a 16-bit COLOR format — the FBO alpha channel stores
//   the PS2 A-bit (1.0 iff PS2 alpha >= 0x80) instead of the x2 rescale,
//   restoring GS DATE / 5551-pack bit-15 semantics for 16-bit targets.
// u_zsrc_en: nonzero = this draw's ZBUF.ZBP addresses VRAM authored by a
//   live COLOR target (the game's inverted-Z copy). The depth test runs
//   HERE against u_zsrc_tex (that target's color image, unit 4): z32 is
//   reconstructed from the RGBA8 bytes and compared per ZTST (value 2 =
//   GEQUAL, 3 = GREATER) against gl_FragCoord.z (= PS2 z / 2^32 under the
//   R112 mapping). The GL depth func is forced ALWAYS by the CPU side.
// u_clut_raw: idx-arm CLUT row select. Row 1 carries the RAW PS2 texel
//   (alpha unscaled); the PSMT8H self-read invert class needs the full
//   0-255 alpha range (Av = At verbatim), which the legacy prescale
//   round-trip caps at 128.
uniform int u_fb16;

// [WN4-CT16Q] retail PSMCT16/16S write rule (s9_shadow.h Ct16QuantOn):
// u_ct16q = 0 (off / 32-bit frame) or the bound target's scale factor
// (>=1) — the DIMX matrix indexes NATIVE pixels (gl_FragCoord / u_ct16q).
// u_dimx_lo/hi carry the raw 64-bit DIMX register (DMij = 3-bit signed
// field at bit 16*i + 4*j).
uniform int u_ct16q;
uniform int u_dthe;
uniform int u_dimx_lo;   // raw DIMX bits 0..31  (int-typed for SetU1i)
uniform int u_dimx_hi;   // raw DIMX bits 32..63
uniform int u_zsrc_en;
uniform sampler2D u_zsrc_tex;  // binding 4
// [GE1] zsrc coordinate map: zc = fragcoord * num / den. num = SOURCE
// target scale, den = DEST target scale (both 1 at PS2_GS_SCALE=1 --
// identity, byte-identical). Cross-scale pairs (e.g. scaled z-copy read
// by the native-pinned S9 accumulator) map 1:1 in LOGICAL pixels.
uniform int u_zsrc_num;
uniform int u_zsrc_den;
uniform int u_clut_raw;

out vec4 frag_color;

#ifdef S83_IDXSCALE
// [S83-IDXSCALE] Requantize an interpolated GS texel coordinate to the
// value it takes at the NATIVE pixel center. At draw scale s the physical
// fragments of one native pixel sit at phases (i+0.5)/s, so their
// interpolants straddle source-texel boundaries; the aliasing serve arms
// (depth fmt=3, idx fmt 4/5/6) must read the ONE texel the native GS pixel
// reads. Exact for affine mappings (the sprite/tri FST chains these serves
// carry; the derivative IS the mapping's Jacobian).
vec2 S83NativeCenter(vec2 rt) {
    if (u_draw_scale <= 1) return rt;
    float s = float(u_draw_scale);
    vec2 fc = gl_FragCoord.xy;
    vec2 d  = (floor(fc / s) + 0.5) * s - fc;
    return rt + dFdx(rt) * d.x + dFdy(rt) * d.y;
}
#endif

void main() {
    // --- PS2 SCANMSK (Scanline Mask for Interlaced Rendering) ---
    // Per GS spec: prohibits drawing to even or odd Y rows of the framebuffer.
    // MSK=0: normal (no mask), MSK=2: even Y prohibited, MSK=3: odd Y prohibited.
    // Applied early as a discard to skip all pixel processing for masked rows.
    if (u_scanmsk == 2) {
        // Even Y prohibited: discard pixels on even rows
        if (int(gl_FragCoord.y) % 2 == 0) discard;
    } else if (u_scanmsk == 3) {
        // Odd Y prohibited: discard pixels on odd rows
        if (int(gl_FragCoord.y) % 2 != 0) discard;
    }

    vec4 result;

    if (u_use_texture != 0) {
        // GS-accurate STQ: S, T, Q were interpolated LINEARLY in screen space
        // (noperspective); perform the perspective divide PER PIXEL, exactly
        // like the compute path (texture_sample.glsl / cs_rasterize_tri.glsl):
        // q == 0 falls back to 1.0 (no epsilon — match the compute path).
        // FST=1 (UV mode) and forced-UV paths upload q=1.0, so the divide is
        // a no-op there.
        float inv_q = (v_texcoord.z != 0.0) ? (1.0 / v_texcoord.z) : 1.0;
        // [S103-UVNATIVE] indexed-view-of-RT legacy sample: requantize the
        // texel interpolant to the native pixel center before normalizing,
        // so all scale^2 fragment phases of one native pixel resolve the
        // SAME texel (the GS aliasing views are native-pixel-defined; see
        // the uniform's comment). Inert unless u_uv_native != 0.
        vec2 uvn_rt = v_texcoord.xy * inv_q;
#ifdef S83_IDXSCALE
        if (u_uv_native != 0) uvn_rt = S83NativeCenter(uvn_rt);
#endif
        // Normalize texel coords to [0,1] using actual texture dimensions
        vec2 uv = uvn_rt / u_tex_size;
        // Flip V for RT texture reads
        if (u_flip_v != 0) {
            uv.y = 1.0 - uv.y;
        }

        // Convert to PS2 integer range (0-255) for accurate TFX math.
        vec4 vc = floor(v_color * 255.0 + 0.5);
        vec4 tex;
        if (u_depth_fmt == 2) {
            // [R162-ZTEX] Z16-family view of a depth attachment: set the
            // integer-space texel directly (bypasses floor(norm*255+0.5));
            // the shared PS2 alpha rescale below still applies, exactly as
            // it does to the legacy decoded upload (Expand16to32 alpha is
            // 0/255 -> rescale -> 0/128).
            float df = texture(u_texture, uv).r;
            uint d = (df >= 1.0) ? 0xFFFFFFFFu
                                 : uint(max(df, 0.0) * exp2(32.0));
            uint z16 = min(d, 0xFFFFu);
            uint r5 = z16 & 0x1Fu;
            uint g5 = (z16 >> 5) & 0x1Fu;
            uint b5 = (z16 >> 10) & 0x1Fu;
            tex = vec4(float((r5 << 3u) | (r5 >> 2u)),
                       float((g5 << 3u) | (g5 >> 2u)),
                       float((b5 << 3u) | (b5 >> 2u)),
                       ((z16 >> 15) != 0u) ? 255.0 : 0.0);
        } else if (u_depth_fmt == 1) {
            // [R163-ZTEX2] CLASS Z32N: native PSMZ32 read of a Z32 depth
            // attachment. Legacy chain: SyncZToVRAM Z32 encode (raw 4 bytes
            // of trunc(clamp(d)*2^32)) -> DecodePSMZ32 (raw bytes as RGBA)
            // -> dispatcher ScaleAlphaPS2ToGPU once -> upload. Identity
            // footprint: uses the shared uv verbatim (like fmt 2). The
            // shared tex.a rescale below rounds the alpha trip back:
            // a8 < 128 -> 2*a8 -> floor(/2) = a8; a8 >= 128 -> 255 -> 128
            // -- byte-identical to the legacy uploaded texel's
            // post-rescale value.
            float df = texture(u_texture, uv).r;
            uint z = (df >= 1.0) ? 0xFFFFFFFFu
                                 : uint(max(df, 0.0) * exp2(32.0));
            tex = vec4(float( z         & 0xFFu),
                       float((z >>  8u) & 0xFFu),
                       float((z >> 16u) & 0xFFu),
                       0.0);
            uint a8 = (z >> 24u) & 0xFFu;
            tex.a = (a8 >= 128u) ? 255.0 : float(a8 << 1u);  // ScaleAlphaPS2ToGPU
        } else if (u_depth_fmt == 3) {
            // [R163-ZTEX2] CLASS Z16V: PSMZ16 view of a Z32 depth
            // attachment. For every Z16 reader texel (x,y) the legacy chain
            // (GetAddressPSMZ16 read over GetAddressPSMZ32-written bytes)
            // picks up exactly one Z32 pixel (PXg,PYg) and one halfword hw
            // -- the closed-form inversion below is byte-exact against the
            // block/column-table walk for ALL live-footprint texels
            // (229376 + 114688, 0 mismatches; /tmp/r163_verify.py).
            // NOTE (A.2/W4): needs RAW reader texel space, so recompute
            // from v_texcoord * inv_q -- the shared `uv` is already divided
            // by u_tex_size (which holds the TARGET dims here).
            vec2  rt = v_texcoord.xy * inv_q;
#ifdef S83_IDXSCALE
            rt = S83NativeCenter(rt);              // [S83-IDXSCALE]
#endif
            ivec2 t  = ivec2(floor(rt));
            int lx = t.x & 63, ly = t.y & 63;      // Z16 page-local (64x64)
            int PX = (lx & 7)  | (ly & 0x38);      // intra-page swizzle
            int PY = (ly & 7)  | ((lx & 0x30) >> 1);
            int hw = (lx >> 3) & 1;                // 0 = LOW, 1 = HIGH half
            int PXg = (t.x & ~63) | PX;            // page column preserved
            int PYg = (t.y >> 6) * 32 + PY + u_depth_zoff;  // 64x64 Z16 page
                                                   // = 64x32 Z32 page
            vec2 uvz = (vec2(PXg, PYg) + 0.5) / u_tex_size;
            if (u_flip_v != 0) uvz.y = 1.0 - uvz.y;   // same flip rule
            float df = texture(u_texture, uvz).r;
            uint z = (df >= 1.0) ? 0xFFFFFFFFu
                                 : uint(max(df, 0.0) * exp2(32.0));
            // Halfword SELECT of the unsaturated z32 (NO saturation -- the
            // live chain reads 2 raw bytes; min() != halfword select), then
            // the R162-proven 5551 expansion verbatim (bit-replicate + msb
            // alpha, no TEXA).
            uint z16 = (hw != 0) ? ((z >> 16u) & 0xFFFFu) : (z & 0xFFFFu);
            uint r5 = z16 & 0x1Fu;
            uint g5 = (z16 >> 5) & 0x1Fu;
            uint b5 = (z16 >> 10) & 0x1Fu;
            tex = vec4(float((r5 << 3u) | (r5 >> 2u)),
                       float((g5 << 3u) | (g5 >> 2u)),
                       float((b5 << 3u) | (b5 >> 2u)),
                       ((z16 >> 15u) != 0u) ? 255.0 : 0.0);
        } else if (u_idx_fmt == 7) {
            // Depth-byte palette serve (an owned effect's INTENT, not an
            // aliasing view): the bound texture is a Depth32f attachment; the
            // index is one byte of the GS Z the pixel holds -- bits
            // [shift+7:shift], shift = u_idx_self (8 = the byte a Z16 view of
            // a Z32 buffer leaves in a 16-bit frame under FBMSK 0x3FFF, the
            // retail colour filter's lane) -- looked up in u_clut row 0
            // (pre-scaled alpha, like fmt 4). Screen-aligned: the retail copy
            // maps pixel (x,y) to (x,y), so the serve reads THIS fragment's
            // own depth texel (the depth attachment and the target share the
            // physical extent), never a UV-interpolated neighbour at scale > 1.
            ivec2 ztc = ivec2(gl_FragCoord.xy);
            ztc = clamp(ztc, ivec2(0), textureSize(u_texture, 0) - 1);
            float df = texelFetch(u_texture, ztc, 0).r;
            uint z = (df >= 1.0) ? 0xFFFFFFFFu
                                 : uint(max(df, 0.0) * exp2(32.0));
            uint idx = (z >> uint(u_idx_self & 31)) & 0xFFu;
            tex = floor(texelFetch(u_clut, ivec2(int(idx), 0), 0) * 255.0 + 0.5);
        } else if (u_idx_fmt == 4) {
            // [R164-IDXTEX] CLASS IDX-A: PSMT8H read of a CT32-class RT.
            // PSMT8H shares the CT32 page layout; index(x,y) = byte3 of the
            // CT32 word at the SAME (x,y) (DecodePSMT8H walks
            // GetAddressPSMCT32(x,y) and reads vram[addr+3]). Legacy chain
            // transcription:
            //   SyncRTToVRAM CT32 encode: ps2_a = (a_gpu >= 255) ? 128 :
            //     a_gpu/2; gs_vram[addr+3] = ps2_a
            //   DecodePSMT8H: idx = ps2_a; texel = eff_clut[idx] with RAW
            //     PS2 alpha (ReadCLUT32 outputs raw alpha)
            //   dispatcher ScaleAlphaPS2ToGPU once
            // The CLUT texture is uploaded PRE-FOLDED (CSM1) and PRE-SCALED
            // (ScaleAlphaPS2ToGPU) on the CPU, so the lookup result == the
            // legacy uploaded texel byte-for-byte; the shared tex.a rescale
            // line below then applies identically.
            // NOTE (A.2/W4): RAW reader texel space (v_texcoord * inv_q),
            // never the shared normalized `uv`; u_tex_size = TARGET dims.
            // texelFetch flip rule: same u_flip_v convention as the fmt=3
            // RT-read path (row flip when set; production RT draws set 0).
            vec2  rtc = v_texcoord.xy * inv_q;          // raw texel space
#ifdef S83_IDXSCALE
            rtc = S83NativeCenter(rtc);            // [S83-IDXSCALE]
#endif
            ivec2 tdim4 = ivec2(u_tex_size);
            ivec2 tc  = clamp(ivec2(floor(rtc)), ivec2(0), tdim4 - 1);
            if (u_flip_v != 0) tc.y = tdim4.y - 1 - tc.y;
#ifdef S83_IDXSCALE
            // [S83-IDXSCALE] The closed-form aliasing remaps (fmt 4/5/6)
            // operate in NATIVE pixel space (GS page/block/column tables
            // are defined on native pixels); a [GE1]-scaled target stores
            // scale x scale physical texels per native pixel. texelFetch
            // with native coords on a scaled texture reads the top-left
            // QUADRANT (wrong pixels). Map native -> physical by the
            // texture's own scale factor (top-left texel of the block =
            // the NEAREST-equivalent sample). scale==1 is arithmetically
            // identity (legacy bytes).
            {
                ivec2 ts4 = (u_idx_self != 0) ? textureSize(u_rt_texture, 0)
                                              : textureSize(u_texture, 0);
                tc *= max(ts4.x / max(tdim4.x, 1), 1);
            }
#endif
            vec4 src = (u_idx_self != 0)
                     ? texelFetch(u_rt_texture, tc, 0)
                     : texelFetch(u_texture,   tc, 0);
            uint a_gpu = uint(floor(src.a * 255.0 + 0.5));
            uint idx = (a_gpu >= 255u) ? 128u : (a_gpu >> 1u);  // inverse scale
            // floor(+0.5) = the legacy uploaded-texel read (floor(norm*255
            // +0.5)); identity for the exact unorm round-trip and byte-
            // identical to legacy either way.
            tex = floor(texelFetch(u_clut, ivec2(int(idx), u_clut_raw), 0) * 255.0 + 0.5);
        } else if (u_idx_fmt == 5) {
            // [R164-IDXTEX] CLASS IDX-B: PSMT8H read over CT16-rendered
            // pages. For every PSMT8H texel (x,y) the byte3 of the CT32
            // word is the HIGH byte of exactly one CT16 pixel; closed form
            // (byte-exact vs the GetAddressPSMCT32/PSMCT16 table walk for
            // ALL 229376 live texels, 0 mismatches):
            //   ppr  = render_fbw/64 (the TARGET's, == tbw by P4)
            //   page = (y >> 5)*ppr + (x >> 6)
            //   lx = x & 63; ly = y & 31
            //   lX = (lx & 7) | 8 | ((ly & 0x18) << 1)
            //   lY = (lx & 0x38) | (ly & 7)
            //   X  = (page % ppr)*64 + lX
            //   Y  = (page / ppr)*64 + lY          // Y in [0, 2*height)
            //   Y <  height  -> sample u_texture  at (X, Y)
            //   Y >= height  -> sample u_idx_tex2 at (X, Y - height)
            // (live: height=448; boundary exactly at PSMT8H y=224 ->
            //  partner row 0). Then the CT16 encode pack's high byte:
            //   hw16 = (r>>3) | ((g>>3)<<5) | ((b>>3)<<10) | ((a>127)<<15)
            //   idx  = (hw16 >> 8) & 0xFF
            //        = ((g>>6)&3) | ((b>>3)<<2) | ((a>127) ? 0x80 : 0)
            // The FBO holds the post-FBMSK merged value (shader-side
            // fbmsk16 merge), so the pack reproduces the masked deposit's
            // gs_vram[addr+1] byte exactly.
            vec2  rtc = v_texcoord.xy * inv_q;
#ifdef S83_IDXSCALE
            rtc = S83NativeCenter(rtc);            // [S83-IDXSCALE]
#endif
            ivec2 tc  = ivec2(floor(rtc));
            int ppr  = int(u_tex_size.x) >> 6;
            int page = (tc.y >> 5)*ppr + (tc.x >> 6);
            int lx = tc.x & 63, ly = tc.y & 31;
            int lX = (lx & 7) | 8 | ((ly & 0x18) << 1);
            int lY = (lx & 0x38) | (ly & 7);
            int X  = (page % ppr)*64 + lX;
            int Y  = (page / ppr)*64 + lY;
            int h  = int(u_tex_size.y);
            int wmax = int(u_tex_size.x) - 1;
            vec4 src;
            if (Y < h) {
                ivec2 t0 = clamp(ivec2(X, Y), ivec2(0), ivec2(wmax, h - 1));
                if (u_flip_v != 0) t0.y = h - 1 - t0.y;    // fmt=3 flip rule
#ifdef S83_IDXSCALE
                // [S83-IDXSCALE] native -> physical (see fmt=4 note).
                t0 *= max(textureSize(u_texture, 0).x /
                              max(int(u_tex_size.x), 1), 1);
#endif
                src = texelFetch(u_texture, t0, 0);
            } else {
                ivec2 t1 = clamp(ivec2(X, Y - h), ivec2(0), ivec2(wmax, h - 1));
                if (u_flip_v != 0) t1.y = h - 1 - t1.y;    // fmt=3 flip rule
#ifdef S83_IDXSCALE
                // [S83-IDXSCALE] partner target scales independently.
                t1 *= max(textureSize(u_idx_tex2, 0).x /
                              max(int(u_tex_size.x), 1), 1);
#endif
                src = texelFetch(u_idx_tex2, t1, 0);
            }
            uvec3 c = uvec3(floor(src.rgb * 255.0 + 0.5));
            uint a  = uint(floor(src.a * 255.0 + 0.5));
            uint idx = ((c.g >> 6u) & 3u) | ((c.b >> 3u) << 2u)
                     | ((a > 127u) ? 0x80u : 0u);
            // floor(+0.5): see the fmt=4 note (legacy read, exact identity).
            tex = floor(texelFetch(u_clut, ivec2(int(idx), u_clut_raw), 0) * 255.0 + 0.5);
        } else if (u_idx_fmt == 6) {
            // [R198-FIX] CLASS IDX-C: plain PSMT8 read of a CT16 render
            // target (blob-shadow/effects channel shuffle). Closed-form
            // remap of each PSMT8 texel (x,y) to the ONE CT16 pixel whose
            // low/high byte holds that index (byte-exact vs the
            // GetAddressPSMT8/GetAddressPSMCT16 table walk: 0 mismatches
            // over 512x448 + 256x256 + 512x64; /tmp/r198/derive_remap.py,
            // recorded in R198_IMPL.md):
            //   ppr8 = tbw8_px/128 (>=1), ppr16 = fbw16_px/64
            //   page = (y>>6)*ppr8 + (x>>7)
            //   br = (x>>4)&7; bc = (y>>4)&3   (PSMT8 block table is the
            //                                   TRANSPOSE of CT16's)
            //   lx = x&15; ly = y&15
            //   lane = (ly>>1)&1               (0 = low byte of hw16)
            //   cr = ((ly>>2)<<1)|(ly&1)
            //   cc = lx ^ ((((ly+2)>>2)&1)<<2)
            //   X = (page%ppr16)*64 + bc*16 + cc
            //   Y = (page/ppr16)*64 + br*8  + cr
            // GS REPEAT (wms/wmt=0) wraps at the CLAIMED 2^tw x 2^th like
            // the legacy GL sampler does on the claim-sized decode output.
            // u_idx_self is overloaded as packed metadata for this class:
            // ppr8 | (tw_log<<4) | (th_log<<8).
            vec2  rtc = v_texcoord.xy * inv_q;
#ifdef S83_IDXSCALE
            rtc = S83NativeCenter(rtc);            // [S83-IDXSCALE]
#endif
            int ppr8   = max(u_idx_self & 15, 1);
            int twl    = (u_idx_self >> 4) & 15;
            int thl    = (u_idx_self >> 8) & 15;
            // fst=0 (STQ, u_idx_self bit12 clear): the CPU multiplied the
            // normalized s/t by uv_tex_w/h == the TARGET dims for an
            // idx-served draw; the GS texel space is the CLAIM (2^tw x
            // 2^th). Rescale: texel = (st/q) * claim / target.
            if ((u_idx_self & 0x1000) == 0) {
                rtc *= vec2(float(1 << twl), float(1 << thl)) / u_tex_size;
            }
            ivec2 tc   = ivec2(floor(rtc));
            tc.x &= (1 << twl) - 1;            // REPEAT at claim size
            tc.y &= (1 << thl) - 1;
            int ppr16 = int(u_tex_size.x) >> 6;
            // [S8-INRT] window-in-target: bits 16-21 carry the page offset
            // of this PSMT8 window inside the containing CT16 target
            // (8 KB pages are byte-identical between the two layouts, so
            // one linear page index serves both; 0 = exact-base serve).
            int pgoff = (u_idx_self >> 16) & 63;
            int page  = pgoff + (tc.y >> 6)*ppr8 + (tc.x >> 7);
            int br = (tc.x >> 4) & 7;
            int bc = (tc.y >> 4) & 3;
            int lx = tc.x & 15, ly = tc.y & 15;
            int lane = (ly >> 1) & 1;
            int cr = ((ly >> 2) << 1) | (ly & 1);
            int cc = lx ^ ((((ly + 2) >> 2) & 1) << 2);
            int X  = (page % ppr16)*64 + bc*16 + cc;
            int Y  = (page / ppr16)*64 + br*8  + cr;
            int wmax6 = int(u_tex_size.x) - 1;
            int h6    = int(u_tex_size.y);
            ivec2 t6 = clamp(ivec2(X, Y), ivec2(0), ivec2(wmax6, h6 - 1));
            if (u_flip_v != 0) t6.y = h6 - 1 - t6.y;    // fmt=3 flip rule
#ifdef S83_IDXSCALE
            // [S83-IDXSCALE] native -> physical (see fmt=4 note).
            t6 *= max(textureSize(u_texture, 0).x /
                          max(int(u_tex_size.x), 1), 1);
#endif
            vec4 src = texelFetch(u_texture, t6, 0);
            // CT16 encode pack (fmt=5 precedent): hw16 low/high byte is
            // the PSMT8 index this texel aliases.
            uvec3 c = uvec3(floor(src.rgb * 255.0 + 0.5));
            uint a  = uint(floor(src.a * 255.0 + 0.5));
            uint hw = (c.r >> 3u) | ((c.g >> 3u) << 5u)
                    | ((c.b >> 3u) << 10u) | ((a > 127u) ? 0x8000u : 0u);
            uint idx = (lane != 0) ? ((hw >> 8u) & 0xFFu) : (hw & 0xFFu);
            // floor(+0.5): see the fmt=4 note (legacy read, exact identity).
            tex = floor(texelFetch(u_clut, ivec2(int(idx), u_clut_raw), 0) * 255.0 + 0.5);
        } else if (u_mip_ctl.x > 0) {
            // [S82-MIP] retail per-pixel LOD over the uploaded chain.
            float q_abs = abs(v_texcoord.z);
            float lod = (u_mip_ctl.y != 0)
                ? u_mip_lodp.x
                : log2(1.0 / max(q_abs, 1e-30)) * u_mip_lodp.y
                      + u_mip_lodp.x;
#ifdef GSMIP_AAF
            // [S91-MINIF] anti-aliasing LOD floor (gs_mipmap.h doc):
            // clamp from below by the render-resolution footprint lambda
            // over level-0 texel coords — only ever picks a COARSER level
            // than retail, only where the pixel cannot resolve it.
            // GSMIP_AAF==2 = pure-lambda reference arm (PS2_GS_MIP_LODREF).
            vec2 gsm_tc = v_texcoord.xy * inv_q;
            float gsm_r2 = max(dot(dFdx(gsm_tc), dFdx(gsm_tc)),
                               dot(dFdy(gsm_tc), dFdy(gsm_tc)));
            float gsm_lam = 0.5 * log2(max(gsm_r2, 1e-30));
#if GSMIP_AAF == 2
            lod = gsm_lam;
#else
            // [S92-MOTION] clamp the RETAIL term at the authored chain
            // end (u_mip_ctl.x) exactly as the GS clamps LOD at MXL, so
            // the synthesized levels appended past it (UploadMipLevels)
            // are reachable only through the footprint floor.
            lod = max(min(lod, float(u_mip_ctl.x)), gsm_lam);
#endif
#endif
            vec4 tex_norm;
            if (u_tex_lod_hw != 0) tex_norm = texture(u_texture, uv);
            else tex_norm = textureLod(u_texture, uv, lod);
            tex = floor(tex_norm * 255.0 + 0.5);
        } else {
            vec4 tex_norm = texture(u_texture, uv);
            tex = floor(tex_norm * 255.0 + 0.5);
        }

        // Sampled textures carry GPU-convention alpha: the decoder applies
        // ScaleAlphaPS2ToGPU (ps2>=128 ? 255 : ps2*2) once per texel, and RT
        // color attachments store the post-rescale alpha written below. The
        // TFX / alpha-test / FBA math in this shader is PS2-space (0x80 =
        // opaque), and the final u_sw_blend==0 rescale converts to GL space
        // exactly once. Without this inverse the alpha is scaled TWICE and
        // the GL blend factor is 2x the GS value — mid-alpha (0x40-0x7f)
        // translucent layers (iceberg shell) saturate fully opaque (R97/R99).
        // [S9-CLUTRAW] Row-1 CLUT texels (idx arms with u_clut_raw=1) carry
        // the RAW PS2 alpha already — the inverse would halve it and the
        // prescale round-trip caps >=128 at 128, destroying the inverted-Z
        // hi byte the blob-shadow slab test needs. Skip it for that class.
        if (u_idx_fmt == 0 || u_clut_raw == 0) {
            tex.a = (tex.a >= 255.0) ? 128.0 : floor(tex.a * 0.5);
        }

        // PS2 TFX texture functions operate in integer space.
        // Multiply: (A * B) >> 7  which is trunc(A * B / 128.0)
        // This matches PCSX2's shader: trunc((C * T) / 128.0)
        vec4 FxT = trunc((vc * tex) / 128.0);

        if (u_tfx == 0) {
            // MODULATE
            result = FxT;
            if (u_tcc == 0) result.a = vc.a;
        } else if (u_tfx == 1) {
            // DECAL
            result.rgb = tex.rgb;
            result.a = (u_tcc == 1) ? tex.a : vc.a;
        } else if (u_tfx == 2) {
            // HIGHLIGHT
            result.rgb = FxT.rgb + vc.a;
            result.a = (u_tcc == 1) ? tex.a + vc.a : vc.a;
        } else {
            // HIGHLIGHT2
            result.rgb = FxT.rgb + vc.a;
            result.a = (u_tcc == 1) ? tex.a : vc.a;
        }

        // Clamp to [0, 255] in integer space, then normalize to [0, 1]
        result = min(result, 255.0) / 255.0;
    } else {
        // Untextured: output vertex color directly.
        // v_color is in [0,1] from GL uint8 normalization (0-255 → 0.0-1.0).
        result = v_color;
    }

    // Clamp to reasonable range after overbright math (pre-blend, pre-test)
    // COLCLAMP is applied later, after blending. This early clamp prevents
    // extreme values from propagating through fog and alpha test.
    result = clamp(result, 0.0, 1.0);

    // --- PS2 Fog Effect (PRIM.FGE + FOGCOL register) ---
    // Per GS spec: RGB_out = F * RGB_in + (0xFF - F) * RGB_fog
    // where the multiply is (A * B) >> 8.
    // F=255 (v_fog=1.0): no fog, full original color.
    // F=0   (v_fog=0.0): full fog color, no original color.
    // Alpha is NOT affected by fog.
    // Applied after texture function, before pixel tests.
    if (u_fog_enabled != 0) {
        float f = v_fog;  // Already [0,1] from GL normalized ubyte
        result.rgb = mix(u_fogcol, result.rgb, f);
    }

    // --- PS2 Alpha Test (TEST register: ATE, ATST, AREF, AFAIL) ---
    // Per GS spec, the alpha test compares the source pixel's alpha against
    // AREF and controls whether the pixel is written to the framebuffer.
    // The test occurs after texture function but before alpha blending.
    if (u_ate != 0) {
        // Convert result alpha to PS2 integer range (0-255) for comparison
        // against AREF which is in PS2 alpha space.
        int alpha_int = int(result.a * 255.0 + 0.5);
        bool alpha_pass = true;

        if (u_atst == 0) {
            alpha_pass = false;       // NEVER — all pixels fail
        } else if (u_atst == 1) {
            alpha_pass = true;        // ALWAYS — all pixels pass
        } else if (u_atst == 2) {
            alpha_pass = (alpha_int < u_aref);   // LESS
        } else if (u_atst == 3) {
            alpha_pass = (alpha_int <= u_aref);  // LEQUAL
        } else if (u_atst == 4) {
            alpha_pass = (alpha_int == u_aref);  // EQUAL
        } else if (u_atst == 5) {
            alpha_pass = (alpha_int >= u_aref);  // GEQUAL
        } else if (u_atst == 6) {
            alpha_pass = (alpha_int > u_aref);   // GREATER
        } else {
            alpha_pass = (alpha_int != u_aref);  // NOTEQUAL
        }

        if (!alpha_pass) {
            // AFAIL controls what happens when the alpha test fails:
            // 0=KEEP: discard entirely (no FB or ZB write)
            // 1=FB_ONLY: write FB but not ZB (handled via depth mask on CPU side)
            // 2=ZB_ONLY: write ZB but not FB (handled via color mask on CPU side)
            // 3=RGB_ONLY: write RGB but not A or ZB
            // For KEEP (most common), discard the fragment entirely.
            // For other modes, we'd need multi-pass or stencil tricks.
            // For now, implement KEEP (discard) which covers the vast majority
            // of game usage. FB_ONLY/ZB_ONLY/RGB_ONLY are rare edge cases.
            if (u_afail == 0) {
                discard;
            }
            // AFAIL=1 (FB_ONLY): let the fragment through but depth write is
            // disabled on the CPU side when this case is detected.
            // AFAIL=2 (ZB_ONLY): would need to zero out color output.
            // AFAIL=3 (RGB_ONLY): would need to mask alpha channel.
            // These are handled approximately for now.
            if (u_afail == 2) {
                // ZB_ONLY: suppress color write by outputting transparent
                result = vec4(0.0);
            }
            if (u_afail == 3) {
                // RGB_ONLY: keep RGB but zero alpha to suppress alpha write
                result.a = 0.0;
            }
        }
    }

    // --- PS2 Destination Alpha Test (TEST register: DATE, DATM) ---
    // Per GS spec: tests the EXISTING framebuffer alpha at this pixel location.
    // The test reads the destination alpha MSB (bit 7 of 8-bit alpha).
    // DATE=1, DATM=0: pass only if dest alpha bit 7 == 0
    // DATE=1, DATM=1: pass only if dest alpha bit 7 == 1
    // Requires reading the current framebuffer via u_rt_texture (same as sw_blend).
    // When sw_blend is not active, the RT copy may not be available — in that case
    // DATE is handled on the CPU side by forcing an RT copy before the draw.
    if (u_date != 0) {
        vec4 dst = texelFetch(u_rt_texture, ivec2(gl_FragCoord.xy), 0);
        // Convert dest alpha to 8-bit integer to check MSB (bit 7)
        int dst_alpha_int = int(dst.a * 255.0 + 0.5);
        bool dst_msb = (dst_alpha_int & 0x80) != 0;
        if (u_datm == 0) {
            // Pass if dest alpha MSB == 0 (i.e., dest alpha < 128)
            if (dst_msb) discard;
        } else {
            // Pass if dest alpha MSB == 1 (i.e., dest alpha >= 128)
            if (!dst_msb) discard;
        }
    }

    // --- [S9-ZSRC] Depth test against color-authored ZBUF VRAM ---
    // The draw's ZBUF.ZBP addresses bytes a live COLOR target authored
    // (the blob-shadow chain renders an inverted copy of the scene Z into
    // 0x268000 through the color pipeline, then Z-tests 108 volume strips
    // against it). The host FBO's private depth attachment does not hold
    // those bytes, so the test runs here: reconstruct the z32 word from
    // the source target's RGBA8 color at this fragment's coordinates
    // (VRAM byte order R=z[7:0] G=z[15:8] B=z[23:16] A=z[31:24]) and
    // compare per ZTST against the fragment's PS2 z. Under the R112
    // mapping gl_FragCoord.z == interpolated PS2 z / 2^32 exactly, so the
    // compare happens in normalized space (float32 grants ~2^-24 relative
    // precision — ~256 z units near the far plane, far finer than the
    // strip slab widths). ZMSK=1 is a route precondition (s9_shadow.h),
    // so no depth write is needed or performed.
    if (u_zsrc_en != 0) {
        ivec2 zdim = textureSize(u_zsrc_tex, 0);
        int zn = max(u_zsrc_num, 1), zd = max(u_zsrc_den, 1);
        ivec2 zc = clamp(ivec2(gl_FragCoord.xy) * zn / zd, ivec2(0),
                         zdim - ivec2(1));
        vec4 zpix = texelFetch(u_zsrc_tex, zc, 0);
        vec4 zb = floor(zpix * 255.0 + 0.5);
        float dstz = (zb.a * 16777216.0 + zb.b * 65536.0 +
                      zb.g * 256.0 + zb.r) / 4294967296.0;
        if (u_zsrc_en == 3) {
            if (gl_FragCoord.z <= dstz) discard;   // GREATER fails
        } else {
            if (gl_FragCoord.z < dstz) discard;    // GEQUAL fails
        }
    }

    // --- PCSX2-style shader-side alpha blending ---
    // PS2 blend equation: output = (A - B) * C + D
    // where A,B,D select from {Cs, Cd, 0} and C selects from {As, Ad, FIX}
    // Cd is read from a copy of the render target bound to u_rt_texture.
    // GL blending is DISABLED — the shader outputs the final blended pixel.
    if (u_blend_enabled != 0 && u_sw_blend != 0) {
        // Read destination color from RT copy (PCSX2: sample_from_rt)
        vec4 RT = texelFetch(u_rt_texture, ivec2(gl_FragCoord.xy), 0);

        // Convert to PS2 color space [0, 255] for the blend math
        vec3 Cs = result.rgb * 255.0;
        // Alpha must be in PS2 scale: 0-128 maps to 0.0-1.0.
        // result.a is in GL normalized [0,1] where PS2 128 = 128/255 ≈ 0.502.
        // Convert: As_ps2 = result.a * (255.0/128.0), clamped to [0,1].
        float As = clamp(result.a * (255.0 / 128.0), 0.0, 1.0);
        vec3 Cd = RT.rgb * 255.0;
        float Ad = clamp(RT.a * (255.0 / 128.0), 0.0, 1.0);

        // Select A
        vec3 A_val;
        if (u_blend_a == 0) A_val = Cs;
        else if (u_blend_a == 1) A_val = Cd;
        else A_val = vec3(0.0);

        // Select B
        vec3 B_val;
        if (u_blend_b == 0) B_val = Cs;
        else if (u_blend_b == 1) B_val = Cd;
        else B_val = vec3(0.0);

        // Select C (scalar alpha multiplier)
        float C_val;
        if (u_blend_c == 0) C_val = As;
        else if (u_blend_c == 1) C_val = Ad;
        else C_val = u_fix_alpha;

        // Select D
        vec3 D_val;
        if (u_blend_d == 0) D_val = Cs;
        else if (u_blend_d == 1) D_val = Cd;
        else D_val = vec3(0.0);

        // PS2 blend: (A - B) * C + D
        vec3 blended = (A_val - B_val) * C_val + D_val;

        // [S8-WRAPBLEND] COLCLAMP applies to the raw blend RESULT.
        // COLCLAMP=1: saturate to [0,255] (legacy behavior, unchanged).
        // COLCLAMP=0: the GS masks each channel's fixed-point result to
        // its low 8 bits — wrap-around. The old clamp-then-fract order
        // crushed every overflowed channel to 1.0 -> fract 0.0 (black)
        // instead of (v mod 256); the shadow-decal overflow trick
        // (blend_ff_map.h s8wrap header) needs the true modulo.
        if (u_colclamp != 0) {
            result.rgb = clamp(blended, vec3(0.0), vec3(255.0)) / 255.0;
        } else {
            result.rgb = mod(floor(blended + 0.5), 256.0) / 255.0;
        }
        // Alpha: source alpha passes through (PS2 writes As to framebuffer)
    } else if (u_blend_enabled == 0) {
        // ABE=0: blending disabled. Alpha passes through as-is.
        // (PS2 does NOT force alpha to 1.0 when ABE=0 — it just
        // skips the blend equation and writes source directly.)
    }

    // --- PS2 COLCLAMP (Color Clamp/Wrap) ---
    // Per GS spec: applied after alpha blending, before format conversion.
    // COLCLAMP=1: clamp RGB to [0, 255] (saturate) — default, standard GPU behavior.
    // COLCLAMP=0: mask to lower 8 bits (wrap around) — used for special effects.
    // After blending, values may exceed [0,1] in normalized space.
    if (u_colclamp != 0) {
        // Clamp (standard): saturate to [0, 1]
        result.rgb = clamp(result.rgb, vec3(0.0), vec3(1.0));
    } else if (u_blend_enabled == 0 || u_sw_blend == 0) {
        // Wrap: convert to [0,255] integer, mask to 8 bits, convert back.
        // fract() gives the fractional part, which is equivalent to mod 1.0
        // for positive values. For negative values, fract returns 1.0 + fract,
        // which matches the PS2's unsigned wrap behavior.
        // [S8-WRAPBLEND] Skipped when the shader-side blend above already
        // applied the exact mod-256 wrap to the raw result: re-wrapping the
        // normalized value here turned an exact 255/255 into fract(1.0)=0.
        result.rgb = fract(result.rgb);
    }
    // Alpha is always clamped regardless of COLCLAMP setting
    result.a = clamp(result.a, 0.0, 1.0);

    // PS2 FBA (Force Bit Alpha): bitwise OR bit 7 into the output alpha.
    // Per GS spec: A_out = A_in | (FBA << 7)
    // Applied AFTER blending — modifies the pixel written to the framebuffer,
    // not the alpha used as the blend factor.
    // This is a bitwise OR, not a max: e.g. PS2 alpha 76 (0x4C) | 128 (0x80) = 204 (0xCC).
    // The max() approach would give max(76, 128) = 128, losing the lower bits.
    if (u_fba != 0) {
        int a_int = int(result.a * 255.0 + 0.5);
        a_int = a_int | 0x80;  // Set bit 7
        result.a = float(min(a_int, 255)) / 255.0;
    }

    // --- PS2 Alpha Convention: 0x80 (128) = fully opaque ---
    // PS2 alpha range is 0-128 where 128 = 1.0. Values above 128 are clamped.
    // All internal math above operates in PS2 native space (0-255 integer or
    // 0.0-1.0 GL normalized where 128/255 ≈ 0.502 = PS2 opaque).
    // GL blending and framebuffer writes interpret alpha 1.0 as fully opaque,
    // so we must rescale: PS2 alpha 128 → GL alpha 1.0.
    // Formula: gl_alpha = min(ps2_alpha * 2, 255) / 255.0
    // In normalized space: gl_alpha = min(result.a * (255.0/128.0), 1.0)
    //
    // NOTE: Only apply the rescale when GL fixed-function blending is active
    // (u_sw_blend == 0). When shader-side blending is active, the blend math
    // already operates in PS2 native space and the alpha written to the
    // framebuffer should remain in PS2 convention for correct DATE/FBA behavior
    // in subsequent passes.
    //
    // [S9-ABIT16] 16-bit COLOR frames (PSMCT16/16S) store only the A-bit:
    // the FBO alpha channel now encodes it directly (1.0 iff PS2 alpha >=
    // 0x80), REPLACING the x2 rescale for those targets. The x2 convention
    // destroyed the bit for PS2 alphas in [0x40,0x7F] (doubled >= 0x80) —
    // the blob-shadow accumulator's 0x78 clear read back as DATE-blocked
    // everywhere, discarding all 108 volume strips. With the A-bit stored,
    // the DATE test above and the fbmsk16 merge below keep their bit-7
    // semantics unchanged. FF-blended draws (u_blend_enabled=1, u_sw_blend
    // =0) are excluded: GL uses this output as the blend FACTOR, which
    // needs the x2 scale (s9_shadow.h Abit16Applies — the recipe never
    // authors accumulator alpha through FF blend). 32-bit targets are
    // byte-identical to the legacy path.
    if (u_fb16 != 0 && (u_blend_enabled == 0 || u_sw_blend != 0)) {
        result.a = (int(result.a * 255.0 + 0.5) >= 128) ? 1.0 : 0.0;
    } else if (u_sw_blend == 0) {
        result.a = clamp(result.a * (255.0 / 128.0), 0.0, 1.0);
    }

    // --- [WN4-CT16Q] Retail 16-bit frame write rule ---
    // GS pipeline order for CT16-class frames: blend -> COLCLAMP -> FBA ->
    // dither (DTHE=1: DIMX[y&3][x&3] added per channel, clamped) -> 5-bit
    // truncation at storage. The RT stores the bit-replicate expansion so
    // every reader (Cd copy, fbmsk16 merge below — idempotent >>3 pack —
    // fmt=5/6 aliasing serves, SyncRTToVRAM) observes the retail value.
    // Excluded when GL fixed-function blending consumes this output as a
    // factor/source (same predicate as the A-bit block above): the stored
    // pixel is post-GL-blend there and cannot be quantized in-shader.
    if (u_ct16q != 0 && (u_blend_enabled == 0 || u_sw_blend != 0)) {
        ivec3 cq = ivec3(result.rgb * 255.0 + 0.5);
        if (u_dthe != 0) {
            int nx = (int(gl_FragCoord.x) / u_ct16q) & 3;
            int ny = (int(gl_FragCoord.y) / u_ct16q) & 3;
            int sh = ny * 16 + nx * 4;
            uint w = uint((ny < 2) ? u_dimx_lo : u_dimx_hi);
            int dm = int((w >> uint(sh & 31)) & 7u);
            if (dm >= 4) dm -= 8;
            cq = clamp(cq + ivec3(dm), ivec3(0), ivec3(255));
        }
        ivec3 q5 = cq >> 3;
        // Zero-fill expansion (q5 << 3), NOT bit-replicate: the GS hands a
        // 16-bit frame's Cd back to the blender as the 5-bit value << 3 —
        // the game's own shadow recipe arithmetic depends on it (pass-A
        // clear 0x78 = 15<<3; strips step by exactly +/-8 per wrap-add;
        // every reachable byte is a multiple of 8). Bit-replicate turned
        // the 0x78 clear into 123, broke the [S9-PRIMED] byte check and
        // destabilized the whole composite (measured: flip_dark 0.026 ->
        // 0.39 before this correction). Masked draws that fall through the
        // fbmsk16 merge below keep that path's pre-existing replicate
        // convention (proven in its own loop; serves re-pack with >>3, so
        // the stored 5-bit data is identical either way).
        result.rgb = vec3(q5 << 3) / 255.0;
    }

    // --- Exact-bit FBMSK merge for 16-bit frames (PSMCT16/PSMCT16S) ---
    // GS FBMSK semantics: a 1 bit PRESERVES the destination bit. For 16-bit
    // frames the mask has the 5551 layout R[0:4] G[5:9] B[10:14] A[15].
    // glColorMask can only express whole-channel masks, so for masks that
    // split a channel (e.g. FBMSK=0x3fff -> fbmsk16=0x00FF) we do the merge
    // here: pack src and dst to 5551, combine bitwise, unpack. The dest
    // pixel comes from u_rt_texture (rt_copy of the FBO, bound pre-draw).
    // Packing matches SyncRTToVRAM (>>3 per channel, alpha MSB at >127) and
    // unpacking matches Expand16to32 (bit-replicate 5->8, A -> 0/255).
    if (u_fbmsk16_en != 0) {
        ivec4 s = ivec4(result * 255.0 + 0.5);
        int src_bits = ((s.r >> 3) & 0x1F)
                     | (((s.g >> 3) & 0x1F) << 5)
                     | (((s.b >> 3) & 0x1F) << 10)
                     | ((s.a > 127 ? 1 : 0) << 15);
        vec4 dst = texelFetch(u_rt_texture, ivec2(gl_FragCoord.xy), 0);
        ivec4 d = ivec4(dst * 255.0 + 0.5);
        int dst_bits = ((d.r >> 3) & 0x1F)
                     | (((d.g >> 3) & 0x1F) << 5)
                     | (((d.b >> 3) & 0x1F) << 10)
                     | ((d.a > 127 ? 1 : 0) << 15);
        int merged = (src_bits & ~u_fbmsk16) | (dst_bits & u_fbmsk16);
        int r5 = merged & 0x1F;
        int g5 = (merged >> 5) & 0x1F;
        int b5 = (merged >> 10) & 0x1F;
        result.r = float((r5 << 3) | (r5 >> 2)) / 255.0;
        result.g = float((g5 << 3) | (g5 >> 2)) / 255.0;
        result.b = float((b5 << 3) | (b5 >> 2)) / 255.0;
        result.a = ((merged & 0x8000) != 0) ? 1.0 : 0.0;
    }

    frag_color = result;
}
)GLSL";

const char* const kScanoutVertSrc = R"GLSL(
#version 410 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;

out vec2 v_texcoord;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
}
)GLSL";

const char* const kScanoutFragSrc = R"GLSL(
#version 410 core

in vec2 v_texcoord;
uniform sampler2D u_texture;
out vec4 frag_color;

void main() {
    vec4 c = texture(u_texture, v_texcoord);
    // PS2 PCRTC outputs RGB video — alpha is never part of the display signal.
    // Force alpha=1 to prevent macOS compositor from treating the window as transparent.
    frag_color = vec4(c.rgb, 1.0);
}
)GLSL";

// ---- stencil-composite programs (OLD s9_composite_native.inc:53-215, byte-identical) ----
// The accumulator CLEAR arms folded (unused by the NEW device -- the CLEAR runs the
// register program, whose live arms these are -- kept for the text-hash gate).
const char* const kStencilClearFragSrc = R"(
#version 410 core

noperspective in vec3 v_texcoord;
in vec4 v_color;
in float v_fog;

uniform int u_fb16;

out vec4 frag_color;

void main() {
    // Untextured: output vertex color directly (generic else-arm verbatim).
    vec4 result = v_color;
    result = clamp(result, 0.0, 1.0);
    // ABE=0: no blend. COLCLAMP=1 post-blend clamp (generic arm verbatim).
    result.rgb = clamp(result.rgb, vec3(0.0), vec3(1.0));
    // Alpha is always clamped regardless of COLCLAMP setting (verbatim).
    result.a = clamp(result.a, 0.0, 1.0);
    // [S9-ABIT16] 16-bit COLOR frame, unblended write: store the A-bit
    // (generic arm verbatim; u_blend_enabled==0 for this config).
    if (u_fb16 != 0) {
        result.a = (int(result.a * 255.0 + 0.5) >= 128) ? 1.0 : 0.0;
    }
    frag_color = result;
}
)";
// Strip counting pass: DATE + colour-authored-Z discards, no colour output.
const char* const kStencilStripFragSrc = R"(
#version 410 core

noperspective in vec3 v_texcoord;
in vec4 v_color;
in float v_fog;

uniform sampler2D u_rt_texture; // DATE source (unit 1)
uniform sampler2D u_zsrc_tex;   // [S9-ZSRC] color-authored Z (unit 4)
uniform int u_zsrc_en;          // 2=GEQUAL, 3=GREATER (= ztst)
uniform int u_datm;             // census: 0
// [SH8] zsrc coordinate map, the [GE1] generic-shader mapping mirrored
// verbatim: zc = fragcoord * num / den (num = SOURCE target scale, den =
// DEST target scale; both 1 at PS2_GS_SCALE=1 -- integer identity,
// byte-identical). The accumulator is pinned native while the z-copy
// target scales, so without this map a 2x run fetches the bottom-left
// quadrant of the z image (playtest R1 [SH8]: camera-tied mask decay).
uniform int u_zsrc_num;
uniform int u_zsrc_den;

void main() {
    // --- PS2 Destination Alpha Test (generic u_date arm verbatim) ---
    {
        vec4 dst = texelFetch(u_rt_texture, ivec2(gl_FragCoord.xy), 0);
        int dst_alpha_int = int(dst.a * 255.0 + 0.5);
        bool dst_msb = (dst_alpha_int & 0x80) != 0;
        if (u_datm == 0) {
            if (dst_msb) discard;
        } else {
            if (!dst_msb) discard;
        }
    }
    // --- [S9-ZSRC] depth test against color-authored ZBUF (verbatim) ---
    if (u_zsrc_en != 0) {
        ivec2 zdim = textureSize(u_zsrc_tex, 0);
        int zn = max(u_zsrc_num, 1), zd = max(u_zsrc_den, 1);
        ivec2 zc = clamp(ivec2(gl_FragCoord.xy) * zn / zd, ivec2(0),
                         zdim - ivec2(1));
        vec4 zpix = texelFetch(u_zsrc_tex, zc, 0);
        vec4 zb = floor(zpix * 255.0 + 0.5);
        float dstz = (zb.a * 16777216.0 + zb.b * 65536.0 +
                      zb.g * 256.0 + zb.r) / 4294967296.0;
        if (u_zsrc_en == 3) {
            if (gl_FragCoord.z <= dstz) discard;   // GREATER fails
        } else {
            if (gl_FragCoord.z < dstz) discard;    // GEQUAL fails
        }
    }
    // Surviving fragment: stencil op does the counting. No color output.
}
)";
// The folded byte-NOT self-read (rgb = ~Cd, a = 255 - CSM1(idx)).
const char* const kStencilInvertFragSrc = R"(
#version 410 core

noperspective in vec3 v_texcoord;
in vec4 v_color;
in float v_fog;

uniform sampler2D u_rt_texture; // pre-draw destination copy (unit 1)
uniform float u_fix_alpha;      // FIX/128 (= 1.0 for the pinned config)

out vec4 frag_color;

void main() {
    float inv_q = (v_texcoord.z != 0.0) ? (1.0 / v_texcoord.z) : 1.0;
    vec2  rtc = v_texcoord.xy * inv_q;          // raw texel space (verbatim)
    ivec2 tdim = textureSize(u_rt_texture, 0);  // == u_tex_size (target dims)
    ivec2 tc = clamp(ivec2(floor(rtc)), ivec2(0), tdim - 1);
    vec4 src = texelFetch(u_rt_texture, tc, 0);
    // [R164-IDXTEX] fmt=4 index reconstruction (generic arm verbatim).
    uint a_gpu = uint(floor(src.a * 255.0 + 0.5));
    uint idx = (a_gpu >= 255u) ? 128u : (a_gpu >> 1u);
    // Verified CLUT row-1 texel fold, ORACLE-effective table ([GD4]):
    // the physical grid holds {FFFFFF, 255-CSM1(p)} and the legacy
    // chain's two CSM1 involutions cancel, so the generic serve's row-1
    // texel at idx is {255,255,255, 255-CSM1(idx)} ([S9-CLUTRAW] raw
    // row — no alpha inverse). TFX HIGHLIGHT2/TCC=1 with the pinned
    // vertex color (0x80,0x80,0x80,0x00): FxT.rgb = trunc(128*255/128) =
    // 255, +vc.a(0); result folds to white with alpha 255-CSM1(idx).
    uint w32 = idx & 31u;
    uint sidx = (w32 >= 8u && w32 < 16u) ? (idx + 8u)
              : ((w32 >= 16u && w32 < 24u) ? (idx - 8u) : idx);
    vec4 result = vec4(1.0, 1.0, 1.0, (255.0 - float(sidx)) / 255.0);
    result = clamp(result, 0.0, 1.0);
    // sw blend, ABCD=(0,1,2,2): (Cs - Cd)*FIX + 0 (generic selects folded;
    // expression shape and literals preserved).
    {
        vec4 RT = texelFetch(u_rt_texture, ivec2(gl_FragCoord.xy), 0);
        vec3 Cs = result.rgb * 255.0;
        vec3 Cd = RT.rgb * 255.0;
        vec3 A_val = Cs;
        vec3 B_val = Cd;
        float C_val = u_fix_alpha;
        vec3 D_val = vec3(0.0);
        vec3 blended = (A_val - B_val) * C_val + D_val;
        // COLCLAMP=1 (generic arm verbatim).
        result.rgb = clamp(blended, vec3(0.0), vec3(255.0)) / 255.0;
    }
    result.rgb = clamp(result.rgb, vec3(0.0), vec3(1.0));
    result.a = clamp(result.a, 0.0, 1.0);
    // 32-bit frame under sw blend: alpha stored RAW (the generic's u_fb16
    // and x2-rescale arms are both unreachable for this config).
    frag_color = result;
}
)";
// Constant-colour NDC triangle for the stencil-masked quads.
const char* const kConstVertSrc = R"(
#version 410 core
layout(location = 0) in vec2 a_position;
void main() { gl_Position = vec4(a_position, 0.0, 1.0); }
)";
const char* const kConstFragSrc = R"(
#version 410 core
uniform vec4 u_color;
out vec4 frag_color;
void main() { frag_color = u_color; }
)";

}  // namespace

const char* GlShaderSources::Ps2Vertex() { return kPs2VertSrc; }
const char* GlShaderSources::Ps2Fragment() { return kPs2FragSrc; }
const char* GlShaderSources::ScanoutVertex() { return kScanoutVertSrc; }
const char* GlShaderSources::ScanoutFragment() { return kScanoutFragSrc; }
const char* GlShaderSources::StencilClearFragment() { return kStencilClearFragSrc; }
const char* GlShaderSources::StencilStripFragment() { return kStencilStripFragSrc; }
const char* GlShaderSources::StencilInvertFragment() { return kStencilInvertFragSrc; }
const char* GlShaderSources::ConstVertex() { return kConstVertSrc; }
const char* GlShaderSources::ConstFragment() { return kConstFragSrc; }

}  // namespace render
}  // namespace ps2r
