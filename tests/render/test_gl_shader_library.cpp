// tests/render/test_gl_shader_library.cpp -- Render_ShaderCompiles: the baseline shader
// text (hash-pinned to the OLD capture with the two defines in the baseline order),
// both programs compile + link on this machine's GL 4.1 context, and every uniform the
// draw executor sets resolves. Exit 77 (SKIP) without a context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
#include <cstring>

#include "engine/core/hash/hash.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"
#include "engine/render/backends/gl/gl_shader_library.h"
#include "engine/render/backends/gl/gl_shader_sources.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

// FNV-1a64 of the exact texts (/tmp/p6/gen_shader_sources.py over the OLD captures).
// The vertex text is the OLD baseline (0x4fd2767450e43bb7) plus the `v_z` output
// (the PS2 z, screen-linear) the fragment program turns into gl_FragDepth =
// z * 2^-32: the clip-space z keeps the OLD mapping (clipping only), the window
// depth is exact for small z (GS Z tests of z < 64 against a 0-cleared Z buffer).
static const uint64_t kVertHash = 0xa12a8c2a25c17ff7ull;
// The fragment text is the OLD baseline arm (0x3ffd78421c147ece) plus TWO
// additive branches: the u_idx_fmt == 7 serve (the depth-byte palette lookup the
// owned colour filter asks for: clut[(Z >> u_idx_self) & 0xFF] of the bound
// Depth32f texel under this fragment; screen_renderer.cpp; 0xb5b8e150faca35c5)
// and the A2 texture-filtering enhancement's `u_tex_lod_hw` (the mip arm samples
// with texture()'s hardware LOD instead of textureLod's retail formula when the
// executor set it -- 0 on every draw under the title's own choice); then
// (0x3d5f929280797bd3) the exact window depth: `gl_FragDepth = v_z * 2^-32` written
// first in main (before any discard) and the colour-authored Z compare reading that
// value instead of gl_FragCoord.z; then (0x1c4e1e3d11fc8ad9) the exact 32-bit FBMSK
// merge (`u_fbmsk32_en` / `u_fbmsk32`: a sub-byte channel mask on a 32-bit frame merged
// per bit against the Cd copy, after the fbmsk16 merge). Every other arm's text is
// unchanged; re-pin only together with a reviewed shader change.
static const uint64_t kFragHash = 0x9aa9c998af376fe4ull;
static const uint64_t kScanoutVertHash = 0x8823cded4f383d15ull;
static const uint64_t kScanoutFragHash = 0x5d955137d32b0c78ull;
// The stencil-composite sources (OLD s9_composite_native.inc:53-215 k_s9_* texts).
static const uint64_t kStencilClearHash = 0xaf25eec16f07cca3ull;
static const uint64_t kStencilStripHash = 0x8fcb62c7a48824b9ull;
static const uint64_t kStencilInvertHash = 0xda3087d8b47270ccull;
static const uint64_t kConstVertHash = 0xa8293c48adbb37b0ull;
static const uint64_t kConstFragHash = 0xb5e42c4bdc02e060ull;

int main() {
    // Text checks first (no context needed).
    const char* frag = render::GlShaderLibrary::FragmentSource();
    const char* head = "\n#version 410 core\n#define GSMIP_AAF 1\n#define S83_IDXSCALE 1\n";
    CHECK(std::strncmp(frag, head, std::strlen(head)) == 0, "fragment header + define order");
    if (hash::Fnv1a64(frag, std::strlen(frag)) != kFragHash)
        std::printf("fragment text hash %016llx\n", static_cast<unsigned long long>(hash::Fnv1a64(frag, std::strlen(frag))));
    CHECK(hash::Fnv1a64(frag, std::strlen(frag)) == kFragHash, "fragment text hash == the pinned register program");
    const char* vert = render::GlShaderLibrary::VertexSource();
    CHECK(hash::Fnv1a64(vert, std::strlen(vert)) == kVertHash, "vertex text hash == OLD");
    const char* sv = render::GlShaderLibrary::ScanoutVertexSource();
    const char* sf = render::GlShaderLibrary::ScanoutFragmentSource();
    CHECK(hash::Fnv1a64(sv, std::strlen(sv)) == kScanoutVertHash, "scanout vertex hash == OLD");
    CHECK(hash::Fnv1a64(sf, std::strlen(sf)) == kScanoutFragHash, "scanout fragment hash == OLD");
    CHECK(std::strstr(frag, "u_pk_i") == nullptr, "no packed-uniform arm in the text");
    // Render_S9ShaderText: the stencil-composite texts byte-identical to OLD.
    auto H = [](const char* t) { return hash::Fnv1a64(t, std::strlen(t)); };
    CHECK(H(render::GlShaderSources::StencilClearFragment()) == kStencilClearHash, "stencil clear text == OLD");
    CHECK(H(render::GlShaderSources::StencilStripFragment()) == kStencilStripHash, "stencil strip text == OLD");
    CHECK(H(render::GlShaderSources::StencilInvertFragment()) == kStencilInvertHash, "invert text == OLD");
    CHECK(H(render::GlShaderSources::ConstVertex()) == kConstVertHash, "const vertex text == OLD");
    CHECK(H(render::GlShaderSources::ConstFragment()) == kConstFragHash, "const fragment text == OLD");
    CHECK(std::strstr(render::GlShaderSources::StencilInvertFragment(), "(255.0 - float(sidx)) / 255.0") != nullptr,
          "invert alpha = 255 - CSM1(idx)");

    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_shader_library") || !ctx.Create(win)) {
        std::printf("test_gl_shader_library: no GL context -> SKIP (77)\n");
        return 77;
    }
    std::printf("GL %s / %s\n", ctx.Version(), ctx.Renderer());
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "GlDevice::Init");
    CHECK(dev.Shaders().Ps2Program() != 0, "PS2 program linked");
    CHECK(dev.Shaders().ScanoutProgram() != 0, "scanout program linked");
    CHECK(dev.Shaders().StencilStripProgram() != 0 && dev.Shaders().StencilInvertProgram() != 0 &&
          dev.Shaders().ConstProgram() != 0, "stencil-composite programs linked");
    CHECK(dev.Shaders().StencilLocations().strip_zsrc_en >= 0 && dev.Shaders().StencilLocations().invert_fix >= 0 &&
          dev.Shaders().StencilLocations().const_color >= 0, "stencil-composite uniform locations");
    CHECK(dev.Shaders().ScanoutTextureLocation() >= 0, "scanout u_texture");

    const render::GlUniformLocations& L = dev.Shaders().Locations();
    struct Named { const char* name; int loc; };
    const Named named[] = {
        {"u_screen_size", L.screen_size}, {"u_use_texture", L.use_texture}, {"u_texture", L.tex_sampler},
        {"u_blend_a", L.blend_a}, {"u_blend_b", L.blend_b}, {"u_blend_c", L.blend_c}, {"u_blend_d", L.blend_d},
        {"u_fix_alpha", L.fix_alpha}, {"u_blend_enabled", L.blend_enabled}, {"u_tfx", L.tfx}, {"u_tcc", L.tcc},
        {"u_fba", L.fba}, {"u_colclamp", L.colclamp}, {"u_scanmsk", L.scanmsk}, {"u_ate", L.ate},
        {"u_atst", L.atst}, {"u_aref", L.aref}, {"u_afail", L.afail}, {"u_date", L.date}, {"u_datm", L.datm},
        {"u_fog_enabled", L.fog_enabled}, {"u_fogcol", L.fogcol}, {"u_tex_size", L.tex_size},
        {"u_uv_scale", L.uv_scale}, {"u_uv_mode", L.uv_mode}, {"u_sw_blend", L.sw_blend},
        {"u_rt_texture", L.rt_sampler}, {"u_flip_v", L.flip_v}, {"u_fbmsk16_en", L.fbmsk16_en},
        {"u_fbmsk16", L.fbmsk16}, {"u_depth_fmt", L.depth_fmt}, {"u_depth_zoff", L.depth_zoff},
        {"u_idx_fmt", L.idx_fmt}, {"u_idx_self", L.idx_self}, {"u_draw_scale", L.draw_scale},
        {"u_uv_native", L.uv_native}, {"u_clut", L.clut_sampler}, {"u_idx_tex2", L.idx_tex2_sampler},
        {"u_fb16", L.fb16}, {"u_ct16q", L.ct16q}, {"u_dthe", L.dthe}, {"u_dimx_lo", L.dimx_lo},
        {"u_dimx_hi", L.dimx_hi}, {"u_zsrc_en", L.zsrc_en}, {"u_zsrc_tex", L.zsrc_sampler},
        {"u_zsrc_num", L.zsrc_num}, {"u_zsrc_den", L.zsrc_den}, {"u_clut_raw", L.clut_raw},
        {"u_mip_ctl", L.mip_ctl}, {"u_mip_lodp", L.mip_lodp},
    };
    int resolved = 0;
    for (const Named& n : named) {
        if (n.loc < 0) std::printf("  unresolved uniform %s\n", n.name);
        else resolved++;
        CHECK(n.loc >= 0, n.name);
    }
    std::printf("uniforms resolved: %d / %d\n", resolved, static_cast<int>(sizeof(named) / sizeof(named[0])));
    // u_zte / u_ztst are declared but never read by the fragment shader: the driver may
    // strip them (location -1 is legal); the executor never sets them.
    std::printf("u_zte=%d u_ztst=%d (informational)\n", L.zte, L.ztst);
    CHECK(glGetError() == GL_NO_ERROR, "no GL error after Init");
    dev.Shutdown();
    std::printf("test_gl_shader_library: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
