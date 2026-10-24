// tests/render/test_gl_alpha_fail_split.cpp -- Render_AlphaFailSplit: the per-pixel alpha
// test's two populations (GS User's Manual, TEST register). With ATE = 1 and a per-pixel
// ATST (here GEQUAL AREF 0x80) a pixel that PASSES updates the frame and Z buffers per
// FBMSK / ZMSK; a pixel that FAILS updates what AFAIL names -- FB_ONLY: the frame buffer
// and not Z; RGB_ONLY: RGB only, neither alpha nor Z; ZB_ONLY: Z and no colour; KEEP:
// nothing. One draw holds both populations (a left quad whose alpha passes, a right
// quad whose alpha fails), and the failing population's colour is blended with ITS
// OWN As (ALPHA C = As): the test never alters the fragment's alpha. Exit 77 (SKIP)
// without a GL context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

namespace {

const uint8_t* Px(const std::vector<uint8_t>& fb, int x, int y) { return fb.data() + (static_cast<size_t>(y) * 64 + x) * 4; }

gs::VertexRec V(float x, float y, uint8_t a) {
    gs::VertexRec v{};
    v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f;
    v.r = 255; v.g = 0; v.b = 0; v.a = a;
    return v;
}

// Two quads in one draw: x < 32 with alpha `pass_a`, x >= 32 with alpha `fail_a`.
void TwoQuads(gs::DrawIr& ir, uint8_t pass_a, uint8_t fail_a) {
    ir.verts = {V(0.f, 0.f, pass_a),  V(32.f, 0.f, pass_a),  V(0.f, 64.f, pass_a),
                V(32.f, 0.f, pass_a), V(32.f, 64.f, pass_a), V(0.f, 64.f, pass_a),
                V(32.f, 0.f, fail_a), V(64.f, 0.f, fail_a),  V(32.f, 64.f, fail_a),
                V(64.f, 0.f, fail_a), V(64.f, 64.f, fail_a), V(32.f, 64.f, fail_a)};
    ir.vertex_count = 12;
}

bool Near(int v, int want, int tol) { return std::abs(v - want) <= tol; }

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_alpha_fail_split") || !ctx.Create(win)) {
        std::printf("test_gl_alpha_fail_split: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());
    render::SurfaceDesc cd; cd.width = 64; cd.height = 64; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id != 0 && depth.id != 0, "surfaces created");

    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.alpha_blend = false;
    ir.raster.frame.fbw = 64; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;   // ALWAYS: Z is decided by the alpha test alone
    ir.raster.test.ate = true; ir.raster.test.atst = 5; ir.raster.test.aref = 0x80;   // GEQUAL 0x80
    ir.raster.colclamp = true;
    gs::TextureSource ts;   // untextured: the fragment alpha is the vertex alpha
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = 64; p.render_h = 64; p.target_w = 64; p.target_h = 64; p.scale = 1;
    p.cd_copy = &cds;
    std::vector<uint8_t> fb(64 * 64 * 4, 0);
    std::vector<float> zb(64 * 64, 1.f);
    auto draw = [&](uint8_t afail, uint8_t pass_a, uint8_t fail_a) {
        dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
        ir.raster.test.afail = afail;
        TwoQuads(ir, pass_a, fail_a);
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
        dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * 4);
        return r.drew;
    };
    const int L = 8, R = 56, Y = 32;   // one pixel of each population

    // FB_ONLY: both populations paint; only the passing one writes Z.
    CHECK(draw(1, 0xC0, 0x40), "FB_ONLY draw executed");
    CHECK(Px(fb, L, Y)[0] == 255 && Px(fb, L, Y)[3] == 0xC0, "FB_ONLY: passing pixel red, alpha stored");
    CHECK(Px(fb, R, Y)[0] == 255 && Px(fb, R, Y)[3] == 0x40, "FB_ONLY: failing pixel red, alpha stored");
    CHECK(zb[Y * 64 + L] > 0.f, "FB_ONLY: passing pixel wrote Z");
    CHECK(zb[Y * 64 + R] == 0.f, "FB_ONLY: failing pixel left Z untouched");

    // RGB_ONLY: the failing pixel's RGB lands, its alpha and Z do not.
    CHECK(draw(3, 0xC0, 0x40), "RGB_ONLY draw executed");
    CHECK(Px(fb, L, Y)[0] == 255 && Px(fb, L, Y)[3] == 0xC0 && zb[Y * 64 + L] > 0.f, "RGB_ONLY: passing pixel full write");
    CHECK(Px(fb, R, Y)[0] == 255 && Px(fb, R, Y)[1] == 0 && Px(fb, R, Y)[3] == 0x44, "RGB_ONLY: failing pixel RGB only");
    CHECK(zb[Y * 64 + R] == 0.f, "RGB_ONLY: failing pixel left Z untouched");

    // ZB_ONLY: the failing pixel's Z lands, its colour does not.
    CHECK(draw(2, 0xC0, 0x40), "ZB_ONLY draw executed");
    CHECK(Px(fb, L, Y)[0] == 255 && zb[Y * 64 + L] > 0.f, "ZB_ONLY: passing pixel full write");
    CHECK(Px(fb, R, Y)[0] == 0x11 && Px(fb, R, Y)[1] == 0x22 && Px(fb, R, Y)[2] == 0x33 && Px(fb, R, Y)[3] == 0x44,
          "ZB_ONLY: failing pixel colour untouched");
    CHECK(zb[Y * 64 + R] > 0.f, "ZB_ONLY: failing pixel wrote Z");

    // KEEP: the failing pixel writes nothing.
    CHECK(draw(0, 0xC0, 0x40), "KEEP draw executed");
    CHECK(Px(fb, L, Y)[0] == 255 && zb[Y * 64 + L] > 0.f, "KEEP: passing pixel full write");
    CHECK(Px(fb, R, Y)[0] == 0x11 && Px(fb, R, Y)[3] == 0x44 && zb[Y * 64 + R] == 0.f, "KEEP: failing pixel writes nothing");

    // The failing population blends with its own As (the lerp pass of a two-pass lit
    // object: ALPHA A=Cs B=Cd C=As D=Cd, FB_ONLY). Vertex alpha 0x40 = As 0.5:
    // Cv = (Cs - Cd) * 0.5 + Cd over the clear (0x11,0x22,0x33) with Cs red.
    ir.alpha_blend = true;
    ir.raster.alpha = {0, 1, 0, 1, 0};
    CHECK(draw(1, 0xC0, 0x40), "FB_ONLY lerp draw executed");
    {
        const uint8_t* f = Px(fb, R, Y);
        CHECK(Near(f[0], (255 - 0x11) / 2 + 0x11, 2) && Near(f[1], 0x22 / 2, 2) && Near(f[2], 0x33 / 2, 2),
              "FB_ONLY lerp: the failing pixel blends with As = 0.5 (not Cd, not Cs)");
        CHECK(zb[Y * 64 + R] == 0.f, "FB_ONLY lerp: failing pixel left Z untouched");
        const uint8_t* g = Px(fb, L, Y);
        CHECK(g[0] == 255 && g[1] == 0 && g[2] == 0 && zb[Y * 64 + L] > 0.f, "FB_ONLY lerp: the passing pixel (As >= 1.0) replaces and writes Z");
    }
    // RGB_ONLY under ATST NEVER (the static arm) blends with As too.
    ir.raster.test.atst = 0;
    CHECK(draw(3, 0x40, 0x40), "NEVER + RGB_ONLY lerp draw executed");
    {
        const uint8_t* f = Px(fb, R, Y);
        CHECK(Near(f[0], (255 - 0x11) / 2 + 0x11, 2) && f[3] == 0x44 && zb[Y * 64 + R] == 0.f,
              "NEVER + RGB_ONLY lerp: RGB blended with As = 0.5, alpha and Z untouched");
    }
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");
    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_gl_alpha_fail_split: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
