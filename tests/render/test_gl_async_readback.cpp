// tests/render/test_gl_async_readback.cpp -- Render_AsyncReadback: GlDevice::BeginReadback
// queues a colour read into a pack buffer behind a fence and PollReadback hands back the
// same bytes the synchronous Readback does (full surface and a sub-rect, packed and
// strided); a read begun BEFORE a later clear returns the earlier contents (the GPU
// read is ordered at Begin); ReadbackReady is false for an unknown handle and
// PollReadback refuses it; two outstanding reads complete independently in any order;
// pack buffers are reused across Begin/Poll pairs. Exit 77 (SKIP) without a context.
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backends/gl/gl_device.h"

using namespace ps2r;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

int main() {
    platform::Window win;
    platform::GlContext ctx;
    if (!win.Create(true, 64, 64, "test_gl_async_readback") || !ctx.Create(win)) {
        std::printf("test_gl_async_readback: no GL context -> SKIP (77)\n");
        return 77;
    }
    render::GlDevice dev;
    CHECK(dev.Init([&] { win.Swap(); }), "Init");
    dev.SetWindowSize(win.Width(), win.Height());
    CHECK(dev.QueryCaps().staging_readback, "the GL backend's readbacks are staged");

    render::SurfaceDesc cd; cd.width = 64; cd.height = 48; cd.format = render::SurfaceFormat::Rgba8; cd.renderable = true;
    const render::SurfaceId color = dev.CreateSurface(cd);
    CHECK(color.id != 0, "surface created");

    // 1. Full-surface: Begin after a clear, Poll equals the synchronous read.
    dev.Clear(color, render::SurfaceId{}, 0x44332211u, 0.0f, render::kClearColor);
    std::vector<uint8_t> sync_fb(64 * 48 * 4, 0xCC), async_fb(64 * 48 * 4, 0xCC);
    dev.Readback(color, render::Rect{0, 0, 64, 48}, sync_fb.data(), 64 * 4);
    const render::FenceId f1 = dev.BeginReadback(color, render::Rect{0, 0, 64, 48});
    CHECK(f1.id != 0, "Begin returns a handle");
    CHECK(dev.PollReadback(f1, async_fb.data(), 64 * 4), "Poll completes the read");
    CHECK(sync_fb == async_fb, "async bytes == sync bytes (full surface)");
    CHECK(async_fb[0] == 0x11 && async_fb[1] == 0x22 && async_fb[2] == 0x33 && async_fb[3] == 0x44, "the cleared colour");
    CHECK(!dev.ReadbackReady(f1) && !dev.PollReadback(f1, async_fb.data(), 0), "a consumed handle is unknown");

    // 2. Ordering: a read begun BEFORE a clear sees the earlier contents; one begun after sees the new.
    const render::FenceId before = dev.BeginReadback(color, render::Rect{0, 0, 64, 48});
    dev.Clear(color, render::SurfaceId{}, 0xFF0000FFu, 0.0f, render::kClearColor);
    const render::FenceId after = dev.BeginReadback(color, render::Rect{0, 0, 64, 48});
    std::vector<uint8_t> a(64 * 48 * 4), b(64 * 48 * 4);
    CHECK(dev.PollReadback(after, b.data(), 0), "the later read completes first (any order)");
    CHECK(dev.PollReadback(before, a.data(), 0), "the earlier read completes");
    CHECK(a == sync_fb, "the read begun before the clear holds the earlier pixels");
    CHECK(b[0] == 0xFF && b[1] == 0x00 && b[2] == 0x00 && b[3] == 0xFF, "the read begun after the clear holds the new pixels");

    // 3. Sub-rect with a wider destination stride: rows land `stride` apart, the padding untouched.
    std::vector<uint8_t> sub_sync(16 * 8 * 4, 0xEE), sub_async(32 * 8 * 4, 0xEE);
    dev.Readback(color, render::Rect{8, 4, 16, 8}, sub_sync.data(), 16 * 4);
    const render::FenceId f3 = dev.BeginReadback(color, render::Rect{8, 4, 16, 8});
    CHECK(dev.PollReadback(f3, sub_async.data(), 32 * 4), "sub-rect Poll");
    bool rows_ok = true, pad_ok = true;
    for (int r = 0; r < 8; ++r) {
        if (std::memcmp(sub_async.data() + 32 * 4 * r, sub_sync.data() + 16 * 4 * r, 16 * 4) != 0) rows_ok = false;
        for (int k = 16 * 4; k < 32 * 4; ++k) if (sub_async[32 * 4 * r + k] != 0xEE) pad_ok = false;
    }
    CHECK(rows_ok, "sub-rect rows equal the synchronous read");
    CHECK(pad_ok, "the destination's padding bytes are untouched");

    // 4. Many Begin/Poll pairs: the census counts them as readbacks; nothing leaks (the smoke of reuse).
    const uint64_t rb0 = dev.Census()->readbacks;
    for (int i = 0; i < 20; ++i) {
        const render::FenceId f = dev.BeginReadback(color, render::Rect{0, 0, 64, 48});
        CHECK(dev.PollReadback(f, a.data(), 0), "reuse Poll");
    }
    CHECK(dev.Census()->readbacks == rb0 + 20, "each Begin is one readback in the census");
    CHECK(glGetError() == GL_NO_ERROR, "no GL error");

    dev.DestroySurface(color);
    dev.Shutdown();
    std::printf("test_gl_async_readback: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
