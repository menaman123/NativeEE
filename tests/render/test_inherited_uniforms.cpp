// tests/render/test_inherited_uniforms.cpp -- Render_InheritedUniforms: a draw whose
// DrawParams::inherited_uniforms names the CT16 quantisation + dither uniforms keeps
// the values the LAST full-set draw wrote (the reference's class-native generic arms,
// skinned_native.inc:1145-1170 vs opengl_backend.cpp:10817-10823): a flat white CT32
// draw after a CT16S draw lands 5-bit quantised (248), the same draw with the full
// set lands 255, and a full-set CT32 draw in between resets the inherited value.
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

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

namespace {

// A flat, untextured, unblended full-target triangle pair of vertex colour `rgb`.
gs::DrawIr FlatIr(uint8_t frame_psm, uint8_t rgb) {
    gs::DrawIr ir;
    ir.prim_type = 3; ir.alpha_blend = false; ir.tex_enabled = false;
    ir.raster.frame.fbw = 64; ir.raster.frame.psm = frame_psm; ir.raster.frame.fbmsk = 0;
    ir.raster.scissor = {0, 0, 63, 63};
    ir.raster.test.zte = true; ir.raster.test.ztst = 1;
    ir.raster.zbuf.zmsk = true; ir.raster.colclamp = true; ir.raster.dthe = 0;
    auto V = [&](float x, float y) {
        gs::VertexRec v{}; v.x = x; v.y = y; v.z = 1000.f; v.q = 1.f; v.r = rgb; v.g = rgb; v.b = rgb; v.a = 128;
        return v;
    };
    ir.verts = {V(0.f, 0.f), V(128.f, 0.f), V(0.f, 128.f)};
    ir.vertex_count = 3;
    return ir;
}

uint8_t Red(render::GlDevice& dev, render::SurfaceId color) {
    std::vector<uint8_t> fb(64 * 64 * 4);
    dev.Readback(color, render::Rect{0, 0, 64, 64}, fb.data(), 64 * 4);
    return fb[(8 * 64 + 8) * 4];
}

}  // namespace

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_inherited_uniforms") || !ctx.Create(win)) {
        std::printf("SKIP: no GL context\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "device init");
    dev.SetWindowSize(64, 64);

    render::SurfaceDesc cd; cd.width = 64; cd.height = 64; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    render::SurfaceDesc dd = cd; dd.format = render::SurfaceFormat::Depth32f; dd.renderable = false;
    const render::SurfaceId color = dev.CreateSurface(cd);
    const render::SurfaceId depth = dev.CreateSurface(dd);
    CHECK(color.id && depth.id, "surfaces created");
    gs::TextureSource ts;
    render::CdCopyState cds;
    render::DrawParams p;
    p.color = color; p.depth = depth; p.render_w = p.render_h = p.target_w = p.target_h = 64; p.scale = 1;
    p.cd_copy = &cds;
    auto draw = [&](const gs::DrawIr& ir, uint32_t inherited) {
        dev.Clear(color, depth, 0x00000000u, 0.f, render::kClearColor | render::kClearDepth);
        p.inherited_uniforms = inherited;
        dev.BeginDrawRun();
        const render::DrawResult r = dev.Draw(ir, ts, p);
        dev.EndDrawRun();
        CHECK(r.drew, "drew");
        return Red(dev, color);
    };
    const uint32_t arm = render::kInheritCt16Quant | render::kInheritDither;

    // (a) the full set: a CT16S frame quantises white to 248, a CT32 frame keeps 255.
    CHECK(draw(FlatIr(0x0A, 255), 0) == 248, "full set, CT16S frame: 5-bit white = 248");
    CHECK(draw(FlatIr(0x00, 255), 0) == 255, "full set, CT32 frame: 255");
    // (b) the inheriting arm after a CT16S full-set draw: the CT32 draw is quantised.
    CHECK(draw(FlatIr(0x0A, 255), 0) == 248, "prime: CT16S full set");
    CHECK(draw(FlatIr(0x00, 255), arm) == 248, "inheriting CT32 draw after a CT16S draw quantises (248)");
    CHECK(draw(FlatIr(0x00, 181), arm) == 176, "inheriting CT32 draw: 181 -> 176 (low three bits cleared)");
    // (c) a full-set CT32 draw resets the inherited value; the arm then observes 0.
    CHECK(draw(FlatIr(0x00, 255), 0) == 255, "full-set CT32 draw writes u_ct16q = 0");
    CHECK(draw(FlatIr(0x00, 255), arm) == 255, "inheriting CT32 draw after a CT32 draw: 255");
    // (d) the arm never writes: a CT16S draw under the arm after a CT32 full set is NOT quantised.
    CHECK(draw(FlatIr(0x0A, 255), arm) == 255, "inheriting CT16S draw after a CT32 draw: unquantised 255");
    // (e) the other bits leave the quantisation set alone.
    CHECK(draw(FlatIr(0x00, 255), render::kInheritBlendAbcd | render::kInheritZsrcMap |
                                  render::kInheritDrawScale | render::kInheritUvNative) == 255,
          "blend / zsrc / scale / uv-native inheritance does not touch u_ct16q");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");

    dev.DestroySurface(color);
    dev.DestroySurface(depth);
    dev.Shutdown();
    std::printf("test_inherited_uniforms: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
