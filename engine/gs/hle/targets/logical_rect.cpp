// gs/hle/targets/logical_rect.cpp -- the unclamped and the clamped union.
#include "engine/gs/hle/targets/logical_rect.h"

#include <algorithm>

namespace ps2r {
namespace gs {

void LogicalRect::Union(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1) {
    if (ax0 >= ax1 || ay0 >= ay1) return;
    if (Empty()) {
        x0 = ax0; y0 = ay0; x1 = ax1; y1 = ay1;
        return;
    }
    x0 = std::min(x0, ax0);
    y0 = std::min(y0, ay0);
    x1 = std::max(x1, ax1);
    y1 = std::max(y1, ay1);
}

void LogicalRect::ExpandToInclude(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1) {
    if (Empty()) {
        x0 = ax0; y0 = ay0; x1 = ax1; y1 = ay1;
        return;
    }
    x0 = std::min(x0, ax0);
    y0 = std::min(y0, ay0);
    x1 = std::max(x1, ax1);
    y1 = std::max(y1, ay1);
}

void LogicalRect::Expand(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1, int32_t w,
                         int32_t h) {
    ax0 = std::max<int32_t>(0, ax0);
    ay0 = std::max<int32_t>(0, ay0);
    ax1 = std::min<int32_t>(w, ax1);
    ay1 = std::min<int32_t>(h, ay1);
    Union(ax0, ay0, ax1, ay1);
}

bool LogicalRect::Covers(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1) const {
    if (Empty() || ax0 >= ax1 || ay0 >= ay1) return false;
    return x0 <= ax0 && y0 <= ay0 && x1 >= ax1 && y1 >= ay1;
}

}  // namespace gs
}  // namespace ps2r
