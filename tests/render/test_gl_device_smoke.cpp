// tests/render/test_gl_device_smoke.cpp -- Render_DeviceSmoke: a 64x64 RGBA8 renderable
// surface + Depth32f, Clear to a colour and Readback the bytes; one untextured
// triangle through GlDrawExecutor with a hand-built DrawIr covers the expected
// pixels (and only those) and the DrawResult carries the fused bbox and route
// flags; a PSMCT24 + ZMSK draw leaves colour and depth writable for a raw clear
// (post-draw mask reset); the TEST register's static alpha-test arm (ATST NEVER with
// AFAIL ZB_ONLY / FB_ONLY / KEEP: the Z-clear idiom leaves the colour untouched); the
// ALPHA A = B identity (Cv = D); the exact window depth (z = 16 passes GREATER over a
// 0-cleared Z buffer); an RGBA8 surface created after an R8 release keeps all four
// channels (the pool holds RGBA8 storage only). Exit 77 (SKIP) without a context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <algorithm>
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

static const uint8_t* Px(const std::vector<uint8_t>& fb, int w, int x, int y) {
    return fb.data() + (static_cast<size_t>(y) * w + x) * 4;   // rows bottom-up = PS2 y
}

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_device_smoke") || !ctx.Create(win)) {
        std::printf("test_gl_device_smoke: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());
    const render::Caps caps = dev.QueryCaps();
    CHECK(caps.max_texture_size >= 4096 && caps.reverse_subtract && caps.staging_readback, "caps");

    render::SurfaceDesc cd; cd.width = 64; cd.height = 64; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id != 0 && depth.id != 0, "surfaces created");
    CHECK(dev.DescribeSurface(color).width == 64 && dev.DescribeSurface(color).renderable, "describe");

    // Clear to (0x11,0x22,0x33,0x44) and read back.
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    std::vector<uint8_t> fb(64 * 64 * 4, 0xCC);
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    bool all = true;
    for (int i = 0; i < 64 * 64; i++)
        all = all && fb[i * 4] == 0x11 && fb[i * 4 + 1] == 0x22 && fb[i * 4 + 2] == 0x33 && fb[i * 4 + 3] == 0x44;
    CHECK(all, "clear colour read back on every pixel");
    std::vector<float> zb(64 * 64, 1.f);
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * 4);
    CHECK(zb[0] == 0.f && zb[64 * 64 - 1] == 0.f, "depth cleared to 0.0 (GS far plane)");
    // Owned scene persistence restores depth into its actual attachment. A
    // padded subrectangle must preserve every other pixel and all float bits.
    const float saved_depth[]{0.0f, 0.25f, 0.5f, 0.75f, -9.0f, -9.0f,
                              1.0f, 0x1p-24f, 0x1.fffffep-1f, 0.375f, -9.0f, -9.0f};
    dev.Upload(depth, render::Rect{7, 9, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    bool depth_exact = true;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        const float expected = x >= 7 && x < 11 && y >= 9 && y < 11
            ? saved_depth[(y - 9) * 6 + x - 7] : 0.0f;
        depth_exact = depth_exact && std::memcmp(&expected, &zb[y * 64 + x], sizeof(float)) == 0;
    }
    CHECK(depth_exact, "depth upload preserves float precision, padded stride and untouched rows");
    GLint unpack = -1;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack);
    CHECK(unpack == 0 && glGetError() == GL_NO_ERROR, "depth upload restores unpack state");
    std::fill(zb.begin(), zb.end(), 0.625f);
    dev.Upload(depth, render::Rect{0, 0, 64, 64}, zb.data(), 0);
    std::fill(zb.begin(), zb.end(), 0.f);
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    CHECK(std::all_of(zb.begin(), zb.end(), [](float f) { return f == 0.625f; }),
          "whole depth allocation restores with tightly packed zero stride");
    dev.Upload(depth, render::Rect{-1, 0, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Upload(depth, render::Rect{63, 0, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Upload(depth, render::Rect{0, 0, 4, 2}, saved_depth, 3);
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    CHECK(std::all_of(zb.begin(), zb.end(), [](float f) { return f == 0.625f; }) &&
          glGetError() == GL_NO_ERROR, "invalid depth extent/stride cannot mutate an attachment");
    render::SurfaceDesc scaled_desc = dd;
    scaled_desc.width = 128; scaled_desc.height = 96; scaled_desc.scale = 2;
    const auto scaled_depth = dev.CreateSurface(scaled_desc);
    std::vector<float> scaled_pixels(128 * 96), restored_pixels(scaled_pixels.size());
    for (size_t i = 0; i < scaled_pixels.size(); ++i) scaled_pixels[i] = float(i % 8192) / 8192;
    dev.Upload(scaled_depth, {0, 0, 128, 96}, scaled_pixels.data(), 128 * sizeof(float));
    dev.Readback(scaled_depth, {0, 0, 128, 96}, restored_pixels.data(), 128 * sizeof(float));
    CHECK(std::memcmp(scaled_pixels.data(), restored_pixels.data(), scaled_pixels.size() * sizeof(float)) == 0,
          "scaled depth snapshot uses the full physical attachment without row rescaling");
    dev.DestroySurface(scaled_depth);
    dev.Clear(color, depth, 0, 0.0f, render::kClearDepth);

    // Upload + readback of a plain texture surface.
    render::SurfaceDesc td; td.width = 4; td.height = 4; td.format = render::SurfaceFormat::Rgba8;
    const render::SurfaceId tex = dev.CreateSurface(td);
    std::vector<uint8_t> tp(4 * 4 * 4);
    for (size_t i = 0; i < tp.size(); i++) tp[i] = static_cast<uint8_t>(i);
    dev.Upload(tex, render::Rect{0, 0, 4, 4}, tp.data(), 16);
    std::vector<uint8_t> tr(tp.size(), 0);
    dev.Readback(tex, render::Rect{0, 0, 4, 4}, tr.data(), 16);
    CHECK(tr == tp, "texture upload round-trips");

    // One untextured red triangle covering the lower-left half of the target
    // (PS2 y grows downward on the top-at-GL-bottom convention: row y = GL row y).
    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.alpha_blend = false;
    ir.raster.frame.fbw = 64; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;   // ALWAYS
    ir.raster.colclamp = true;
    auto V = [](float x, float y) { gs::VertexRec v{}; v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f; v.r = 255; v.a = 128; return v; };
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f)};
    ir.vertex_count = 3;
    gs::TextureSource ts;   // enabled = false
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = 64; p.render_h = 64; p.target_w = 64; p.target_h = 64; p.scale = 1;
    p.cd_copy = &cds;
    dev.BeginDrawRun();
    const render::DrawResult r = dev.Draw(ir, ts, p);
    dev.EndDrawRun();
    CHECK(r.drew, "draw executed");
    CHECK(r.bbox_valid && r.bbox_all[0] == 0.f && r.bbox_all[1] == 0.f && r.bbox_all[2] == 64.f && r.bbox_all[3] == 64.f, "fused bbox");
    CHECK(!r.sw_blend_active && !r.needs_rt_copy && !r.route_rt_blend && !r.wrap_route, "untextured unblended route");
    CHECK(r.write_rect.x == 0 && r.write_rect.y == 0 && r.write_rect.w == 64 && r.write_rect.h == 64, "write rect");
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    const uint8_t* in = Px(fb, 64, 8, 8);        // inside: x + y < 64
    const uint8_t* out = Px(fb, 64, 56, 56);     // outside
    // the stored alpha is the GS byte (the stored-alpha convention): 0x80 stays 0x80
    CHECK(in[0] == 255 && in[1] == 0 && in[2] == 0 && in[3] == 0x80, "covered pixel is opaque red, alpha 0x80 as stored");
    CHECK(out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33 && out[3] == 0x44, "uncovered pixel keeps the clear colour");
    int red = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            if (Px(fb, 64, x, y)[0] == 255) red++;
    CHECK(red > 64 * 64 / 2 - 100 && red < 64 * 64 / 2 + 100, "about half the pixels covered");
    std::printf("red pixels: %d\n", red);
    // Depth written: covered pixel at z=1000 -> window 1000/2^32
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(zb[0] > 0.f && zb[0] < 1e-6f, "depth written at the covered pixel");

    // Scissored second draw: green, only rows 0..15 may change.
    ir.raster.scissor = {0, 0, 63, 15};
    for (auto& v : ir.verts) { v.r = 0; v.g = 255; }
    const render::DrawResult r2 = dev.Draw(ir, ts, p);
    CHECK(r2.write_rect.h == 16, "write rect clipped by scissor");
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[1] == 255 && Px(fb, 64, 8, 20)[1] == 0 && Px(fb, 64, 8, 20)[0] == 255, "scissor honoured");

    // Present to the hidden window must not error.
    render::ScanoutDesc sd; sd.source = color; sd.src = {0, 0, 64, 64}; sd.dst = {0, 0, 64, 64};
    dev.Present(sd);
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");

    // Post-draw mask reset: a PSMCT24 draw (alpha mask FALSE) with ZBUF.ZMSK (depth mask
    // FALSE) must leave colour and depth writable for the raw GL pass that follows.
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.frame.psm = 0x01;
    ir.raster.zbuf.zmsk = true;
    for (auto& v : ir.verts) { v.r = 0; v.g = 0; v.b = 255; }
    const render::DrawResult r3 = dev.Draw(ir, ts, p);
    CHECK(r3.drew, "CT24 draw executed");
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[2] == 255 && fb[3] == 0x80, "CT24 draw wrote blue and left alpha untouched");
    dev.BindTarget(color, depth);
    dev.State().Disable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 0x77 / 255.0f);   // raw: no mask calls of its own
    glClearDepth(0.5);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0 && fb[2] == 0 && fb[3] == 0x77, "alpha writable again after the CT24 draw");
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(zb[0] == 0.5f, "depth writable again after the ZMSK draw");

    // The TEST register's static alpha-test arm (GS User's Manual: ATST NEVER fails
    // every pixel, AFAIL names what is still updated). The GS Z-clear idiom -- a
    // frame-covering untextured draw with ATE=1 ATST=NEVER AFAIL=ZB_ONLY, ZTST ALWAYS,
    // blend off -- writes the Z buffer and leaves the frame buffer untouched.
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.frame.psm = 0x00;
    ir.raster.zbuf.zmsk = false;
    ir.raster.test.ate = true; ir.raster.test.atst = 0; ir.raster.test.afail = 2;
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;
    for (auto& v : ir.verts) { v.r = 255; v.g = 255; v.b = 255; v.z = 0.f; }
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f), V(64.f, 0.f), V(64.f, 64.f), V(0.f, 64.f)};
    for (auto& v : ir.verts) { v.r = 255; v.g = 255; v.b = 255; v.z = 0.f; }
    ir.vertex_count = 6;
    const render::DrawResult rz = dev.Draw(ir, ts, p);
    CHECK(rz.drew, "Z-only clear draw executed");
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11 && fb[1] == 0x22 && fb[2] == 0x33 && fb[3] == 0x44, "ATST NEVER + AFAIL ZB_ONLY leaves the colour untouched");
    // ... and FB_ONLY the reverse: colour written, depth not.
    dev.Clear(color, depth, 0x44332211u, 0.25f, render::kClearColor | render::kClearDepth);
    ir.raster.test.afail = 1;
    for (auto& v : ir.verts) v.z = 1000.f;
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(fb[0] == 255 && fb[1] == 255 && zb[0] == 0.25f, "ATST NEVER + AFAIL FB_ONLY writes colour, not depth");
    // ... and KEEP neither (the draw is dropped).
    ir.raster.test.afail = 0;
    const render::DrawResult rk = dev.Draw(ir, ts, p);
    CHECK(!rk.drew, "ATST NEVER + AFAIL KEEP is no draw");
    ir.raster.test.ate = false;

    // ALPHA with A = B: the product term is zero, Cv = D. (2,2,2,1) = Cd keeps the
    // frame colour whatever the vertex colour (alpha is source-replaced).
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.alpha_blend = true;
    ir.raster.alpha = {2, 2, 2, 1, 0};
    for (auto& v : ir.verts) { v.r = 0; v.g = 0x20; v.b = 0; v.a = 0x80; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11 && fb[1] == 0x22 && fb[2] == 0x33, "ALPHA 2221 (Cv = Cd) keeps the frame colour");
    ir.alpha_blend = false;
    ir.raster.alpha = {};

    // The window depth is exact for small z: after a Z buffer cleared to 0 (the
    // GS far plane) a draw at z = 16 with ZTST GREATER passes, as on the console
    // (the NDC representation alone would round 16 / 2^31 - 1 to -1.0 and fail).
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.test.zte = true; ir.raster.test.ztst = 3;
    for (auto& v : ir.verts) { v.r = 0; v.g = 0; v.b = 0; v.a = 0x80; v.z = 16.f; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(fb[0] == 0 && fb[1] == 0 && fb[2] == 0, "z = 16 GREATER a 0-cleared Z passes (black written)");
    CHECK(zb[0] == 16.0f / 4294967296.0f, "window depth is exactly z / 2^32");
    // ... and the same z against itself fails GREATER (exact compare, no drift).
    for (auto& v : ir.verts) { v.r = 255; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0, "z = 16 GREATER z = 16 fails");
    ir.raster.test.ztst = 1;
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f)};
    ir.vertex_count = 3;

    // Pool hygiene: R8 storage is never pooled, so an RGBA8 surface of the same size
    // created after an R8 release keeps all four channels.
    dev.DestroySurface(tex);   // retires the 4x4 RGBA8 storage to the pool
    render::SurfaceDesc r8d = td; r8d.format = render::SurfaceFormat::R8;
    const render::SurfaceId r8 = dev.CreateSurface(r8d);
    CHECK(r8.id != 0, "R8 surface created");
    std::vector<uint8_t> r8p(4 * 4, 0x5A);
    dev.Upload(r8, render::Rect{0, 0, 4, 4}, r8p.data(), 4);
    dev.DestroySurface(r8);
    const render::SurfaceId tex2 = dev.CreateSurface(td);
    // two partial uploads: SubImage into whatever storage the pool handed out
    dev.Upload(tex2, render::Rect{0, 0, 4, 2}, tp.data(), 16);
    dev.Upload(tex2, render::Rect{0, 2, 4, 2}, tp.data() + 2 * 16, 16);
    std::fill(tr.begin(), tr.end(), 0);
    dev.Readback(tex2, render::Rect{0, 0, 4, 4}, tr.data(), 16);
    CHECK(tr == tp, "RGBA8 after R8 release keeps R, G, B and A");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error after pool checks");
    dev.DestroySurface(tex2);
    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_gl_device_smoke: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
