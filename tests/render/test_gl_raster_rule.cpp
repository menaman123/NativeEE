// tests/render/test_gl_raster_rule.cpp -- the GS pixel coverage rule through the GL
// executor (GS User's Manual 3.4 "Drawing Primitive": the GS samples pixel (i, j) at
// the INTEGER point (i, j) of the XYOFFSET-relative primitive space and draws it when
// x0 <= i < x1 and y0 <= j < y1). Pins, with sprite-expanded quads through the generic
// path on a 64 x 256 target:
//   * y in [-0.5, 223.5) covers rows 0..223 exactly (the Z-clear / letterbox idiom:
//     the last row is NOT lost);
//   * y in [0, 224) covers rows 0..223 and not row 224 (integer edges: inclusive low,
//     exclusive high -- a GL edge exactly on a sample point is a driver tie, the rule
//     here must not depend on it);
//   * y in [0.5, 10.5) covers rows 1..10 (distinguishes the integer sample point from
//     a "+0.5 centre" convention, which would give rows 0..9);
//   * x likewise over [-0.5, 31.5) -> columns 0..31 and [0.5, 8.5) -> columns 1..8.
// Exit 77 (SKIP) without a context.
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
#include "engine/render/backends/gl/gl_draw_executor.h"

using namespace ps2r;

namespace {

int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

constexpr int kW = 64, kH = 256;

gs::VertexRec V(float x, float y) {
    gs::VertexRec v{};
    v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f; v.r = 255; v.g = 255; v.b = 255; v.a = 128;
    return v;
}

// A sprite (x0,y0)-(x1,y1) as VertexKick::EmitSprite expands it: tl tr bl, tr br bl.
void Quad(gs::DrawIr& ir, float x0, float y0, float x1, float y1) {
    ir.verts = {V(x0, y0), V(x1, y0), V(x0, y1), V(x1, y0), V(x1, y1), V(x0, y1)};
    ir.vertex_count = 6;
}

// Rows (or columns) whose first pixel of the other axis is white, as a run [lo, hi].
struct Run { int lo = -1, hi = -1, count = 0; };
Run Covered(const std::vector<uint8_t>& fb, bool rows) {
    Run r;
    const int n = rows ? kH : kW;
    for (int i = 0; i < n; ++i) {
        const int x = rows ? 2 : i, y = rows ? i : 2;
        const uint8_t* px = fb.data() + (static_cast<size_t>(y) * kW + x) * 4;
        if (px[0] == 255 && px[1] == 255 && px[2] == 255) {
            if (r.lo < 0) r.lo = i;
            r.hi = i;
            r.count++;
        }
    }
    return r;
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_raster_rule") || !ctx.Create(win)) {
        std::printf("test_gl_raster_rule: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());

    render::SurfaceDesc cd; cd.width = kW; cd.height = kH; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id != 0 && depth.id != 0, "surfaces created");

    gs::DrawIr ir;
    ir.prim_type = 6;
    ir.alpha_blend = false;
    ir.raster.frame.fbw = kW; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, kW - 1, kH - 1};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;   // ALWAYS
    ir.raster.colclamp = true;
    gs::TextureSource ts;   // untextured
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = kW; p.render_h = kH; p.target_w = kW; p.target_h = kH; p.scale = 1;
    p.cd_copy = &cds;
    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH * 4);

    struct Case { float x0, y0, x1, y1; bool rows; int lo, hi; const char* name; };
    const Case cases[] = {
        {-0.5f, -0.5f, 32.f, 223.5f, true, 0, 223, "y [-0.5, 223.5) -> rows 0..223 (the Z-clear / letterbox idiom keeps row 223)"},
        {0.f, 0.f, 32.f, 224.f, true, 0, 223, "y [0, 224) -> rows 0..223 (integer edges: inclusive low, exclusive high)"},
        {0.f, 0.5f, 32.f, 10.5f, true, 1, 10, "y [0.5, 10.5) -> rows 1..10 (the sample point is the integer coordinate)"},
        {0.f, 100.0625f, 32.f, 110.f, true, 101, 109, "y [100 + 1/16, 110) -> rows 101..109 (a 1/16 step past the sample point drops the row)"},
        {-0.5f, 0.f, 31.5f, 16.f, false, 0, 31, "x [-0.5, 31.5) -> columns 0..31"},
        {0.5f, 0.f, 8.5f, 16.f, false, 1, 8, "x [0.5, 8.5) -> columns 1..8"},
        {0.f, 0.f, 32.f, 16.f, false, 0, 31, "x [0, 32) -> columns 0..31"},
    };
    for (const Case& c : cases) {
        dev.Clear(color, depth, 0x00000000u, 0.0f, render::kClearColor | render::kClearDepth);
        Quad(ir, c.x0, c.y0, c.x1, c.y1);
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        CHECK(r.drew, c.name);
        dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
        const Run run = Covered(fb, c.rows);
        const bool ok = run.lo == c.lo && run.hi == c.hi && run.count == c.hi - c.lo + 1;
        CHECK(ok, c.name);
        if (!ok) std::printf("  got %s %d..%d (%d covered), want %d..%d\n", c.rows ? "rows" : "columns", run.lo, run.hi, run.count, c.lo, c.hi);
    }
    // A single pixel: the sprite (3,5)-(4,6) draws exactly pixel (3,5).
    dev.Clear(color, depth, 0x00000000u, 0.0f, render::kClearColor | render::kClearDepth);
    Quad(ir, 3.f, 5.f, 4.f, 6.f);
    dev.BeginDrawRun();
    dev.Draw(ir, ts, p);
    dev.EndDrawRun();
    dev.Readback(color, render::Rect{0, 0, kW, kH}, fb.data(), kW * 4);
    int lit = 0, lit35 = 0;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            if (fb[(static_cast<size_t>(y) * kW + x) * 4] == 255) { lit++; lit35 += (x == 3 && y == 5); }
    CHECK(lit == 1 && lit35 == 1, "the 1x1 sprite (3,5)-(4,6) draws exactly pixel (3,5)");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");

    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_gl_raster_rule: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
