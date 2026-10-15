// gs/hle/targets/logical_rect.h -- LogicalRect: a half-open rectangle in a
// target's logical (native GS) pixel space, with the unions the coherence
// bookkeeping accumulates: Union (unclamped, degenerate input ignored -- the
// drawn / companion rects follow the vertex bbox wherever it lands; consumers
// clamp to the target when they read), ExpandToInclude (unclamped, unguarded --
// the valid-content rect) and Expand (clamped to the target at accumulation time).
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

struct LogicalRect {
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // empty when x0 >= x1 || y0 >= y1
    bool Empty() const { return x0 >= x1 || y0 >= y1; }
    void Reset() { x0 = y0 = x1 = y1 = 0; }
    // Union with [ax0,ax1) x [ay0,ay1); degenerate input ignored; no clamp.
    void Union(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1);
    // Union with [ax0,ax1) x [ay0,ay1) WITHOUT the degenerate guard: an empty
    // rect takes the input as-is (a zero-area input leaves it empty), a
    // non-empty rect min/maxes with it even when the input has zero area. The
    // valid-content rect grows this way on the main draw route (the raster
    // extent of a zero-area triangle still marks its span as "authored").
    void ExpandToInclude(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1);
    // Union with [ax0,ax1) x [ay0,ay1) clamped to [0,w) x [0,h); degenerate input ignored.
    void Expand(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1, int32_t w, int32_t h);
    // True when this rect covers [ax0,ax1) x [ay0,ay1) entirely (both non-empty).
    bool Covers(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1) const;
};

}  // namespace gs
}  // namespace ps2r
