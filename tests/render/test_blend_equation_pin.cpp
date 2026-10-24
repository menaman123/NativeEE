// tests/render/test_blend_equation_pin.cpp -- Render_BlendEquationPin: the GL lowering
// of the GS alpha blend reproduces the PS2 equation
//     Cv = ((A - B) * C >> 7) + D,  A/B/D in {Cs, Cd, 0}, C in {As, Ad, FIX}
// (GS User's Manual 3.6 "Alpha Blending"; COLCLAMP=1 saturates the result to
// [0, 255]) on sample pixels for every ALPHA tuple the title screen draws with:
// 0101 (alpha-over), 0201 (additive Cs*As + Cd), 2001 (Cd - Cs*As), 0122 and 2021
// (FIX-scaled), plus the texture-function chain in front of it: MODULATE with the
// GS colour convention 0x80 = 1.0 (a 0xFF vertex colour brightens the texel 2x,
// clamped) and texel alpha (0x80 = 1.0) feeding As. Each case clears the target to
// Cd, draws one blended full-target quad with Cs/As, reads the pixel back and
// compares to the integer equation within 1 step (GL rounds 8-bit blend factors).
// Exit 77 (SKIP) without a GL context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"
#include "engine/render/backends/gl/gl_draw_executor.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

namespace {

constexpr int kW = 16, kH = 16;

struct Rgb { int r, g, b; };

// The PS2 equation per channel; C is the 0..128 alpha-domain factor.
int Ps2Blend(int a_sel, int b_sel, int d_sel, int cs, int cd, int c) {
    auto sel = [&](int s) { return s == 0 ? cs : (s == 1 ? cd : 0); };
    const int v = (((sel(a_sel) - sel(b_sel)) * c) >> 7) + sel(d_sel);
    return std::max(0, std::min(255, v));
}

// The texture function MODULATE in GS integer space: Cv = Ct * Cf >> 7, clamped.
int Ps2Modulate(int ct, int cf) { return std::min(255, (ct * cf) >> 7); }

gs::DrawIr Quad(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool textured) {
    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.alpha_blend = true;
    ir.raster.frame.fbw = kW;
    ir.raster.frame.psm = 0x00;
    ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, kW - 1, kH - 1};
    ir.raster.test.zte = true;
    ir.raster.test.ztst = 1;   // ALWAYS
    ir.raster.colclamp = true;
    ir.tex_enabled = textured;
    ir.use_fst = true;
    ir.tex.fst = 1;   // UV in 12.4 texel units
    ir.tex.tfx = 0;   // MODULATE
    ir.tex.tcc = 1;   // RGBA
    auto V = [&](float x, float y, uint16_t u, uint16_t v) {
        gs::VertexRec vr{};
        vr.x = x; vr.y = y; vr.z = 1000.f; vr.q = 1.f;
        vr.uv_u = u; vr.uv_v = v;
        vr.r = r; vr.g = g; vr.b = b; vr.a = a;
        return vr;
    };
    const uint16_t uw = static_cast<uint16_t>(kW * 16), uh = static_cast<uint16_t>(kH * 16);
    ir.verts = {V(0.f, 0.f, 0, 0), V(kW, 0.f, uw, 0), V(0.f, kH, 0, uh),
                V(kW, 0.f, uw, 0), V(kW, kH, uw, uh), V(0.f, kH, 0, uh)};
    ir.vertex_count = 6;
    return ir;
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_blend_equation_pin") || !ctx.Create(win)) {
        std::printf("test_blend_equation_pin: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());

    render::SurfaceDesc cd; cd.width = kW; cd.height = kH; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id != 0 && depth.id != 0, "surfaces");

    // A 16x16 texture: constant texel (0x60,0x80,0xA0) with GS alpha 0x80 in the left
    // half and 0x40 in the right. The stored-alpha convention (test_gl_alpha_convention):
    // a texel carries the GS alpha BYTE as the GS stores it; 0x80 = 1.0 is applied by
    // the blend's As factor, not by a rescale at upload.
    render::SurfaceDesc td; td.width = kW; td.height = kH; td.format = render::SurfaceFormat::Rgba8;
    const render::SurfaceId tex = dev.CreateSurface(td);
    std::vector<uint8_t> tp(kW * kH * 4);
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            uint8_t* p = &tp[(static_cast<size_t>(y) * kW + x) * 4];
            p[0] = 0x60; p[1] = 0x80; p[2] = 0xA0;
            p[3] = x < kW / 2 ? 0x80 : 0x40;   // the stored GS alpha byte
        }
    dev.Upload(tex, render::Rect{0, 0, kW, kH}, tp.data(), kW * 4);

    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = kW; p.render_h = kH; p.target_w = kW; p.target_h = kH; p.scale = 1;
    p.cd_copy = &cds;

    std::vector<uint8_t> fb(kW * kH * 4);
    auto px = [&](int x, int y) { return fb.data() + (static_cast<size_t>(y) * kW + x) * 4; };

    struct Case {
        const char* name;
        uint8_t a, b, c, d, fix;
        Rgb cs; uint8_t as;    // vertex colour / alpha (untextured: Cs = vertex colour)
        Rgb cd;                 // clear colour
    };
    // The title's tuples with pixels from its own draws: additive white arcs (As 0x7f)
    // over a dark orb, lavender glow (6a54c0, 7f) over a yellow letter, subtractive
    // white over sky, the tail's FIX-scaled 0122/2021 forms.
    const Case cases[] = {
        {"0101 alpha-over 9999997f over yellow", 0, 1, 0, 1, 0, {0x99, 0x99, 0x99}, 0x7f, {238, 216, 16}},
        {"0201 additive ffffff7f over dark orb", 0, 2, 0, 1, 0, {0xff, 0xff, 0xff}, 0x7f, {20, 10, 40}},
        {"0201 additive 6a54c07f over yellow", 0, 2, 0, 1, 0, {0x6a, 0x54, 0xc0}, 0x7f, {238, 216, 16}},
        {"0201 additive 6a54c020 over yellow", 0, 2, 0, 1, 0, {0x6a, 0x54, 0xc0}, 0x20, {238, 216, 16}},
        {"0201 additive a6a6a67f over water", 0, 2, 0, 1, 0, {0xa6, 0xa6, 0xa6}, 0x7f, {0, 0x6b, 0xff}},
        {"2001 subtractive ffffff7f over sky", 2, 0, 0, 1, 0, {0xff, 0xff, 0xff}, 0x7f, {206, 143, 156}},
        {"2001 subtractive ffffff40 over sky", 2, 0, 0, 1, 0, {0xff, 0xff, 0xff}, 0x40, {206, 143, 156}},
        {"0122 fix=80 (Cs-Cd)*1+Cd", 0, 1, 2, 2, 0x80, {0x10, 0x20, 0x30}, 0x00, {200, 100, 50}},
        {"2021 fix=20 Cd - 0.25*Cs", 2, 0, 2, 1, 0x20, {0x80, 0x80, 0x80}, 0x7f, {238, 216, 16}},
        {"0101 alpha-over As=0 leaves Cd", 0, 1, 0, 1, 0, {0x26, 0x82, 0xff}, 0x00, {238, 216, 16}},
    };
    for (const Case& c : cases) {
        const uint32_t clear = static_cast<uint32_t>(c.cd.r) | (static_cast<uint32_t>(c.cd.g) << 8) |
                               (static_cast<uint32_t>(c.cd.b) << 16) | (0x80u << 24);
        dev.Clear(color, depth, clear, 0.0f, render::kClearColor | render::kClearDepth);
        gs::DrawIr ir = Quad(static_cast<uint8_t>(c.cs.r), static_cast<uint8_t>(c.cs.g),
                             static_cast<uint8_t>(c.cs.b), c.as, false);
        ir.raster.alpha.a = c.a; ir.raster.alpha.b = c.b; ir.raster.alpha.c = c.c; ir.raster.alpha.d = c.d;
        ir.raster.alpha.fix = c.fix;
        gs::TextureSource ts;
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        CHECK(r.drew, c.name);
        dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
        const int cf = (c.c == 2) ? c.fix : c.as;   // untextured: As = vertex alpha
        const int er = Ps2Blend(c.a, c.b, c.d, c.cs.r, c.cd.r, cf);
        const int eg = Ps2Blend(c.a, c.b, c.d, c.cs.g, c.cd.g, cf);
        const int eb = Ps2Blend(c.a, c.b, c.d, c.cs.b, c.cd.b, cf);
        const uint8_t* o = px(kW / 4, kH / 2);
        const bool ok = std::abs(o[0] - er) <= 1 && std::abs(o[1] - eg) <= 1 && std::abs(o[2] - eb) <= 1;
        std::printf("%-40s ps2=(%3d,%3d,%3d) gl=(%3d,%3d,%3d) %s\n", c.name, er, eg, eb, o[0], o[1], o[2],
                    ok ? "ok" : "MISMATCH");
        CHECK(ok, c.name);
        // the quad covers the whole target: a far corner agrees with the sample
        const uint8_t* q = px(kW - 2, kH - 2);
        CHECK(std::abs(q[0] - er) <= 1 && std::abs(q[1] - eg) <= 1 && std::abs(q[2] - eb) <= 1, "corner pixel");
    }

    // Textured MODULATE through the alpha chain: vertex (0xff,0xff,0xff,0x7f) over a
    // texel (0x60,0x80,0xA0) with stored alpha 0x80 (left) / 0x40 (right), additive 0201
    // over black. GS User's Manual 3.4.4 (MODULATE, TCC=1): Cv = Ct * Cf >> 7, Av = At * Af >> 7;
    // 3.6: Cv = (Cs - 0) * As >> 7 + Cd with As = Av (0x80 = 1.0). Left: Cs = (0xBF,0xFF,0xFF),
    // Av = 0x7f*0x80>>7 = 127 -> (189,253,253). Right: Av = 0x7f*0x40>>7 = 63 -> (94,125,125).
    {
        dev.Clear(color, depth, 0x80000000u, 0.0f, render::kClearColor | render::kClearDepth);
        gs::DrawIr ir = Quad(0xff, 0xff, 0xff, 0x7f, true);
        ir.raster.alpha.a = 0; ir.raster.alpha.b = 2; ir.raster.alpha.c = 0; ir.raster.alpha.d = 1;
        gs::TextureSource ts;
        ts.enabled = true;
        ts.owner.kind = gs::TextureSource::OwnerKind::Decoded;
        ts.owner.surface = tex;
        ts.transform.uv_tex_w = kW; ts.transform.uv_tex_h = kH;
        ts.transform.uv_pre_w = kW; ts.transform.uv_pre_h = kH;
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        CHECK(r.drew && !r.sw_blend_active, "textured additive draw on the fixed-function arm");
        dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
        const int cs_r = Ps2Modulate(0x60, 0xff), cs_g = Ps2Modulate(0x80, 0xff), cs_b = Ps2Modulate(0xA0, 0xff);
        const int as_l = (0x7f * 0x80) >> 7, as_r = (0x7f * 0x40) >> 7;
        const uint8_t* l = px(2, kH / 2);
        const uint8_t* rr = px(kW - 3, kH / 2);
        const int el[3] = {Ps2Blend(0, 2, 1, cs_r, 0, as_l), Ps2Blend(0, 2, 1, cs_g, 0, as_l), Ps2Blend(0, 2, 1, cs_b, 0, as_l)};
        const int er[3] = {Ps2Blend(0, 2, 1, cs_r, 0, as_r), Ps2Blend(0, 2, 1, cs_g, 0, as_r), Ps2Blend(0, 2, 1, cs_b, 0, as_r)};
        std::printf("textured MODULATE 0201: left ps2=(%d,%d,%d) gl=(%d,%d,%d)  right ps2=(%d,%d,%d) gl=(%d,%d,%d)\n",
                    el[0], el[1], el[2], l[0], l[1], l[2], er[0], er[1], er[2], rr[0], rr[1], rr[2]);
        CHECK(std::abs(l[0] - el[0]) <= 2 && std::abs(l[1] - el[1]) <= 2 && std::abs(l[2] - el[2]) <= 2,
              "texel alpha 0x80: the 2x vertex brightening reaches the additive sum at full As");
        CHECK(std::abs(rr[0] - er[0]) <= 2 && std::abs(rr[1] - er[1]) <= 2 && std::abs(rr[2] - er[2]) <= 2,
              "texel alpha 0x40: As halves the additive contribution");
    }

    std::printf("test_blend_equation_pin: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
