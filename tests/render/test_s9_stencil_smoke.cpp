// tests/render/test_s9_stencil_smoke.cpp -- Render_S9StencilSmoke: the stencil-composite
// GL primitives on a real context, at the primitive level (no renderer): a D24S8
// surface, ClearStencil, one strip's three passes (StripCount pass 1 through Draw,
// passes 2 + 3 through DrawConstQuad with the exact stencil states), then the 32
// materialise quads -- the covered byte reads 8 * ((base + 1) & 0x1F), the uncovered
// byte 8 * base, alpha is never written, a -8 strip returns the byte to the base, the
// masks are reset for a following generic draw, and a StripCount draw carrying the
// consumed per-frame clear still clears the colour target's depth. Exit 77 (SKIP)
// without a context.
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

const uint8_t* Px(const std::vector<uint8_t>& fb, int w, int x, int y) {
    return fb.data() + (static_cast<size_t>(y) * w + x) * 4;
}

render::StencilState St(render::StencilState::Func f, uint8_t ref, uint8_t cmp, uint8_t wr,
                        render::StencilState::Op op) {
    render::StencilState s;
    s.enabled = true; s.func = f; s.ref = ref; s.cmp_mask = cmp; s.write_mask = wr; s.pass_op = op;
    return s;
}

// The DATE strip: a flat +8 / -8 lower-left triangle over a CT16S frame, GEQUAL
// against a colour-authored Z source, ZMSK, DATE with DATM 0.
gs::DrawIr StripIr(uint8_t rgb) {
    gs::DrawIr ir;
    ir.prim_type = 3; ir.alpha_blend = false;
    ir.raster.frame.fbw = 64; ir.raster.frame.psm = 0x0A; ir.raster.frame.fbmsk = 0xFF000000u;
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.test.zte = true; ir.raster.test.ztst = 2; ir.raster.test.date = true; ir.raster.test.datm = false;
    ir.raster.zbuf.zmsk = true; ir.raster.colclamp = true;
    auto V = [&](float x, float y) {
        gs::VertexRec v{}; v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f; v.r = rgb; v.g = rgb; v.b = rgb; v.a = 128;
        return v;
    };
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f)};
    ir.vertex_count = 3;
    return ir;
}

// The 32 materialise quads: EQUAL v over the low five bits, byte 8v, alpha protected.
void Materialize(render::GlDevice& dev, render::SurfaceId color, render::SurfaceId sten,
                 const render::Rect& rect) {
    for (int v = 0; v < 32; v++) {
        const uint32_t b = static_cast<uint32_t>(8 * v) & 0xFF;
        dev.DrawConstQuad(color, sten, rect, 0xFF000000u | (b << 16) | (b << 8) | b, false,
                          St(render::StencilState::kEqual, static_cast<uint8_t>(v), 0x1F, 0x00,
                             render::StencilState::kKeep));
    }
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_s9_stencil_smoke") || !ctx.Create(win)) {
        std::printf("SKIP: no GL context\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "device init");
    dev.SetWindowSize(64, 64);
    std::printf("GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    render::SurfaceDesc cd; cd.width = 64; cd.height = 64; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    render::SurfaceDesc sd = cd; sd.format = render::SurfaceFormat::DepthStencil24_8; sd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    const render::SurfaceId sten = dev.CreateSurface(sd);
    const render::SurfaceId zsrc = dev.CreateSurface(cd);
    CHECK(color.id && depth.id && sten.id && zsrc.id, "surfaces created");
    CHECK(dev.Surface(sten) && dev.Surface(sten)->rbo != 0, "D24S8 is a renderbuffer");
    dev.Clear(color, depth, 0x00000000u, 1.0f, render::kClearColor | render::kClearDepth);
    dev.Clear(zsrc, render::SurfaceId{}, 0x00000000u, 0.f, render::kClearColor);
    std::vector<uint8_t> fb(64 * 64 * 4);
    const render::Rect full{0, 0, 64, 64};

    // (a) ClearStencil + one const quad under EQUAL base: colour lands only inside the scissor.
    dev.ClearStencil(sten, 0x0F);
    dev.DrawConstQuad(color, sten, render::Rect{8, 8, 16, 16}, 0xFF404040u, false,
                      St(render::StencilState::kEqual, 0x0F, 0x1F, 0, render::StencilState::kKeep));
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 10, 10)[0] == 0x40 && Px(fb, 64, 10, 10)[3] == 0x00, "quad writes rgb inside, alpha protected");
    CHECK(Px(fb, 64, 30, 30)[0] == 0x00, "quad clipped by the scissor");
    dev.DrawConstQuad(color, sten, render::Rect{8, 8, 16, 16}, 0xFF808080u, false,
                      St(render::StencilState::kEqual, 0x01, 0x1F, 0, render::StencilState::kKeep));
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 10, 10)[0] == 0x40, "stencil EQUAL gate rejects a wrong ref");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error (a)");

    // (b) the ladder: pass 1 (StripCount, bit 7 INVERT on first touch), pass 2 (bbox quad,
    // INCR_WRAP the counter where bit 7 is set), pass 3 (bbox quad, bit 7 back), then the
    // materialise -> covered byte = 8 * ((0x0F + 1) & 0x1F) = 0x80, uncovered = 0x78.
    dev.Clear(color, render::SurfaceId{}, 0x00000000u, 0.f, render::kClearColor);
    dev.ClearStencil(sten, 0x0F);
    gs::DrawIr ir = StripIr(8);
    gs::TextureSource ts;
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth; p.render_w = p.render_h = p.target_w = p.target_h = 64; p.scale = 1;
    p.cd_copy = &cds;
    p.program = render::StencilProgram::StripCount; p.stencil = sten;
    p.zsrc = zsrc; p.zsrc_armed = true; p.zsrc_scale = 1;
    p.stencil_state = St(render::StencilState::kEqual, 0x00, 0x80, 0x80, render::StencilState::kInvert);
    dev.BeginDrawRun();
    const render::DrawResult r1 = dev.Draw(ir, ts, p);
    dev.EndDrawRun();
    CHECK(r1.drew && r1.bbox_valid, "pass 1 drew");
    CHECK(r1.fbmsk16 == 0x8000, "the strip pass reports the CT16S A-protect mask (keep mask / no evict downstream)");
    const render::StencilState p2 = St(render::StencilState::kEqual, 0x80, 0x80, 0x1F, render::StencilState::kIncrWrap);
    const render::StencilState p3 = St(render::StencilState::kEqual, 0x80, 0x80, 0x80, render::StencilState::kInvert);
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p2);
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p3);
    Materialize(dev, color, sten, full);
    dev.Readback(color, full, fb.data(), 64 * 4);
    const uint8_t* in = Px(fb, 64, 8, 8);
    const uint8_t* out = Px(fb, 64, 56, 56);
    std::printf("covered=%02x %02x %02x %02x uncovered=%02x\n", in[0], in[1], in[2], in[3], out[0]);
    CHECK(in[0] == 0x80 && in[1] == 0x80 && in[2] == 0x80, "covered byte = 8 * (base + 1) = 0x80 (+8 strip)");
    CHECK(in[3] == 0x00, "alpha never written by the materialise");
    CHECK(out[0] == 8 * 0x0F, "uncovered byte = 8 * base = 0x78");
    // A second +8 strip over the same area counts once more: 0x88.
    dev.BeginDrawRun(); dev.Draw(ir, ts, p); dev.EndDrawRun();
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p2);
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p3);
    Materialize(dev, color, sten, full);
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[0] == 0x88, "second +8 strip -> 0x88 (the counter, not a flag)");
    // Two -8 strips (DECR_WRAP): back to the base 0x78.
    const render::StencilState p2d = St(render::StencilState::kEqual, 0x80, 0x80, 0x1F, render::StencilState::kDecrWrap);
    gs::DrawIr back = StripIr(0xF8);
    for (int i = 0; i < 2; i++) {
        dev.BeginDrawRun(); dev.Draw(back, ts, p); dev.EndDrawRun();
        dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p2d);
        dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p3);
    }
    Materialize(dev, color, sten, full);
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[0] == 0x78, "two -8 strips return the covered byte to 0x78");
    // A materialise scissored to the strip extent leaves bytes outside it untouched.
    dev.Clear(color, render::SurfaceId{}, 0x11111111u, 0.f, render::kClearColor);
    Materialize(dev, color, sten, render::Rect{0, 0, 32, 32});
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[0] == 0x78 && Px(fb, 64, 40, 40)[0] == 0x11, "materialise extent is the scissor");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error (b)");

    // (c) 6E' review fix 1: a StripCount draw carrying the consumed per-frame clear clears
    // the colour target's depth (bound first), then draws into the stencil alone.
    dev.Clear(color, depth, 0x00000000u, 1.0f, render::kClearColor | render::kClearDepth);
    std::vector<float> zb(64 * 64);
    dev.Readback(depth, full, zb.data(), 64 * 4);
    CHECK(zb[0] == 1.0f, "depth primed to 1.0 before the strip");
    dev.ClearStencil(sten, 0x0F);
    p.do_frame_clear = true;
    dev.BeginDrawRun(); const render::DrawResult r2 = dev.Draw(ir, ts, p); dev.EndDrawRun();
    p.do_frame_clear = false;
    dev.Readback(depth, full, zb.data(), 64 * 4);
    CHECK(r2.drew && zb[0] == 0.0f && zb[64 * 64 - 1] == 0.0f, "frame clear on the strip: depth cleared to 0.0 on the colour target");
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[0] == 0x00, "frame clear on a non-scanout target leaves colour alone");
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p2);
    dev.DrawConstQuad(render::SurfaceId{}, sten, full, 0, false, p3);
    Materialize(dev, color, sten, full);
    dev.Readback(color, full, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[0] == 0x80, "the strip still counted after its frame clear");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error (c)");

    // (d) the mask reset after the stencil passes: a plain generic draw still writes colour.
    p.program = render::StencilProgram::None; p.stencil = render::SurfaceId{};
    gs::DrawIr red = StripIr(0);
    for (auto& v : red.verts) { v.r = 255; v.g = 0; v.b = 0; }
    red.raster.frame.psm = 0x00; red.raster.frame.fbmsk = 0;
    dev.BeginDrawRun(); const render::DrawResult r4 = dev.Draw(red, ts, p); dev.EndDrawRun();
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(r4.drew && fb[0] == 255, "generic draw after the ladder writes colour (masks reset)");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error (d)");

    std::printf("test_s9_stencil_smoke: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
