// gs/hle/classrdr/skinned_renderer.cpp -- the skinned shape through the generic path.
#include "engine/gs/hle/classrdr/skinned_renderer.h"

#include "engine/gs/hle/classrdr/draw_pipeline.h"

namespace ps2r {
namespace gs {

void SkinnedRenderer::Stats::Emit(std::FILE* f) const {
    if (!f || native_spec + native_generic == 0) return;
    std::fprintf(f,
                 "[GS-SKINRDR] native=%llu spec=%llu generic=%llu skin=%llu"
                 " world2=%llu raw=%llu rt_serve=%llu\n",
                 (unsigned long long)(native_spec + native_generic), (unsigned long long)native_spec,
                 (unsigned long long)native_generic, (unsigned long long)skin,
                 (unsigned long long)world2, (unsigned long long)raw, (unsigned long long)rt_serve);
}

SkinnedRenderer::SkinnedRenderer(const DrawShapeSpec& shape, Route route, Stats* stats)
    : shape_(shape), route_(route), stats_(stats) {}

void SkinnedRenderer::DrawAs(DrawPipeline& pipe, const DrawIr& ir, Route route) {
    DrawPipeline::PrepareOptions opt;
    opt.inherited_uniforms = kInheritSkinnedArm;
    pipe.DrawGeneric(ir, opt);
    stats_->native_generic++;
    if (route == kSkin) stats_->skin++;
    else if (route == kWorld2) stats_->world2++;
    else stats_->raw++;
    if (pipe.LastSource().owner.kind == TextureSource::OwnerKind::Surface) stats_->rt_serve++;
}

}  // namespace gs
}  // namespace ps2r
