// tests/render/test_gl_date_blend.cpp -- Render_DateBlend: a blended draw whose only
// destination-copy reason is DATE (the destination alpha test) blends with ITS OWN ALPHA
// tuple, never the previous draw's, and never twice (GL_BLEND left on from a fixed-
// function draw). The title's shadow-volume counter (ShadowCounterSplit: 2021/FIX 0x80 =
// Cd - 2 and 0221/FIX 0x80 = Cd + 2 in the G byte, FBMSK ffff00ff, DATE=1 DATM=0) is the
// pinned form: its faces self-overlap inside one draw, so the counter must accumulate per
// fragment (the fixed-function arm), the copy serving DATE alone. A tuple without an exact
// fixed-function form (C = Ad) takes the shader arm with its own tuple. The Cd-copy tracker
// is channel-aware: strips that mask alpha do not re-copy for the next DATE read; a draw
// that writes alpha does. Exit 77 (SKIP) without a GL context.
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
#include "engine/render/backends/gl/cd_copy_policy.h"
#include "engine/render/backends/gl/gl_device.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

namespace {

constexpr int W = 64, H = 64;

const uint8_t* Px(const std::vector<uint8_t>& fb, int x, int y) { return fb.data() + (static_cast<size_t>(y) * W + x) * 4; }

gs::VertexRec V(float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    gs::VertexRec v{};
    v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f;
    v.r = r; v.g = g; v.b = b; v.a = a;
    return v;
}

// One quad over [x0, x1) x [0, H).
void Quad(gs::DrawIr& ir, float x0, float x1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ir.verts = {V(x0, 0.f, r, g, b, a), V(x1, 0.f, r, g, b, a), V(x0, (float)H, r, g, b, a),
                V(x1, 0.f, r, g, b, a), V(x1, (float)H, r, g, b, a), V(x0, (float)H, r, g, b, a)};
    ir.vertex_count = 6;
}

// Two quads in ONE draw: [x0, x1) and [x1 - ov, x1 + ov) overlap over [x1 - ov, x1).
void TwoQuadsOverlap(gs::DrawIr& ir, float x0, float x1, float ov, uint8_t g, uint8_t a) {
    Quad(ir, x0, x1, 0, g, 0, a);
    gs::DrawIr tmp;
    Quad(tmp, x1 - ov, x1 + ov, 0, g, 0, a);
    ir.verts.insert(ir.verts.end(), tmp.verts.begin(), tmp.verts.end());
    ir.vertex_count = 12;
}

bool Band(const std::vector<uint8_t>& fb, int x0, int x1, int ch, int want) {
    for (int y = 0; y < H; y += 7)
        for (int x = x0; x < x1; ++x)
            if (Px(fb, x, y)[ch] != want) {
                std::printf("  band x=%d y=%d ch%d = %d want %d\n", x, y, ch, Px(fb, x, y)[ch], want);
                return false;
            }
    return true;
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, W, H, "test_gl_date_blend") || !ctx.Create(win)) {
        std::printf("test_gl_date_blend: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());
    render::SurfaceDesc cd; cd.width = W; cd.height = H; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    render::SurfaceDesc rc = cd; rc.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    const render::SurfaceId rt_copy = dev.CreateSurface(rc);
    CHECK(color.id != 0 && depth.id != 0 && rt_copy.id != 0, "surfaces created");

    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.raster.frame.fbw = W; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, W - 1, H - 1};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;   // ALWAYS
    ir.raster.colclamp = true;
    gs::TextureSource ts;   // untextured: the fragment colour is the vertex colour
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth; p.rt_copy = rt_copy; p.rt_copy_available = true;
    p.render_w = W; p.render_h = H; p.target_w = W; p.target_h = H; p.scale = 1;
    p.cd_copy = &cds;
    std::vector<uint8_t> fb(W * H * 4, 0);
    // The target's post-draw tail, as gs/hle/targets records it: every write into `dirty`,
    // alpha-writing draws into `dirty_alpha` as well.
    auto draw = [&]() {
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        if (r.drew) {
            const render::Rect& w = r.write_rect;
            cds.dirty.Extend(w.x, w.y, w.x + w.w, w.y + w.h);
            if (r.wrote_alpha) cds.dirty_alpha.Extend(w.x, w.y, w.x + w.w, w.y + w.h);
        }
        dev.Readback(color, render::Rect{0, 0, W, H}, fb.data(), W * 4);
        return r;
    };
    auto date = [&](bool on) { ir.raster.test.date = on; ir.raster.test.datm = false; };

    // Counter zero: G = 126, alpha 0 everywhere (DATE DATM=0 passes).
    dev.Clear(color, depth, 0x00007E00u, 0.0f, render::kClearColor | render::kClearDepth);
    dev.Readback(color, render::Rect{0, 0, W, H}, fb.data(), W * 4);
    CHECK(Band(fb, 0, W, 1, 126) && Band(fb, 0, W, 3, 0), "cleared: G 126, A 0");

    // 1. The previous draw: fixed-function 0201 (Cs * As + Cd), G += 2 over x < 16, As = 1.0
    //    (alpha masked: a stored 0x80 would make DATE discard there).
    ir.alpha_blend = true; date(false);
    ir.raster.alpha = {0, 2, 0, 1, 0};
    ir.raster.frame.fbmsk = 0xFF000000u;
    Quad(ir, 0.f, 16.f, 0, 2, 0, 0x80);
    render::DrawResult r = draw();
    CHECK(r.drew && !r.sw_blend_active && !r.needs_rt_copy, "0201 on the fixed-function arm");
    CHECK(Band(fb, 0, 16, 1, 128) && Band(fb, 16, W, 1, 126), "0201: G += 2 on x < 16");

    // 2. The counter's SUB strip: DATE=1, 2021 / FIX 0x80 (Cd - Cs), FBMSK ffff00ff, G = 2.
    //    Own tuple => -2 (the stale 0201 gave +2; a second GL blend gave +128).
    date(true);
    ir.raster.alpha = {2, 0, 2, 1, 0x80};
    ir.raster.frame.fbmsk = 0xFFFF00FFu;
    Quad(ir, 0.f, (float)W, 0, 2, 0, 0x80);
    r = draw();
    CHECK(r.drew && r.needs_rt_copy && !r.sw_blend_active, "DATE sub strip: copy for DATE, blend reported fixed-function");
    CHECK(r.cd_copy_performed, "the first DATE strip copies (alpha never copied before)");
    CHECK(!r.wrote_alpha, "FBMSK ffff00ff: no alpha written");
    CHECK(Band(fb, 0, 16, 1, 126) && Band(fb, 16, W, 1, 124), "2021: G -= 2 everywhere (own tuple, once)");
    CHECK(Band(fb, 0, W, 0, 0) && Band(fb, 0, W, 3, 0), "2021: R and A untouched");

    // 3. The ADD strip with self-overlapping faces in ONE draw: 0221 / FIX 0x80 (Cd + Cs).
    //    Per-fragment accumulation: +4 where the two faces overlap, +2 elsewhere.
    ir.raster.alpha = {0, 2, 2, 1, 0x80};
    TwoQuadsOverlap(ir, 0.f, 32.f, 8.f, 2, 0x80);
    r = draw();
    CHECK(r.drew && r.needs_rt_copy && !r.sw_blend_active, "DATE add strip: fixed-function");
    CHECK(!r.cd_copy_performed, "no alpha written since the last copy: the DATE read skips the copy");
    CHECK(Band(fb, 0, 16, 1, 128) && Band(fb, 16, 24, 1, 126) && Band(fb, 24, 32, 1, 128) &&
          Band(fb, 32, 40, 1, 126) && Band(fb, 40, W, 1, 124),
          "0221 self-overlap: +2 single, +4 on the overlap (per-fragment accumulation)");

    // 4. DATE discards where the destination alpha MSB is set: write A = 0xFF on x >= 48
    //    (unblended, FBMSK 0 -> alpha written -> the alpha-dirty set), then an ADD strip.
    ir.alpha_blend = false; date(false);
    ir.raster.frame.fbmsk = 0;
    Quad(ir, 48.f, (float)W, 0, 124, 0, 0xFF);
    r = draw();
    CHECK(r.drew && r.wrote_alpha, "alpha-writing draw reports wrote_alpha");
    CHECK(!cds.dirty_alpha.Empty(), "the tail recorded an alpha-dirty rect");
    ir.alpha_blend = true; date(true);
    ir.raster.alpha = {0, 2, 2, 1, 0x80};
    ir.raster.frame.fbmsk = 0xFFFF00FFu;
    Quad(ir, 0.f, (float)W, 0, 2, 0, 0x80);
    r = draw();
    CHECK(r.cd_copy_performed, "alpha written since the last copy: the DATE read copies again");
    CHECK(Band(fb, 40, 48, 1, 126) && Band(fb, 48, W, 1, 124), "DATE: +2 where A MSB clear, discarded where set");
    CHECK(Band(fb, 48, W, 3, 0xFF), "the alpha of the discarded band kept");

    // 5. A DATE draw whose tuple has no fixed-function form (C = Ad: 0211) takes the shader
    //    arm with its OWN tuple, GL_BLEND off: Cs * Ad + Cd with Ad = 0x40 (0.5) => G += 1.
    dev.Clear(color, depth, 0x40007E00u, 0.0f, render::kClearColor | render::kClearDepth);
    cds = render::CdCopyState();
    ir.alpha_blend = true; date(false);
    ir.raster.alpha = {0, 2, 0, 1, 0};   // the fixed-function 0201 leaves GL_BLEND enabled
    ir.raster.frame.fbmsk = 0xFF000000u;
    Quad(ir, 0.f, 8.f, 0, 2, 0, 0x80);
    draw();
    date(true);
    ir.raster.alpha = {0, 2, 1, 1, 0};
    ir.raster.frame.fbmsk = 0xFFFF00FFu;
    Quad(ir, 0.f, (float)W, 0, 2, 0, 0x80);
    r = draw();
    CHECK(r.drew && r.needs_rt_copy && r.sw_blend_active, "C = Ad with DATE: the shader arm");
    CHECK(Band(fb, 0, 8, 1, 129) && Band(fb, 8, W, 1, 127), "0211: Cs * Ad + Cd with the draw's own tuple, once");

    // 6. An unblended DATE draw after a fixed-function draw must not inherit GL_BLEND.
    ir.alpha_blend = true; date(false);
    ir.raster.alpha = {0, 2, 0, 1, 0};
    ir.raster.frame.fbmsk = 0xFF000000u;
    Quad(ir, 0.f, 8.f, 0, 2, 0, 0x80);
    draw();
    ir.alpha_blend = false; date(true);
    Quad(ir, 0.f, (float)W, 0, 7, 0, 0x40);
    r = draw();
    CHECK(r.drew && r.needs_rt_copy && !r.sw_blend_active, "unblended DATE draw");
    CHECK(Band(fb, 0, W, 1, 7), "unblended DATE draw replaces (no inherited blend)");

    CHECK(glGetError() == GL_NO_ERROR, "no GL error");
    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.DestroySurface(rt_copy);
    dev.Shutdown();
    std::printf("test_gl_date_blend: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
