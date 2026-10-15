// gs/hle/classrdr/fmv_blit_renderer.cpp -- the blit shape through the generic path.
#include "engine/gs/hle/classrdr/fmv_blit_renderer.h"

#include "engine/gs/hle/classrdr/draw_pipeline.h"

namespace ps2r {
namespace gs {

void FmvBlitRenderer::Stats::Emit(std::FILE* f) const {
    if (!f || native_spec + native_generic == 0) return;
    std::fprintf(f, "[GS-FMVRDR] native=%llu spec=%llu generic=%llu rt_serve=%llu\n",
                 (unsigned long long)(native_spec + native_generic), (unsigned long long)native_spec,
                 (unsigned long long)native_generic, (unsigned long long)rt_serve);
}

FmvBlitRenderer::FmvBlitRenderer(const DrawShapeSpec& shape) : shape_(shape) {}

void FmvBlitRenderer::Draw(DrawPipeline& pipe, const DrawIr& ir) {
    DrawPipeline::PrepareOptions opt;
    opt.inherited_uniforms = kInheritFmvBlitArm;
    pipe.DrawGeneric(ir, opt);
    stats_.native_generic++;
    if (pipe.LastSource().owner.kind == TextureSource::OwnerKind::Surface) stats_.rt_serve++;
}

}  // namespace gs
}  // namespace ps2r
