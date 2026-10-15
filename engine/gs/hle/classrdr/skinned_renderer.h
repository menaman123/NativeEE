// gs/hle/classrdr/skinned_renderer.h -- SkinnedRenderer: the skinned-mesh state
// shape (gouraud STQ tristrips, PSMT8 texture, CT32 frame, no blend).
//
// A replica of the generic lowering for this shape: the pixels are the generic
// path's by construction (Draw IS DrawPipeline::DrawGeneric); what the class adds
// is the routing census. The same shape reaches three slots -- the skinned class
// (stamped), the world-tristrip class's second family (the world renderer
// delegates here) and the raw-other class (an unstamped skinned shape) -- and
// one Stats block counts all three routes. The shape itself is title data (spec).
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"

namespace ps2r {
namespace gs {

class SkinnedRenderer final : public ClassRenderer {
public:
    enum Route : uint8_t { kSkin = 0, kWorld2 = 1, kRaw = 2 };
    struct Stats {
        uint64_t native_spec = 0, native_generic = 0;
        uint64_t skin = 0, world2 = 0, raw = 0, rt_serve = 0;
        void Emit(std::FILE* f) const;
    };

    SkinnedRenderer(const DrawShapeSpec& shape, Route route, Stats* stats);
    const char* Name() const override { return route_ == kRaw ? "raw-skin" : "skinned"; }
    bool Accept(const DrawIr& ir) const override { return shape_.Matches(ir); }
    int WarmUp(DrawPipeline&) override { return 0; }   // a replica compiles no program of its own
    void Draw(DrawPipeline& pipe, const DrawIr& ir) override { DrawAs(pipe, ir, route_); }
    void DrawAs(DrawPipeline& pipe, const DrawIr& ir, Route route);
    const DrawShapeSpec& Shape() const { return shape_; }

private:
    const DrawShapeSpec& shape_;
    Route route_;
    Stats* stats_;
};

}  // namespace gs
}  // namespace ps2r
