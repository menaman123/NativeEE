// gs/hle/targets/target_dims.cpp -- the per-draw render size rule.
#include "engine/gs/hle/targets/target_dims.h"

namespace ps2r {
namespace gs {

void TargetDims::Compute(const DrawIr& ir, int window_w, int window_h, const Display& disp,
                         const VramLayout& layout, int* render_w, int* render_h) {
    const uint32_t fbw = ir.raster.frame.fbw;
    const uint32_t fbp = ir.raster.frame.fbp;
    int w = (fbw > 0 && fbw <= kMaxFbw) ? static_cast<int>(fbw) : window_w;
    int h = window_h;
    if (fbp == disp.scanout_fbp && disp.content_w > 0 && disp.content_h > 0) {
        // Content-sized scanout target: the render target follows the DISPLAY's
        // content dimensions, not FRAME's buffer width.
        w = disp.content_w;
        h = disp.content_h;
    } else if (layout.IsSceneAccum(fbp)) {
        // The accumulator's real height is its scissor bound; only ever SHRINKS.
        const int scissor_h = static_cast<int>(ir.raster.scissor.y1) + 1;
        if (scissor_h > 0 && scissor_h < h) h = scissor_h;
    }
    *render_w = w;
    *render_h = h;
}

}  // namespace gs
}  // namespace ps2r
