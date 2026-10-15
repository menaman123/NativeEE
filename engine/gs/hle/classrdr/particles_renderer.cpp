// gs/hle/classrdr/particles_renderer.cpp -- the particle shape through the generic path.
#include "engine/gs/hle/classrdr/particles_renderer.h"

#include "engine/gs/hle/classrdr/draw_pipeline.h"

namespace ps2r {
namespace gs {

void ParticlesRenderer::Stats::Emit(std::FILE* f) const {
    if (!f || native_spec + native_generic == 0) return;
    std::fprintf(f,
                 "[GS-PARTRDR] native=%llu spec=%llu generic=%llu abe0=%llu"
                 " ff0101=%llu ff0201=%llu rt_serve=%llu\n",
                 (unsigned long long)(native_spec + native_generic), (unsigned long long)native_spec,
                 (unsigned long long)native_generic, (unsigned long long)abe0,
                 (unsigned long long)ff0101, (unsigned long long)ff0201,
                 (unsigned long long)rt_serve);
}

ParticlesRenderer::ParticlesRenderer(const DrawShapeSpec& shape) : shape_(shape) {}

void ParticlesRenderer::Draw(DrawPipeline& pipe, const DrawIr& ir) {
    DrawPipeline::PrepareOptions opt;
    opt.inherited_uniforms = kInheritParticlesArm;
    pipe.DrawGeneric(ir, opt);
    stats_.native_generic++;
    const DrawIr::Alpha& a = ir.raster.alpha;
    if (!ir.alpha_blend) stats_.abe0++;
    else if (a.a == 0 && a.b == 1 && a.c == 0 && a.d == 1) stats_.ff0101++;
    else if (a.a == 0 && a.b == 2 && a.c == 0 && a.d == 1) stats_.ff0201++;
    if (pipe.LastSource().owner.kind == TextureSource::OwnerKind::Surface) stats_.rt_serve++;
}

}  // namespace gs
}  // namespace ps2r
