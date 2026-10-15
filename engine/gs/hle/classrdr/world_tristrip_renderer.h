// gs/hle/classrdr/world_tristrip_renderer.h -- WorldTristripRenderer: the world
// geometry class -- the flat untextured DATE strip family (the shadow composite's
// volume strips into the CT16S accumulator, wrap-blended (0,2,2,1) with COLCLAMP
// off) and the skinned shape as its second family.
//
// A replica of the generic lowering: Draw IS DrawPipeline::DrawGeneric for the
// strip family and SkinnedRenderer::DrawAs(kWorld2) for the second, so the pixels
// are the generic path's by construction; the class adds only routing. Both
// shapes are title data (specs). With a stencil-composite renderer attached the
// strip family goes through its steer (the stencil ladder while the composite
// sequence is live on the strip's buffer, the same generic lowering otherwise).
#pragma once

#include <cstdint>

#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"
#include "engine/gs/hle/classrdr/skinned_renderer.h"

namespace ps2r {
namespace gs {

class StencilCompositeRenderer;

class WorldTristripRenderer final : public ClassRenderer {
public:
    // `world2` may be null (no second family).
    WorldTristripRenderer(const DrawShapeSpec& strip, SkinnedRenderer* world2);
    const char* Name() const override { return "world-tristrip"; }
    bool AcceptStrip(const DrawIr& ir) const { return strip_.Matches(ir); }
    bool Accept(const DrawIr& ir) const override;
    int WarmUp(DrawPipeline&) override { return 0; }   // a replica compiles no program of its own
    void Draw(DrawPipeline& pipe, const DrawIr& ir) override;
    uint64_t StripDraws() const { return strip_draws_; }
    void SetStencilComposite(StencilCompositeRenderer* s9) { s9_ = s9; }

private:
    const DrawShapeSpec& strip_;
    SkinnedRenderer* world2_;
    StencilCompositeRenderer* s9_ = nullptr;
    uint64_t strip_draws_ = 0;
};

}  // namespace gs
}  // namespace ps2r
