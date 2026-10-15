// gs/hle/classrdr/fmv_blit_renderer.h -- FmvBlitRenderer: the fullscreen raw blit
// class -- an unblended DECAL sprite of a CT32 1024x1024 stage into a CT32 frame
// with depth GEQUAL and the alpha byte masked.
//
// A replica of the generic lowering (Draw IS DrawPipeline::DrawGeneric): pixels
// identical by construction; the class adds routing. The shape is title data.
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"

namespace ps2r {
namespace gs {

class FmvBlitRenderer final : public ClassRenderer {
public:
    struct Stats {
        uint64_t native_spec = 0, native_generic = 0, rt_serve = 0;
        void Emit(std::FILE* f) const;
    };
    explicit FmvBlitRenderer(const DrawShapeSpec& shape);
    const char* Name() const override { return "raw-blit-fullscreen"; }
    bool Accept(const DrawIr& ir) const override { return shape_.Matches(ir); }
    int WarmUp(DrawPipeline&) override { return 0; }   // a replica compiles no program of its own
    void Draw(DrawPipeline& pipe, const DrawIr& ir) override;
    const Stats& Count() const { return stats_; }

private:
    const DrawShapeSpec& shape_;
    Stats stats_;
};

}  // namespace gs
}  // namespace ps2r
