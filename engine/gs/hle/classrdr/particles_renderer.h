// gs/hle/classrdr/particles_renderer.h -- ParticlesRenderer: the particle class --
// gouraud STQ tristrips over a PSMT8 texture into a CT32 frame with depth ALWAYS,
// unblended or blended (0,1,0,1) / (0,2,0,1).
//
// A replica of the generic lowering (Draw IS DrawPipeline::DrawGeneric): pixels
// identical by construction; the class adds the blend-mode routing census. The
// shape is title data (spec).
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"

namespace ps2r {
namespace gs {

class ParticlesRenderer final : public ClassRenderer {
public:
    struct Stats {
        uint64_t native_spec = 0, native_generic = 0;
        uint64_t abe0 = 0, ff0101 = 0, ff0201 = 0, rt_serve = 0;
        void Emit(std::FILE* f) const;
    };
    explicit ParticlesRenderer(const DrawShapeSpec& shape);
    const char* Name() const override { return "particles-untex"; }
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
