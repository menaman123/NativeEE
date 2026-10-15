// gs/hle/classrdr/sprite_hud_renderer.h -- SpriteHudRenderer: the HUD / text sprite
// class -- FBA sprites into a CT32 frame with depth ALWAYS, either textured (PSMT8
// 128x256 atlas, blended (0,1,0,1)) or untextured flat.
//
// A replica of the generic lowering (Draw IS DrawPipeline::DrawGeneric): pixels
// identical by construction; the class adds routing. The three specs (common,
// textured, untextured) are title data.
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"

namespace ps2r {
namespace gs {

class SpriteHudRenderer final : public ClassRenderer {
public:
    struct Stats {
        uint64_t native_spec = 0, native_generic = 0, untex = 0, rt_serve = 0;
        void Emit(std::FILE* f) const;
    };
    SpriteHudRenderer(const DrawShapeSpec& common, const DrawShapeSpec& textured,
                      const DrawShapeSpec& untextured);
    const char* Name() const override { return "sprites-HUD"; }
    bool Accept(const DrawIr& ir) const override;
    int WarmUp(DrawPipeline&) override { return 0; }   // a replica compiles no program of its own
    void Draw(DrawPipeline& pipe, const DrawIr& ir) override;
    const Stats& Count() const { return stats_; }

private:
    const DrawShapeSpec& common_;
    const DrawShapeSpec& textured_;
    const DrawShapeSpec& untextured_;
    Stats stats_;
};

}  // namespace gs
}  // namespace ps2r
