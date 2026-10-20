// render/backends/metal/metal_draw_executor.h -- MetalDrawExecutor: one DrawIr -> one
// drawPrimitives of the PS2 register program on the Metal device.
//
// The GL arm's step order (gl_draw_executor.h) with the destination read replaced by the
// framebuffer fetch: target pass (+ the per-game-frame clear as the pass's load action),
// texture / sampler selection, vertex conversion into the ring, the register uniforms
// (one block, persistent across draws so a class-native generic arm inherits what the last
// full-set draw wrote), the 5551 / 32-bit write-mask analysis, the blend route (EVERY
// blended draw, DATE, exact mask and FBA runs the shader's integer GS equation on the
// attachment's current value -- no copy, no fixed-function arm), depth state, scissor, the
// colour mask (applied in the program), the draw, the alpha-fail population split as two
// passes. The copy texture is refreshed (a blit) only for the UV-addressed self read (idx
// fmt 4, u_idx_self), which reads other pixels than the fragment's own. The
// stencil-composite specialisations (StripCount / Invert) are not implemented on this arm
// (no draw; recorded).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/vertex_rec.h"
#include "engine/gs/hle/ir/alpha_fail_mask.h"
#include "engine/gs/hle/ir/halfword_shuffle.h"
#include "engine/render/backend.h"
#include "engine/render/backends/gl/gpu_vertex.h"
#include "engine/render/backends/metal/metal_pipeline_cache.h"
#include "engine/render/backends/metal/metal_shader_sources.h"

namespace ps2r {
namespace gs {
struct DrawIr;
struct TextureSource;
}  // namespace gs

namespace render {

class MetalDevice;
struct MetalSurface;

class MetalDrawExecutor {
public:
    explicit MetalDrawExecutor(MetalDevice& device);
    DrawResult Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p);
    // The device ended the render encoder: every per-encoder shadow is stale.
    void OnEncoderEnded();

private:
    struct Step {
        bool has_texture = false;
        bool using_rt = false;
        bool hw_lod = false;
        bool depth_owner = false;    // the bound texture is a Depth32f surface (texture slot 5)
        void* encoder = nullptr;
        SurfaceId color_id, depth_id;   // the pass's attachments (re-opened after a blit)
        int render_w = 0, render_h = 0, scale = 1;
        size_t tri_verts = 0;
        int first = 0;
        bool alpha_blend = false;
        uint32_t fbmsk16 = 0;
        bool fbmsk16_exact = false;
        bool fbmsk32_exact = false;
        bool idx_self_serve = false;
        bool wrap_route = false;
        bool needs_rt_copy = false;
        bool sw_blend_active = false;
        bool cd_copied = false;
        Rect cd_rect;
        float bb_tri[4] = {0, 0, 0, 0};
        float bb_all[4] = {0, 0, 0, 0};
        bool bb_valid = false;
        gs::AlphaFailMask::Result afail;
        MetalSamplerKey sampler;
        void* tex0 = nullptr;        // id<MTLTexture> on the slots (null = the dummy)
        void* tex1 = nullptr;
        void* tex2 = nullptr;
        void* tex3 = nullptr;
        void* tex4 = nullptr;
        void* tex5 = nullptr;
        MetalDepthKey depth;
    };

    DrawResult DrawHalfwordShuffle(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p,
                                   const gs::HalfwordShuffle::Op& op);
    bool BindTarget(const gs::DrawIr& ir, const DrawParams& p, Step& s);
    void BindTextures(const gs::TextureSource& tex, const DrawParams& p, Step& s);
    void TextureUniforms(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p, const Step& s);
    bool ConvertVertices(const gs::DrawIr& ir, const gs::TextureSource& tex, Step& s);
    void RegisterUniforms(const gs::DrawIr& ir, const DrawParams& p);
    void ClassifyRoutes(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& p, Step& s);
    void CdCopy(const DrawParams& p, Step& s);
    void BlendState(const gs::DrawIr& ir, const DrawParams& p, Step& s);
    void DepthState(const gs::DrawIr& ir, const DrawParams& p, Step& s);
    void ColorMaskState(const gs::DrawIr& ir, const Step& s);
    bool ApplyScissor(const gs::DrawIr& ir, const Step& s);
    void Emit(const gs::DrawIr& ir, Step& s);   // binds the encoder state and issues the draw
    MetalSpecKey SpecKey() const;
    DrawResult MakeResult(const gs::DrawIr& ir, const DrawParams& p, const Step& s) const;

    MetalDevice& dev_;
    std::vector<GpuVertex> scratch_;
    std::vector<gs::VertexRec> thin_scratch_;   // PrimExpand output for point / line draws
    MetalFragUniforms u_;          // persistent (the GL uniform cache's semantics)
    // Per-encoder shadows.
    void* enc_shadow_ = nullptr;
    void* bound_pipeline_ = nullptr;
    void* bound_depth_state_ = nullptr;
    void* bound_tex_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    void* bound_sampler_ = nullptr;
    bool vertex_bound_ = false;
    int vp_w_ = -1, vp_h_ = -1;
    int sc_[4] = {-1, -1, -1, -1};
};

}  // namespace render
}  // namespace ps2r
