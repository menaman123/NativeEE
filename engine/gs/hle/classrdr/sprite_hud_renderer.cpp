// gs/hle/classrdr/sprite_hud_renderer.cpp -- the HUD sprite shapes through the generic path.
#include "engine/gs/hle/classrdr/sprite_hud_renderer.h"

#include "engine/gs/hle/classrdr/draw_pipeline.h"

namespace ps2r {
namespace gs {

void SpriteHudRenderer::Stats::Emit(std::FILE* f) const {
    if (!f || native_spec + native_generic == 0) return;
    std::fprintf(f, "[GS-SPRRDR] native=%llu spec=%llu generic=%llu untex=%llu rt_serve=%llu\n",
                 (unsigned long long)(native_spec + native_generic), (unsigned long long)native_spec,
                 (unsigned long long)native_generic, (unsigned long long)untex,
                 (unsigned long long)rt_serve);
}

SpriteHudRenderer::SpriteHudRenderer(const DrawShapeSpec& common, const DrawShapeSpec& textured,
                                     const DrawShapeSpec& untextured)
    : common_(common), textured_(textured), untextured_(untextured) {}

bool SpriteHudRenderer::Accept(const DrawIr& ir) const {
    if (!common_.Matches(ir)) return false;
    return textured_.Matches(ir) || untextured_.Matches(ir);
}

void SpriteHudRenderer::Draw(DrawPipeline& pipe, const DrawIr& ir) {
    DrawPipeline::PrepareOptions opt;
    opt.inherited_uniforms = kInheritSpriteHudArm;
    pipe.DrawGeneric(ir, opt);
    stats_.native_generic++;
    if (!ir.tex_enabled) stats_.untex++;
    if (pipe.LastSource().owner.kind == TextureSource::OwnerKind::Surface) stats_.rt_serve++;
}

}  // namespace gs
}  // namespace ps2r
