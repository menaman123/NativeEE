// tests/lib/fake_backend.cpp -- the CPU Backend bodies.
#include "tests/lib/fake_backend.h"

#include <algorithm>
#include <cmath>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace test {

render::Caps FakeBackend::QueryCaps() const { return render::Caps{4096, false, true, true, false, false}; }

render::SurfaceId FakeBackend::CreateSurface(const render::SurfaceDesc& d) {
    Surface s;
    s.desc = d;
    const size_t n = static_cast<size_t>(d.width) * d.height;
    if (d.format == render::SurfaceFormat::Depth32f) s.depth.assign(n, 0.0f);
    else s.rgba.assign(n * 4, 0);
    surfaces_[++next_] = s;
    ++creates;
    return render::SurfaceId{next_};
}

void FakeBackend::DestroySurface(render::SurfaceId id) {
    surfaces_.erase(id.id);
    ++destroys;
}

render::SurfaceDesc FakeBackend::DescribeSurface(render::SurfaceId id) const {
    auto it = surfaces_.find(id.id);
    return it == surfaces_.end() ? render::SurfaceDesc{} : it->second.desc;
}

void FakeBackend::Clear(render::SurfaceId color, render::SurfaceId depth, uint32_t rgba, float z,
                        uint32_t flags) {
    if ((flags & render::kClearColor) && Has(color)) {
        Surface& s = surfaces_[color.id];
        for (size_t i = 0; i + 3 < s.rgba.size(); i += 4) {
            s.rgba[i] = uint8_t(rgba); s.rgba[i + 1] = uint8_t(rgba >> 8);
            s.rgba[i + 2] = uint8_t(rgba >> 16); s.rgba[i + 3] = uint8_t(rgba >> 24);
        }
    }
    if ((flags & render::kClearDepth) && Has(depth))
        for (float& d : surfaces_[depth.id].depth) d = z;
    ++clears;
}

void FakeBackend::Upload(render::SurfaceId id, const render::Rect& r, const void* bytes, size_t stride) {
    if (!Has(id)) return;
    Surface& s = surfaces_[id.id];
    const uint8_t* src = static_cast<const uint8_t*>(bytes);
    if (!stride) stride = size_t(r.w) * 4;
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            const auto index = size_t(r.y + y) * s.desc.width + size_t(r.x + x);
            void* pixel = s.desc.format == render::SurfaceFormat::Depth32f
                ? static_cast<void*>(&s.depth[index]) : static_cast<void*>(&s.rgba[index * 4]);
            std::memcpy(pixel, src + size_t(y) * stride + size_t(x) * 4, 4);
        }
    ++uploads;
}

void FakeBackend::UploadLevel(render::SurfaceId id, int level, int w, int h, const void*) {
    ++level_uploads;
    last_level = level;
    last_level_w = w;
    last_level_h = h;
    (void)id;
}
void FakeBackend::FinishMipChain(render::SurfaceId, int authored, bool) {
    ++mip_finishes;
    last_mip_authored = authored;
}

// Records the call and reports the draw's geometry (fused bbox over the
// triangle-complete vertices, the write rect = bbox ∩ scissor ∩ target) so the
// coherence tail above sees a plausible result; no pixel is rasterised.
render::DrawResult FakeBackend::Draw(const gs::DrawIr& ir, const gs::TextureSource&,
                                     const render::DrawParams& p) {
    last_params = p;
    ++draws;
    if (p.program != render::StencilProgram::None) {
        StencilCall c{StencilCall::kDraw, p.program, p.stencil_state, p.color, p.stencil, render::Rect{}, 0, false, 0};
        stencil_calls.push_back(c);
    }
    render::DrawResult r;
    const size_t n = (ir.verts.size() / 3) * 3;
    if (n < 3) return r;
    r.drew = true;
    r.alt16_route = p.alt16_route;
    // FRAME.FBMSK folded to the 5551 layout for a 16-bit frame (the contract every
    // program shares -- the coherence tail keys its keep mask and evictions on it).
    const uint8_t fpsm = ir.raster.frame.psm;
    const uint32_t m = ir.raster.frame.fbmsk;
    if ((fpsm == 0x02 || fpsm == 0x0A) && m != 0)
        r.fbmsk16 = ((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) | ((m >> 16) & 0x8000);
    float x0 = ir.verts[0].x, y0 = ir.verts[0].y, x1 = x0, y1 = y0;
    for (size_t i = 1; i < n; ++i) {
        x0 = std::min(x0, ir.verts[i].x); x1 = std::max(x1, ir.verts[i].x);
        y0 = std::min(y0, ir.verts[i].y); y1 = std::max(y1, ir.verts[i].y);
    }
    r.bbox_valid = true;
    r.bbox_tri[0] = r.bbox_all[0] = x0; r.bbox_tri[1] = r.bbox_all[1] = y0;
    r.bbox_tri[2] = r.bbox_all[2] = x1; r.bbox_tri[3] = r.bbox_all[3] = y1;
    const int wx0 = std::max(static_cast<int>(std::floor(x0)), static_cast<int>(ir.raster.scissor.x0));
    const int wy0 = std::max(static_cast<int>(std::floor(y0)), static_cast<int>(ir.raster.scissor.y0));
    const int wx1 = std::min({static_cast<int>(std::ceil(x1)), static_cast<int>(ir.raster.scissor.x1) + 1, p.render_w});
    const int wy1 = std::min({static_cast<int>(std::ceil(y1)), static_cast<int>(ir.raster.scissor.y1) + 1, p.render_h});
    if (wx1 > wx0 && wy1 > wy0) r.write_rect = render::Rect{wx0, wy0, wx1 - wx0, wy1 - wy0};
    return r;
}

void FakeBackend::ClearStencil(render::SurfaceId stencil, uint8_t value) {
    ++stencil_clears;
    StencilCall c{StencilCall::kClear, render::StencilProgram::None, render::StencilState{}, render::SurfaceId{},
                  stencil, render::Rect{}, 0, false, value};
    stencil_calls.push_back(c);
}

// Records the quad; when a colour surface is given the constant colour is written
// over the scissor rect (the stencil test is not modelled: every texel passes), so
// a materialise over a FakeBackend target leaves its last quad's bytes there.
void FakeBackend::DrawConstQuad(render::SurfaceId color, render::SurfaceId stencil, const render::Rect& scissor,
                                uint32_t rgba, bool write_alpha, const render::StencilState& st) {
    ++const_quads;
    StencilCall c{StencilCall::kQuad, render::StencilProgram::None, st, color, stencil, scissor, rgba, write_alpha, 0};
    stencil_calls.push_back(c);
    if (!Has(color)) return;
    Surface& s = surfaces_[color.id];
    for (int y = std::max(0, scissor.y); y < std::min<int>(scissor.y + scissor.h, int(s.desc.height)); ++y)
        for (int x = std::max(0, scissor.x); x < std::min<int>(scissor.x + scissor.w, int(s.desc.width)); ++x) {
            const size_t i = (size_t(y) * s.desc.width + size_t(x)) * 4;
            s.rgba[i] = uint8_t(rgba); s.rgba[i + 1] = uint8_t(rgba >> 8); s.rgba[i + 2] = uint8_t(rgba >> 16);
            if (write_alpha) s.rgba[i + 3] = uint8_t(rgba >> 24);
        }
}

void FakeBackend::CopyRect(render::SurfaceId src, const render::Rect& sr, render::SurfaceId dst,
                           const render::Rect& dr, uint32_t) {
    if (!Has(src) || !Has(dst)) return;
    const Surface& a = surfaces_[src.id];
    Surface& b = surfaces_[dst.id];
    for (int y = 0; y < sr.h && y < dr.h; ++y)
        for (int x = 0; x < sr.w && x < dr.w; ++x)
            std::memcpy(&b.rgba[(size_t(dr.y + y) * b.desc.width + size_t(dr.x + x)) * 4],
                        &a.rgba[(size_t(sr.y + y) * a.desc.width + size_t(sr.x + x)) * 4], 4);
}

void FakeBackend::Readback(render::SurfaceId id, const render::Rect& r, void* out, size_t stride) {
    if (!Has(id)) return;
    const Surface& s = surfaces_[id.id];
    uint8_t* dst = static_cast<uint8_t*>(out);
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            const size_t i = size_t(r.y + y) * s.desc.width + size_t(r.x + x);
            if (s.desc.format == render::SurfaceFormat::Depth32f)
                std::memcpy(dst + size_t(y) * stride + size_t(x) * 4, &s.depth[i], 4);
            else
                std::memcpy(dst + size_t(y) * stride + size_t(x) * 4, &s.rgba[i * 4], 4);
        }
    }
    ++readbacks;
}

render::FenceId FakeBackend::BeginReadback(render::SurfaceId, const render::Rect&) { return {}; }
bool FakeBackend::PollReadback(render::FenceId, void*, size_t) { return true; }

void FakeBackend::Present(const render::ScanoutDesc& d) {
    last_present = d;
    ++presents;
}

void FakeBackend::SetPixel(render::SurfaceId id, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Surface& s = surfaces_[id.id];
    uint8_t* p = &s.rgba[(size_t(y) * s.desc.width + size_t(x)) * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

void FakeBackend::SetDepth(render::SurfaceId id, int x, int y, float d) {
    Surface& s = surfaces_[id.id];
    s.depth[size_t(y) * s.desc.width + size_t(x)] = d;
}

}  // namespace test
}  // namespace ps2r
