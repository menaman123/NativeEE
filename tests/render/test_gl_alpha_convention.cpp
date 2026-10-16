// tests/render/test_gl_alpha_convention.cpp -- the pipeline's ONE alpha convention
// (the stored-alpha convention): every texel, CLUT entry and colour attachment carries the GS alpha
// byte as the GS stores it (0..0xFF), and 0x80 = 1.0 is applied only where the GS
// applies it -- the blend factor As and the alpha test's compare against AREF.
//
// A 4x4 PSMCT16 texture with the A bit alternating per column is decoded through
// TextureDecoder under TEXA (TA0 = 0x40, TA1 = 0x80): the texels carry 0x40 / 0x80
// (no GPU rescale). Drawn DECAL over a black target with ALPHA (0,1,0,1) = Cs As +
// Cd (1 - As) on the fixed-function route: the TA0 columns blend at As = 0x40 / 0x80
// = 0.5 (white -> 128) and the TA1 columns at 1.0 (255); the STORED alpha is the
// texel's byte (0x40 / 0x80), not the factor. An unblended draw stores 0xC0 (a byte
// the x2 convention could not hold); the alpha test GEQUAL AREF 0x80 compares the
// stored byte; DATE reads bit 7 of the stored byte (0x40 passes DATM 0). Exit 77
// (SKIP) without a context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/gs/format/psm.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"
#include "engine/render/backends/gl/gl_draw_executor.h"

using namespace ps2r;

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

constexpr int kW = 16, kH = 16;

const uint8_t* Px(const std::vector<uint8_t>& fb, int x, int y) {
    return fb.data() + (static_cast<size_t>(y) * kW + x) * 4;
}

// A textured sprite (x0,y0)-(x1,y1) with UV (u0,v0)-(u1,v1) in texels, expanded as
// VertexKick::EmitSprite does; the GS samples pixel i at the integer point, so a
// 1:1 copy authors u = x + 0.5.
void Quad(gs::DrawIr& ir, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
          float v1, uint8_t a) {
    auto V = [&](float x, float y, float u, float v) {
        gs::VertexRec r{};
        r.x = x; r.y = y; r.z = 1000.f; r.q = 1.f;
        r.uv_u = static_cast<uint16_t>(u * 16.f); r.uv_v = static_cast<uint16_t>(v * 16.f);
        r.s = u; r.t = v;
        r.r = 255; r.g = 255; r.b = 255; r.a = a;
        return r;
    };
    ir.verts = {V(x0, y0, u0, v0), V(x1, y0, u1, v0), V(x0, y1, u0, v1),
                V(x1, y0, u1, v0), V(x1, y1, u1, v1), V(x0, y1, u0, v1)};
    ir.vertex_count = 6;
}

}  // namespace

int main() {
    // --- CPU: the decoder stores TEXA's byte, nothing rescaled ---
    std::vector<uint8_t> vram(1u << 20, 0);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            // white 5:5:5, A bit = x odd
            const uint16_t px = static_cast<uint16_t>(0x7FFF | ((x & 1) ? 0x8000 : 0));
            const uint32_t at = gs::PsmLayout::PixelByteAddress(gs::Psm::CT16, x, y, 0, 1);
            vram[at] = static_cast<uint8_t>(px);
            vram[at + 1] = static_cast<uint8_t>(px >> 8);
        }
    gs::TexReadView view;
    view.tbp = 0; view.tbw = 64; view.psm = 0x02; view.tw = 2; view.th = 2;
    view.tcc = 1; view.tfx = 1; view.fst = true;
    view.texa_ta0 = 0x40; view.texa_ta1 = 0x80; view.texa_aem = false;
    std::vector<uint32_t> texels;
    gs::TextureDecoder::Decode(vram.data(), view, nullptr, nullptr, &texels);
    CHECK(texels.size() == 16, "4x4 decoded");
    bool texa_ok = texels.size() == 16;
    for (size_t i = 0; i < texels.size() && texa_ok; ++i) {
        const uint32_t want_a = (i & 1) ? 0x80u : 0x40u;
        texa_ok = (texels[i] >> 24) == want_a && (texels[i] & 0x00FFFFFFu) == 0x00FFFFFFu;
    }
    CHECK(texa_ok, "CT16 + TEXA decodes to alpha bytes 0x40 (A=0 -> TA0) / 0x80 (A=1 -> TA1), stored as is");

    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_alpha_convention") || !ctx.Create(win)) {
        std::printf("test_gl_alpha_convention: no GL context -> SKIP (77)\n");
        return g_fail ? 1 : 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());

    render::SurfaceDesc cd; cd.width = kW; cd.height = kH; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    render::SurfaceDesc td; td.width = 4; td.height = 4; td.format = render::SurfaceFormat::Rgba8;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    const render::SurfaceId tex = dev.CreateSurface(td);
    CHECK(color.id != 0 && depth.id != 0 && tex.id != 0, "surfaces created");
    dev.Upload(tex, render::Rect{0, 0, 4, 4}, texels.data(), 16);

    gs::DrawIr ir;
    ir.prim_type = 6;
    ir.raster.frame.fbw = kW; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, kW - 1, kH - 1};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;   // ALWAYS
    ir.raster.colclamp = true;
    ir.tex_enabled = true;
    ir.tex = view;
    gs::TextureSource ts;
    ts.enabled = true;
    ts.owner.kind = gs::TextureSource::OwnerKind::Decoded;
    ts.owner.surface = tex;
    ts.transform.uv_tex_w = 4; ts.transform.uv_tex_h = 4;
    gs::TextureSource none;   // untextured
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = kW; p.render_h = kH; p.target_w = kW; p.target_h = kH; p.scale = 1;
    p.cd_copy = &cds;
    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH * 4);
    auto draw = [&](const gs::TextureSource& src) {
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, src, p);
        dev.EndDrawRun();
        dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
        return r;
    };

    // 1. DECAL, ALPHA (0,1,0,1) fixed-function over black: Cv = Cs As + Cd (1 - As).
    dev.Clear(color, depth, 0x00000000u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.alpha_blend = true;
    ir.raster.alpha = {0, 1, 0, 1, 0};
    Quad(ir, 0.f, 0.f, 4.f, 4.f, 0.5f, 0.5f, 4.5f, 4.5f, 0x80);
    const render::DrawResult r1 = draw(ts);
    CHECK(r1.drew && !r1.sw_blend_active, "textured blended draw took the fixed-function route");
    const uint8_t* p0 = Px(fb, 0, 1);   // column 0: A bit 0 -> TA0 0x40 -> As 0.5
    const uint8_t* p1 = Px(fb, 1, 1);   // column 1: A bit 1 -> TA1 0x80 -> As 1.0
    std::printf("ff blend: col0 %u %u %u a=%u  col1 %u %u %u a=%u\n", p0[0], p0[1], p0[2], p0[3], p1[0], p1[1], p1[2], p1[3]);
    CHECK(p0[0] >= 127 && p0[0] <= 129 && p0[1] >= 127 && p0[1] <= 129, "As = 0x40 blends white over black to 128 (the 0x80 = 1.0 factor)");
    CHECK(p0[3] == 0x40, "the stored alpha is the texel's byte 0x40, not the factor");
    CHECK(p1[0] == 255 && p1[1] == 255 && p1[2] == 255, "As = 0x80 blends to the source (1.0)");
    CHECK(p1[3] == 0x80, "the stored alpha is the texel's byte 0x80");
    CHECK(Px(fb, 8, 8)[0] == 0 && Px(fb, 8, 8)[3] == 0, "outside the sprite untouched");

    // 2. Unblended vertex alpha 0xC0 stores 0xC0 (unrepresentable under the x2 convention).
    ir.alpha_blend = false;
    ir.raster.alpha = {};
    Quad(ir, 8.f, 8.f, 12.f, 12.f, 0.5f, 0.5f, 4.5f, 4.5f, 0xC0);
    draw(none);
    CHECK(Px(fb, 9, 9)[3] == 0xC0 && Px(fb, 9, 9)[0] == 255, "an unblended draw stores alpha 0xC0 as written");

    // 3. The alpha test compares the stored byte with AREF: GEQUAL 0x80 keeps the TA1 columns only.
    dev.Clear(color, depth, 0x00000000u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.test.ate = true; ir.raster.test.atst = 5; ir.raster.test.aref = 0x80; ir.raster.test.afail = 0;
    Quad(ir, 0.f, 0.f, 4.f, 4.f, 0.5f, 0.5f, 4.5f, 4.5f, 0x80);
    draw(ts);
    CHECK(Px(fb, 0, 1)[0] == 0 && Px(fb, 0, 1)[3] == 0, "alpha 0x40 fails GEQUAL AREF 0x80 (KEEP)");
    CHECK(Px(fb, 1, 1)[0] == 255 && Px(fb, 1, 1)[3] == 0x80, "alpha 0x80 passes GEQUAL AREF 0x80");
    ir.raster.test.ate = false;

    // 4. DATE reads bit 7 of the stored byte: a 0x40 destination passes DATM 0 and fails DATM 1.
    dev.Clear(color, depth, 0x40000000u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.test.date = true; ir.raster.test.datm = false;
    Quad(ir, 0.f, 0.f, 4.f, 4.f, 0.5f, 0.5f, 4.5f, 4.5f, 0x80);
    draw(none);
    CHECK(Px(fb, 1, 1)[0] == 255, "DATE DATM 0 over stored alpha 0x40 (bit 7 clear) draws");
    dev.Clear(color, depth, 0x40000000u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.test.datm = true;
    draw(none);
    CHECK(Px(fb, 1, 1)[0] == 0 && Px(fb, 1, 1)[3] == 0x40, "DATE DATM 1 over stored alpha 0x40 blocks");
    ir.raster.test.date = false;

    CHECK(glGetError() == GL_NO_ERROR, "no GL error");
    dev.DestroySurface(tex);
    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_gl_alpha_convention: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
