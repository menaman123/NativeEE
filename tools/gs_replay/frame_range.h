// tools/gs_replay/frame_range.h -- FrameRange: the inclusive `--frames a-b`
// selection over a recording's vsync_frame stamps (b defaults to the maximum).
#pragma once

#include <cstdint>

namespace ps2r {
namespace tools {

struct FrameRange {
    uint32_t lo = 0;
    uint32_t hi = UINT32_MAX;
    uint32_t step = 1;   // every step-th frame from lo ("a-b:s"); 1 = every frame in [lo, hi]
    bool Contains(uint32_t vf) const { return vf >= lo && vf <= hi && (step <= 1 || (vf - lo) % step == 0); }
};

}  // namespace tools
}  // namespace ps2r
