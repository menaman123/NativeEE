// gs/hle/classrdr/class_renderer.h -- ClassRenderer: one native renderer per draw
// class, dispatched by ClassRegistry.
//
// Contract: Accept is a PURE predicate over the DrawIr (no GPU work, no state);
// Draw runs only after Accept said yes and renders the ENTIRE draw (all-or-nothing
// -- a renderer never does partial work and then falls back); WarmUp compiles the
// renderer's programs at init and returns how many. A renderer that declines
// leaves the draw to the caller's generic path, which is the untouched pipeline.
#pragma once

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class DrawPipeline;

class ClassRenderer {
public:
    // The register-program uniforms each reference native generic arm leaves as the
    // previous full-set draw wrote them (render::InheritedUniform; the arms are
    // DrawBatch replications minus these SetU* sites). The world-strip and the
    // stencil-composite natives run their own programs in the reference and lower
    // here to the full set (0).
    static constexpr uint32_t kInheritCommon =
        render::kInheritCt16Quant | render::kInheritDither | render::kInheritDrawScale |
        render::kInheritUvNative;
    // skinned_native.inc DrawSkinnedLitNative (skinned / world family 2 / raw-skin),
    // fmv_blit_native.inc DrawFmvBlitNative.
    static constexpr uint32_t kInheritSkinnedArm =
        kInheritCommon | render::kInheritBlendAbcd | render::kInheritZsrcMap;
    static constexpr uint32_t kInheritFmvBlitArm = kInheritSkinnedArm;
    // sprite_hud_native.inc DrawSpriteHudNative, particles_native.inc DrawParticlesNative.
    static constexpr uint32_t kInheritSpriteHudArm = kInheritCommon | render::kInheritZsrcMap;
    static constexpr uint32_t kInheritParticlesArm = kInheritSpriteHudArm;

    virtual ~ClassRenderer() = default;
    virtual const char* Name() const = 0;
    virtual bool Accept(const DrawIr& ir) const = 0;
    virtual void Draw(DrawPipeline& pipe, const DrawIr& ir) = 0;
    virtual int WarmUp(DrawPipeline& pipe) = 0;
};

}  // namespace gs
}  // namespace ps2r
