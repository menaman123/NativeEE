// render/backends/gl/gl_draw_executor.h -- GlDrawExecutor: one DrawIr -> one
// glDrawArrays(GL_TRIANGLES) of the PS2 program.
//
// Draw runs the fixed step order the pixel result depends on: guards, target bind +
// viewport + u_screen_size, per-game-frame clear, texture bind + texture uniforms,
// vertex conversion into the ring, the register uniforms, the 5551 write-mask
// analysis, the blend route (exact fixed-function map for the eight expressible
// RT-read combos, otherwise the shader-side blend over a lazily refreshed copy of
// the destination), depth state, scissor, colour mask, the draw, the colour / depth
// mask reset to their open defaults, and the result record for the coherence tail
// above the backend. Every input comes from DrawIr, TextureSource and DrawParams;
// no VRAM address is consulted here. DrawParams.program selects the stencil-composite
// specialisations instead: StripCount draws the geometry into the stencil-only
// framebuffer under the caller's StencilState (the routed colour image on unit 1 as
// the DATE source, the colour-authored Z on unit 4, colour writes off); Invert runs
// the folded byte-NOT program over a full Cd copy of the target.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/alpha_fail_mask.h"
#include "engine/gs/hle/ir/halfword_shuffle.h"
#include "engine/gs/hle/ir/vertex_rec.h"
#include "engine/render/backend.h"
#include "engine/render/backends/gl/gpu_vertex.h"
#include "engine/render/texture_filter_selection.h"

namespace ps2r {
namespace gs {
struct DrawIr;
struct TextureSource;
}  // namespace gs

namespace render {

class GlDevice;

class GlDrawExecutor {
public:
    explicit GlDrawExecutor(GlDevice& device);
    DrawResult Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p);

    // Pure helpers (unit-testable without a context).
    static uint32_t Fbmsk16From32(uint32_t m);
    static bool Fbmsk16Exact(uint32_t fbmsk16);
    // A 32-bit FRAME.FBMSK with a byte that is neither fully open nor fully masked:
    // glColorMask cannot express it, the shader merges it against the Cd copy.
    static bool Fbmsk32Partial(uint32_t fbmsk);
    static bool RouteRtBlend(const gs::DrawIr& ir, bool using_rt, bool fbmsk16_exact);
    static void SelectFilters(bool mag_linear, uint8_t mmin, uint8_t levels, uint32_t* mag,
                              uint32_t* min);
    // The player's enhancement over an OWNED texture's sampler (Charter 2 A2).
    // `mag`/`min` arrive as SelectFilters chose them from the title's TEX1 and
    // leave as the mode demands: Title keeps them; Point / Bilinear force
    // nearest / linear, mip-aware exactly when the title sampled the chain
    // (`title_mips`) or the hardware LOD is in force; Trilinear forces linear
    // with linear level blending whenever the texture carries a chain
    // (`levels` > 0). Returns whether the draw samples with the HARDWARE LOD
    // (u_tex_lod_hw): anisotropy > 1 or Trilinear over a chain -- the implicit
    // footprint LOD the driver's anisotropic / level-blended fetch needs, in
    // place of the retail per-pixel formula. `anisotropy` is clamped to the
    // driver limit in `*applied_anisotropy` (1 = none).
    static bool EnhanceFilters(TextureFilter filter, int anisotropy, int max_anisotropy, bool title_mips,
                               uint8_t levels, uint32_t* mag, uint32_t* min, int* applied_anisotropy);

private:
    struct Step {
        bool has_texture = false;
        bool using_rt = false;
        bool hw_lod = false;     // the enhancement samples the chain with the hardware LOD
        uint32_t fbo = 0;
        int render_w = 0, render_h = 0, scale = 1;
        size_t tri_verts = 0;
        int first = 0;
        bool alpha_blend = false;
        uint32_t fbmsk16 = 0;
        bool fbmsk16_exact = false;
        bool fbmsk32_exact = false;   // 32-bit frame, sub-byte mask: the shader merge
        bool route_rt_blend = false;
        bool idx_self_serve = false;
        bool wrap_route = false;
        bool needs_rt_copy = false;
        bool sw_blend_active = false;   // the destination copy is bound on unit 1
        bool ff_with_copy = false;      // ... but the blend itself is fixed-function (DATE reads the copy)
        bool reads_cd_rgb = true;       // the copy's RGB is consumed (false: alpha-only read = DATE)
        bool wrote_alpha = true;        // the colour mask included A (conservative default)
        int copy_x = 0, copy_y = 0, copy_w = 0, copy_h = 0;
        bool cd_copied = false;
        Rect cd_rect;
        float bb_tri[4] = {0, 0, 0, 0};
        float bb_all[4] = {0, 0, 0, 0};
        bool bb_valid = false;
        gs::AlphaFailMask::Result afail;   // the alpha test's static write mask for this draw
    };

    DrawResult DrawStripCount(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p);
    DrawResult DrawInvert(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p);
    // The 16-bit column pass over a 32-bit image as one full-extent half-word copy
    // (HalfwordShuffle::Detect accepted the draw); drew = false when the source or the
    // destination copy is unavailable (the caller falls back to the companion route).
    DrawResult DrawHalfwordShuffle(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p,
                                   const gs::HalfwordShuffle::Op& op);
    bool BindTarget(const gs::DrawIr& ir, const DrawParams& p, Step& s);
    void FrameClear(const DrawParams& p);
    void BindTextures(const gs::TextureSource& tex, const DrawParams& p, Step& s);
    void TextureUniforms(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p,
                         const Step& s);
    bool ConvertVertices(const gs::DrawIr& ir, const gs::TextureSource& tex, Step& s);
    void RegisterUniforms(const gs::DrawIr& ir, const DrawParams& p);
    void ClassifyRoutes(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p,
                        Step& s);
    void CdCopy(const DrawParams& p, Step& s);
    void BlendAbcdUniforms(const gs::DrawIr& ir, const DrawParams& p, bool force = false);
    void BlendState(const gs::DrawIr& ir, const DrawParams& p, Step& s);
    void DepthState(const gs::DrawIr& ir, const DrawParams& p, const Step& s);
    void ScissorState(const gs::DrawIr& ir, const Step& s);
    // Returns whether the mask written includes ALPHA.
    bool ColorMaskState(const gs::DrawIr& ir, const Step& s);
    DrawResult MakeResult(const gs::DrawIr& ir, const DrawParams& p, const Step& s) const;

    GlDevice& dev_;
    std::vector<GpuVertex> scratch_;
    std::vector<gs::VertexRec> thin_scratch_;   // PrimExpand output for point / line draws
};

}  // namespace render
}  // namespace ps2r
