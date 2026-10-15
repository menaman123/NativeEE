// tests/lib/fake_backend.h -- FakeBackend: a CPU render::Backend for GL-free
// tests of the layers above the interface. Surfaces are byte arrays (RGBA8 or
// float depth); Upload / Readback / Clear / CopyRect operate on them; Draw
// records the call and draws nothing (tests feed synthetic DrawResults).
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "engine/render/backend.h"

namespace ps2r {
namespace test {

class FakeBackend : public render::Backend {
public:
    struct Surface {
        render::SurfaceDesc desc;
        std::vector<uint8_t> rgba;   // Rgba8 / R8 (4 bytes per texel for simplicity)
        std::vector<float> depth;    // Depth32f
    };

    render::Caps QueryCaps() const override;
    render::SurfaceId CreateSurface(const render::SurfaceDesc& d) override;
    void DestroySurface(render::SurfaceId id) override;
    render::SurfaceDesc DescribeSurface(render::SurfaceId id) const override;
    void Clear(render::SurfaceId color, render::SurfaceId depth, uint32_t rgba, float z,
               uint32_t flags) override;
    void Upload(render::SurfaceId id, const render::Rect& r, const void* bytes, size_t stride) override;
    void UploadLevel(render::SurfaceId, int, int, int, const void*) override;
    void FinishMipChain(render::SurfaceId, int, bool) override;
    render::DrawResult Draw(const gs::DrawIr&, const gs::TextureSource&,
                            const render::DrawParams& p) override;
    void CopyRect(render::SurfaceId src, const render::Rect& sr, render::SurfaceId dst,
                  const render::Rect& dr, uint32_t) override;
    void Readback(render::SurfaceId id, const render::Rect& r, void* out, size_t stride) override;
    render::FenceId BeginReadback(render::SurfaceId, const render::Rect&) override;
    bool PollReadback(render::FenceId, void*, size_t) override;
    void Present(const render::ScanoutDesc& d) override;
    void ClearStencil(render::SurfaceId stencil, uint8_t value) override;
    void DrawConstQuad(render::SurfaceId color, render::SurfaceId stencil, const render::Rect& scissor,
                       uint32_t rgba, bool write_alpha, const render::StencilState& st) override;
    void BeginFrame() override {}
    void EndFrame() override {}

    bool Has(render::SurfaceId id) const { return id.id != 0 && surfaces_.count(id.id) != 0; }
    Surface& At(render::SurfaceId id) { return surfaces_[id.id]; }
    // Write one texel (row y of the attachment == GS row y).
    void SetPixel(render::SurfaceId id, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void SetDepth(render::SurfaceId id, int x, int y, float d);

    int creates = 0, destroys = 0, clears = 0, uploads = 0, readbacks = 0, draws = 0, presents = 0;
    int level_uploads = 0, mip_finishes = 0, last_level = 0, last_level_w = 0, last_level_h = 0;
    int last_mip_authored = 0;
    render::DrawParams last_params;
    render::ScanoutDesc last_present;
    // Stencil-composite recording: every Draw with a stencil program and every
    // const quad, in call order (the tests replay the pass ladder from these).
    struct StencilCall {
        enum Kind : uint8_t { kDraw, kQuad, kClear } kind;
        render::StencilProgram program;
        render::StencilState st;
        render::SurfaceId color, stencil;
        render::Rect scissor;
        uint32_t rgba = 0;
        bool write_alpha = false;
        uint8_t clear_value = 0;
    };
    std::vector<StencilCall> stencil_calls;
    int stencil_clears = 0, const_quads = 0;

private:
    std::map<uint32_t, Surface> surfaces_;
    uint32_t next_ = 0;
};

}  // namespace test
}  // namespace ps2r
