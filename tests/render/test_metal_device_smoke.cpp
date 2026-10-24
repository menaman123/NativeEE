// tests/render/test_metal_device_smoke.cpp -- Render_MetalDeviceSmoke: the GL device smoke
// (test_gl_device_smoke.cpp) over MetalDevice -- a 64x64 RGBA8 renderable surface + Depth32f,
// Clear + Readback, depth upload / readback bit-exactness, a plain texture round trip, one
// untextured triangle through the executor covering the expected pixels (the GS coverage
// rule), the scissor, the CT24 alpha mask, the static alpha-test arms (Z-clear idiom), the
// ALPHA A = B identity, the exact window depth (z = 16 GREATER a 0-cleared Z), a present
// with the capture armed (the window image read back, rows bottom-up). Exit 77 (SKIP)
// without a Metal device. macOS only (the target exists under APPLE).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backends/metal/metal_device.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static const uint8_t* Px(const std::vector<uint8_t>& fb, int w, int x, int y) {
    return fb.data() + (static_cast<size_t>(y) * w + x) * 4;   // rows bottom-up = PS2 y
}

int main() {
    render::MetalDevice dev;
    int swaps = 0;
    if (!dev.Init([&] { swaps++; })) {
        std::printf("test_metal_device_smoke: no Metal device -> SKIP (77)\n");
        return 77;
    }
    std::printf("device: %s\n", dev.DeviceName());
    dev.SetWindowSize(64, 64);
    const render::Caps caps = dev.QueryCaps();
    CHECK(caps.max_texture_size >= 4096 && caps.reverse_subtract && caps.copy_pack5551 && caps.alpha_lane_write, "caps");

    render::SurfaceDesc cd; cd.width = 64; cd.height = 64; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id != 0 && depth.id != 0, "surfaces created");
    CHECK(dev.DescribeSurface(color).width == 64 && dev.DescribeSurface(color).renderable, "describe");

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
    const float saved_depth[]{0.0f, 0.25f, 0.5f, 0.75f, -9.0f, -9.0f, 1.0f, 0x1p-24f, 0x1.fffffep-1f, 0.375f, -9.0f, -9.0f};
    dev.Upload(depth, render::Rect{7, 9, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    bool depth_exact = true;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const float expected = x >= 7 && x < 11 && y >= 9 && y < 11 ? saved_depth[(y - 9) * 6 + x - 7] : 0.0f;
            depth_exact = depth_exact && std::memcmp(&expected, &zb[y * 64 + x], sizeof(float)) == 0;
        }
    CHECK(depth_exact, "depth upload preserves float precision, padded stride and untouched rows");
    std::fill(zb.begin(), zb.end(), 0.625f);
    dev.Upload(depth, render::Rect{0, 0, 64, 64}, zb.data(), 0);
    std::fill(zb.begin(), zb.end(), 0.f);
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    CHECK(std::all_of(zb.begin(), zb.end(), [](float f) { return f == 0.625f; }), "whole depth restores with zero stride");
    dev.Upload(depth, render::Rect{-1, 0, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Upload(depth, render::Rect{63, 0, 4, 2}, saved_depth, 6 * sizeof(float));
    dev.Upload(depth, render::Rect{0, 0, 4, 2}, saved_depth, 3);
    dev.Readback(depth, render::Rect{0, 0, 64, 64}, zb.data(), 64 * sizeof(float));
    CHECK(std::all_of(zb.begin(), zb.end(), [](float f) { return f == 0.625f; }), "invalid depth extent/stride cannot mutate");
    dev.Clear(color, depth, 0, 0.0f, render::kClearDepth);

    render::SurfaceDesc td; td.width = 4; td.height = 4; td.format = render::SurfaceFormat::Rgba8;
    const render::SurfaceId tex = dev.CreateSurface(td);
    std::vector<uint8_t> tp(4 * 4 * 4);
    for (size_t i = 0; i < tp.size(); i++) tp[i] = static_cast<uint8_t>(i);
    dev.Upload(tex, render::Rect{0, 0, 4, 4}, tp.data(), 16);
    std::vector<uint8_t> tr(tp.size(), 0);
    dev.Readback(tex, render::Rect{0, 0, 4, 4}, tr.data(), 16);
    CHECK(tr == tp, "texture upload round-trips");

    gs::DrawIr ir;
    ir.prim_type = 3;
    ir.alpha_blend = false;
    ir.raster.frame.fbw = 64; ir.raster.frame.psm = 0x00; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;
    ir.raster.colclamp = true;
    auto V = [](float x, float y) { gs::VertexRec v{}; v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f; v.r = 255; v.a = 128; return v; };
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f)};
    ir.vertex_count = 3;
    gs::TextureSource ts;
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth;
    p.render_w = 64; p.render_h = 64; p.target_w = 64; p.target_h = 64; p.scale = 1;
    p.cd_copy = &cds;
    dev.BeginFrame();
    const render::DrawResult r = dev.Draw(ir, ts, p);
    dev.EndFrame();
    CHECK(r.drew, "draw executed");
    CHECK(r.bbox_valid && r.bbox_all[0] == 0.f && r.bbox_all[1] == 0.f && r.bbox_all[2] == 64.f && r.bbox_all[3] == 64.f, "fused bbox");
    CHECK(!r.sw_blend_active && !r.needs_rt_copy && !r.route_rt_blend && !r.wrap_route, "untextured unblended route");
    CHECK(r.write_rect.x == 0 && r.write_rect.y == 0 && r.write_rect.w == 64 && r.write_rect.h == 64, "write rect");
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    const uint8_t* in = Px(fb, 64, 8, 8);
    const uint8_t* out = Px(fb, 64, 56, 56);
    CHECK(in[0] == 255 && in[1] == 0 && in[2] == 0 && in[3] == 0x80, "covered pixel is opaque red, alpha 0x80 as stored");
    CHECK(out[0] == 0x11 && out[1] == 0x22 && out[2] == 0x33 && out[3] == 0x44, "uncovered pixel keeps the clear colour");
    int red = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            if (Px(fb, 64, x, y)[0] == 255) red++;
    CHECK(red > 64 * 64 / 2 - 100 && red < 64 * 64 / 2 + 100, "about half the pixels covered");
    std::printf("red pixels: %d\n", red);
    // The GS coverage rule: x + y < 64 covered exactly (the diagonal row y: x in [0, 64 - y)).
    bool rule = true;
    for (int y = 0; y < 64 && rule; y++)
        for (int x = 0; x < 64; x++) {
            const bool covered = Px(fb, 64, x, y)[0] == 255;
            if (covered != (x + y < 64)) { rule = false; std::printf("coverage mismatch at %d,%d\n", x, y); break; }
        }
    CHECK(rule, "the GS coverage rule (x0 <= i < x1) on the diagonal");
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(zb[0] > 0.f && zb[0] < 1e-6f, "depth written at the covered pixel");

    ir.raster.scissor = {0, 0, 63, 15};
    for (auto& v : ir.verts) { v.r = 0; v.g = 255; }
    const render::DrawResult r2 = dev.Draw(ir, ts, p);
    CHECK(r2.write_rect.h == 16, "write rect clipped by scissor");
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[1] == 255 && Px(fb, 64, 8, 20)[1] == 0 && Px(fb, 64, 8, 20)[0] == 255, "scissor honoured");

    // Present with the capture armed: the window image (64x64, rows bottom-up) shows the
    // target with PS2 row 0 at the window's top = the LAST row of the image.
    dev.ArmPresentCapture(true);
    render::ScanoutDesc sd; sd.source = color; sd.src = {0, 0, 64, 64}; sd.dst = {0, 0, 64, 64};
    dev.Present(sd);
    std::vector<uint8_t> pres;
    int pw = 0, ph = 0;
    CHECK(dev.ReadPresented(&pres, &pw, &ph) && pw == 64 && ph == 64 && swaps == 1, "presented image read");
    if (pw == 64 && ph == 64) {
        // target row 8 (green rows 0..15) is window row 8 from the top = image row 55.
        const uint8_t* top = Px(pres, 64, 8, 63 - 8);
        const uint8_t* mid = Px(pres, 64, 8, 63 - 20);
        CHECK(top[1] == 255 && top[3] == 255 && mid[0] == 255 && mid[1] == 0, "present rows bottom-up, PS2 top at the window top, alpha forced 1");
    }

    // CT24 (alpha mask off) + ZMSK: blue written, alpha untouched, depth untouched.
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.frame.psm = 0x01;
    ir.raster.zbuf.zmsk = true;
    for (auto& v : ir.verts) { v.r = 0; v.g = 0; v.b = 255; v.z = 5000.f; }
    const render::DrawResult r3 = dev.Draw(ir, ts, p);
    CHECK(r3.drew, "CT24 draw executed");
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[2] == 255 && fb[3] == 0x80, "CT24 draw wrote blue and left alpha untouched");
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(zb[0] == 1000.0f / 4294967296.0f, "ZMSK left the depth at the first draw's z");

    // The static alpha-test arm: ATST NEVER + AFAIL ZB_ONLY writes Z, leaves colour.
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.frame.psm = 0x00;
    ir.raster.zbuf.zmsk = false;
    ir.raster.test.ate = true; ir.raster.test.atst = 0; ir.raster.test.afail = 2;
    ir.verts = {V(0.f, 0.f), V(64.f, 0.f), V(0.f, 64.f), V(64.f, 0.f), V(64.f, 64.f), V(0.f, 64.f)};
    for (auto& v : ir.verts) { v.r = 255; v.g = 255; v.b = 255; v.z = 0.f; }
    ir.vertex_count = 6;
    for (auto& v : ir.verts) v.z = 777.f;
    const render::DrawResult rz = dev.Draw(ir, ts, p);
    CHECK(rz.drew, "Z-only clear draw executed");
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11 && fb[1] == 0x22 && fb[2] == 0x33 && fb[3] == 0x44, "ATST NEVER + AFAIL ZB_ONLY leaves the colour untouched");
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(zb[0] == 777.0f / 4294967296.0f, "ATST NEVER + AFAIL ZB_ONLY writes the depth");
    // ... and a GREATER draw at a lower z over it fails, a higher one passes.
    ir.raster.test.ate = false; ir.raster.test.ztst = 3;
    for (auto& v : ir.verts) { v.z = 500.f; v.r = 0x99; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11, "GREATER against the Z-only draw's depth fails below it");
    for (auto& v : ir.verts) { v.z = 900.f; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x99, "GREATER against the Z-only draw's depth passes above it");
    ir.raster.test.ate = true; ir.raster.test.ztst = 1;
    for (auto& v : ir.verts) { v.r = 255; v.z = 0.f; }
    dev.Clear(color, depth, 0x44332211u, 0.25f, render::kClearColor | render::kClearDepth);
    ir.raster.test.afail = 1;
    for (auto& v : ir.verts) v.z = 1000.f;
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(fb[0] == 255 && fb[1] == 255 && zb[0] == 0.25f, "ATST NEVER + AFAIL FB_ONLY writes colour, not depth");
    ir.raster.test.afail = 0;
    const render::DrawResult rk = dev.Draw(ir, ts, p);
    CHECK(!rk.drew, "ATST NEVER + AFAIL KEEP is no draw");
    ir.raster.test.ate = false;

    // ALPHA 2221 (Cv = Cd) keeps the frame colour; alpha source-replaced.
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.alpha_blend = true;
    ir.raster.alpha = {2, 2, 2, 1, 0};
    for (auto& v : ir.verts) { v.r = 0; v.g = 0x20; v.b = 0; v.a = 0x80; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11 && fb[1] == 0x22 && fb[2] == 0x33 && fb[3] == 0x80, "ALPHA 2221 (Cv = Cd) keeps the frame colour, writes As");
    // The integer equation: (Cs - Cd) * As >> 7 + Cd with As = 0x40 over Cd = 0x22 and Cs = 0x20:
    // (0x20 - 0x22) * 64 >> 7 = floor(-1) = -1 -> 0x21 (the floor of the signed product).
    ir.raster.alpha = {0, 1, 0, 1, 0};
    for (auto& v : ir.verts) { v.r = 0x20; v.g = 0x20; v.b = 0x20; v.a = 0x40; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0x11 + ((0x20 - 0x11) * 64 >> 7) && fb[1] == 0x21 && fb[2] == 0x33 + (((0x20 - 0x33) * 64) >> 7), "integer GS blend equation (floor of the signed product)");
    ir.alpha_blend = false;
    ir.raster.alpha = {};

    // Exact window depth: z = 16 GREATER a 0-cleared Z passes; against itself fails.
    dev.Clear(color, depth, 0x44332211u, 0.0f, render::kClearColor | render::kClearDepth);
    ir.raster.test.zte = true; ir.raster.test.ztst = 3;
    for (auto& v : ir.verts) { v.r = 0; v.g = 0; v.b = 0; v.a = 0x80; v.z = 16.f; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    dev.Readback(depth, render::Rect{8, 8, 1, 1}, zb.data(), 4);
    CHECK(fb[0] == 0 && fb[1] == 0 && fb[2] == 0, "z = 16 GREATER a 0-cleared Z passes (black written)");
    CHECK(zb[0] == 16.0f / 4294967296.0f, "window depth is exactly z / 2^32");
    for (auto& v : ir.verts) { v.r = 255; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0, "z = 16 GREATER z = 16 fails");

    // FBMSK: a 32-bit sub-byte mask merges in the program (G[6:0] only written).
    ir.raster.test.ztst = 1;
    ir.raster.frame.fbmsk = 0xFFFF80FFu;
    for (auto& v : ir.verts) { v.r = 0x55; v.g = 0xFF; v.b = 0x55; v.a = 0x00; }
    dev.Draw(ir, ts, p);
    dev.Readback(color, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[0] == 0 && fb[1] == 0x7F && fb[2] == 0 && fb[3] == 0x80, "FBMSK 0xFFFF80FF writes G[6:0] only");
    ir.raster.frame.fbmsk = 0;

    // CopyRect kCopyPack5551 into a second renderable: 5-bit replication, alpha bit.
    const render::SurfaceId color2 = dev.CreateSurface(cd);
    dev.CopyRect(color, render::Rect{0, 0, 64, 64}, color2, render::Rect{0, 0, 64, 64}, render::Backend::kCopyPack5551);
    dev.Readback(color2, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[1] == 0x7B && fb[3] == 0xFF, "Pack5551: 0x7F -> 0x7B (5-bit replicate), alpha 0x80 -> 0xFF");
    // The plain copy.
    dev.CopyRect(color, render::Rect{0, 0, 64, 64}, color2, render::Rect{0, 0, 64, 64}, render::Backend::kCopyPlain);
    dev.Readback(color2, render::Rect{8, 8, 1, 1}, fb.data(), 4);
    CHECK(fb[1] == 0x7F && fb[3] == 0x80, "plain copy is byte-exact");
    // WriteAlphaLane: alpha from an R8 lane, RGB kept.
    render::SurfaceDesc ld; ld.width = 64; ld.height = 64; ld.format = render::SurfaceFormat::R8;
    const render::SurfaceId lane = dev.CreateSurface(ld);
    std::vector<uint8_t> lp(64 * 64, 0x33);
    dev.Upload(lane, render::Rect{0, 0, 64, 64}, lp.data(), 64);
    dev.WriteAlphaLane(color2, render::Rect{0, 0, 64, 64}, lane, render::Rect{0, 0, 16, 16});
    dev.Readback(color2, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    CHECK(Px(fb, 64, 8, 8)[3] == 0x80 && Px(fb, 64, 40, 40)[3] == 0x33 && Px(fb, 64, 40, 40)[1] == 0x7F, "alpha lane written outside the exclude rect, RGB kept");
    dev.DestroySurface(lane);
    dev.DestroySurface(color2);

    dev.DestroySurface(tex);
    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_metal_device_smoke: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
