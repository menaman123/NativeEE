// tests/render/test_sw_blend_integer.cpp -- Render_SwBlendInteger: the shader-side
// blend arm (the destination-copy route: COLCLAMP=0 wrap, partial FBMSK) computes the
// GS equation in INTEGER arithmetic,
//     Cv = ((A - B) * C >> 7) + D
// (GS User's Manual 3.6.2): the shift drops the product's low 7 bits -- floor of the
// signed product, never round-to-nearest. The pinned pixels are the colour-grade mask
// chain's own: ALPHA 1222 FIX 2 ("G[0] := G[6]": 62 * 2 >> 7 = 0, 64 * 2 >> 7 = 1,
// 190 * 2 >> 7 = 2) under FBMSK 0xFFFFFEFF and under COLCLAMP=0, the negative product
// (0 - 62) * 2 >> 7 = -1 (floor, as PCSX2's software renderer's pmulhw does), and the
// mod-256 wrap of 254 + 3. Exact match, no tolerance (no fixed-function rounding on
// this arm). Exit 77 (SKIP) without a GL context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
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

constexpr int kW = 16, kH = 16;

// The GS equation per channel, C in alpha-byte units; floor division by 128.
int Ps2Blend(int a_sel, int b_sel, int d_sel, int cs, int cd, int c, bool clamp) {
    auto sel = [&](int s) { return s == 0 ? cs : (s == 1 ? cd : 0); };
    const int prod = (sel(a_sel) - sel(b_sel)) * c;
    const int shifted = prod >> 7;   // arithmetic shift: floor
    const int v = shifted + sel(d_sel);
    if (clamp) return v < 0 ? 0 : (v > 255 ? 255 : v);
    return v & 0xFF;
}

gs::DrawIr Quad(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.alpha_blend = true;
    ir.raster.frame.fbw = kW;
    ir.raster.frame.psm = 0x00;
    ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, kW - 1, kH - 1};
    ir.raster.test.zte = true;
    ir.raster.test.ztst = 1;
    ir.raster.colclamp = true;
    auto V = [&](float x, float y) {
        gs::VertexRec vr{};
        vr.x = x; vr.y = y; vr.z = 1000.f; vr.q = 1.f;
        vr.r = r; vr.g = g; vr.b = b; vr.a = a;
        return vr;
    };
    ir.verts = {V(0.f, 0.f), V(kW, 0.f), V(0.f, kH), V(kW, 0.f), V(kW, kH), V(0.f, kH)};
    ir.vertex_count = 6;
    return ir;
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_sw_blend_integer") || !ctx.Create(win)) {
        std::printf("test_sw_blend_integer: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());

    render::SurfaceDesc cd; cd.width = kW; cd.height = kH; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    render::SurfaceDesc rc = cd; rc.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    const render::SurfaceId copy = dev.CreateSurface(rc);
    CHECK(color.id != 0 && depth.id != 0 && copy.id != 0, "surfaces");

    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth; p.rt_copy = copy; p.rt_copy_available = true;
    p.render_w = kW; p.render_h = kH; p.target_w = kW; p.target_h = kH; p.scale = 1;
    p.cd_copy = &cds;

    struct Case {
        const char* name;
        uint8_t a, b, c, d, fix;
        uint8_t cs_r, cs_g, cs_b, as;
        uint8_t cd_r, cd_g, cd_b;
        bool colclamp;
        uint32_t fbmsk;
    };
    const Case cases[] = {
        // The mask chain's bit-6 read: Cd * 2 >> 7 into G[0] under FBMSK 0xFFFFFEFF (G[0] open).
        {"1222 fix=2 fbmsk fffffeff Cd.g=62 -> G[0]=0", 1, 2, 2, 2, 2, 0, 0, 0, 0x80, 62, 62, 62, true, 0xFFFFFEFFu},
        {"1222 fix=2 fbmsk fffffeff Cd.g=64 -> G[0]=1", 1, 2, 2, 2, 2, 0, 0, 0, 0x80, 64, 64, 64, true, 0xFFFFFEFFu},
        {"1222 fix=2 fbmsk fffffeff Cd.g=191 -> G[0]=0", 1, 2, 2, 2, 2, 0, 0, 0, 0x80, 191, 191, 191, true, 0xFFFFFEFFu},
        // The same equation on the wrap arm (COLCLAMP=0), whole channels.
        {"1222 fix=2 wrap Cd=62 -> 0", 1, 2, 2, 2, 2, 0, 0, 0, 0x80, 62, 62, 62, false, 0},
        {"1222 fix=2 wrap Cd=190 -> 2", 1, 2, 2, 2, 2, 0, 0, 0, 0x80, 190, 190, 190, false, 0},
        // Negative product floors: (0 - 62) * 2 >> 7 = -1; + Cd 10 = 9.
        {"2021 fix=2 wrap Cs=62 Cd=10 -> 9", 2, 0, 2, 1, 2, 62, 62, 62, 0x80, 10, 10, 10, false, 0},
        // FIX = 0x80 is exact: Cd - Cs.
        {"2021 fix=80 wrap Cs=2 Cd=126 -> 124", 2, 0, 2, 1, 0x80, 2, 2, 2, 0x80, 126, 126, 126, false, 0},
        // The wrap: 254 + 3 = 257 -> 1.
        {"0221 fix=80 wrap Cs=3 Cd=254 -> 1", 0, 2, 2, 1, 0x80, 3, 3, 3, 0x80, 254, 254, 254, false, 0},
        // As as the factor: (Cs - Cd) * As >> 7 + Cd with As = 0x7f: (200 - 100) * 127 >> 7 = 99 -> 199.
        {"0101 As=7f wrap Cs=200 Cd=100 -> 199", 0, 1, 0, 1, 0, 200, 200, 200, 0x7f, 100, 100, 100, false, 0},
        {"0101 As=1 wrap Cs=255 Cd=0 -> 1", 0, 1, 0, 1, 0, 255, 255, 255, 0x01, 0, 0, 0, false, 0},
    };
    std::vector<uint8_t> fb(kW * kH * 4);
    for (const Case& c : cases) {
        const uint32_t clear = static_cast<uint32_t>(c.cd_r) | (static_cast<uint32_t>(c.cd_g) << 8) |
                               (static_cast<uint32_t>(c.cd_b) << 16) | (0x80u << 24);
        dev.Clear(color, depth, clear, 0.0f, render::kClearColor | render::kClearDepth);
        cds = render::CdCopyState();
        gs::DrawIr ir = Quad(c.cs_r, c.cs_g, c.cs_b, c.as);
        ir.raster.alpha.a = c.a; ir.raster.alpha.b = c.b; ir.raster.alpha.c = c.c; ir.raster.alpha.d = c.d;
        ir.raster.alpha.fix = c.fix;
        ir.raster.colclamp = c.colclamp;
        ir.raster.frame.fbmsk = c.fbmsk;
        gs::TextureSource ts;
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        CHECK(r.drew && r.sw_blend_active, "the shader blend arm");
        dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
        const int cf = (c.c == 2) ? c.fix : c.as;
        int e[3] = {Ps2Blend(c.a, c.b, c.d, c.cs_r, c.cd_r, cf, c.colclamp),
                    Ps2Blend(c.a, c.b, c.d, c.cs_g, c.cd_g, cf, c.colclamp),
                    Ps2Blend(c.a, c.b, c.d, c.cs_b, c.cd_b, cf, c.colclamp)};
        const int cdv[3] = {c.cd_r, c.cd_g, c.cd_b};
        for (int i = 0; i < 3; ++i) {
            const uint32_t m = (c.fbmsk >> (8 * i)) & 0xFFu;
            e[i] = static_cast<int>((static_cast<uint32_t>(e[i]) & ~m) | (static_cast<uint32_t>(cdv[i]) & m));
        }
        const uint8_t* o = fb.data() + (static_cast<size_t>(kH / 2) * kW + kW / 2) * 4;
        const bool ok = o[0] == e[0] && o[1] == e[1] && o[2] == e[2];
        std::printf("%-46s ps2=(%3d,%3d,%3d) gl=(%3d,%3d,%3d) %s\n", c.name, e[0], e[1], e[2], o[0], o[1], o[2],
                    ok ? "ok" : "MISMATCH");
        CHECK(ok, c.name);
    }
    dev.DestroySurface(copy);
    dev.DestroySurface(depth);
    dev.DestroySurface(color);
    std::printf("test_sw_blend_integer: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
