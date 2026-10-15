// gs/hle/targets/target_dims.h -- TargetDims: the logical render size a frame
// draw's target is created / grown with.
//
// A PS2 FRAME register carries a base and a WIDTH (FBW) but no height: the
// drawable height is bounded by SCISSOR and the CRTC. The rule, per draw:
//   render_w = FBW when 0 < FBW <= 1024, else the window width;
//   render_h = the window height;
//   the scanout target (FRAME.FBP == DISPFB base) with known content dimensions
//     takes the CONTENT dimensions for both (the game's vertices span the
//     display's content width, and a buffer-width target would squash them);
//   else the title's scene accumulator (VramLayout) takes SCISSOR.Y1 + 1 as its
//     height when that is positive and SMALLER than the window height (the
//     accumulator is a 256-wide buffer drawn 224 / 225 rows tall under two
//     alternating scissors; the cache grows and never shrinks, so the
//     alternation never re-creates it).
// Anything below 1 is left to the caller's guards (a zero window is no draw).
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/vram_layout.h"

namespace ps2r {
namespace gs {

class TargetDims {
public:
    struct Display {
        uint32_t scanout_fbp = 0xFFFFFFFFu;   // DISPFB base (bytes); kNoFbp = none
        int content_w = 0;                    // DISPLAY-derived content dims (0 = unknown)
        int content_h = 0;
    };
    static constexpr uint32_t kMaxFbw = 1024;

    static void Compute(const DrawIr& ir, int window_w, int window_h, const Display& disp,
                        const VramLayout& layout, int* render_w, int* render_h);
};

}  // namespace gs
}  // namespace ps2r
