// gs/hle/classrdr/world_tristrip_renderer.cpp -- strip family + second family routing.
#include "engine/gs/hle/classrdr/world_tristrip_renderer.h"

#include "engine/gs/hle/classrdr/draw_pipeline.h"
#include "engine/gs/hle/classrdr/stencil_composite_renderer.h"

namespace ps2r {
namespace gs {

WorldTristripRenderer::WorldTristripRenderer(const DrawShapeSpec& strip, SkinnedRenderer* world2)
    : strip_(strip), world2_(world2) {}

bool WorldTristripRenderer::Accept(const DrawIr& ir) const {
    if (strip_.Matches(ir)) return true;
    return world2_ && world2_->Accept(ir);
}

void WorldTristripRenderer::Draw(DrawPipeline& pipe, const DrawIr& ir) {
    if (strip_.Matches(ir)) {
        if (s9_) s9_->DrawStripOrWorld(pipe, ir);
        else pipe.DrawGeneric(ir);
        strip_draws_++;
        return;
    }
    world2_->DrawAs(pipe, ir, SkinnedRenderer::kWorld2);
}

}  // namespace gs
}  // namespace ps2r
