// gs/hle/ir/cd_dirty_set.cpp -- CdDirtySet (cd_dirty_set.h).
#include "engine/gs/hle/ir/cd_dirty_set.h"

#include <algorithm>

namespace ps2r {
namespace gs {

void CdDirtySet::Extend(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0) return;
    if (Empty()) {
        union_ = Rect{x0, y0, x1, y1};
    } else {
        union_.x0 = std::min(union_.x0, x0);
        union_.y0 = std::min(union_.y0, y0);
        union_.x1 = std::max(union_.x1, x1);
        union_.y1 = std::max(union_.y1, y1);
    }
    if (overflow_) return;
    if (count_ >= kMaxRects) {
        overflow_ = true;
        count_ = 0;
        return;
    }
    rects_[count_++] = Rect{x0, y0, x1, y1};
}

void CdDirtySet::Clear() {
    union_ = Rect{};
    count_ = 0;
    overflow_ = false;
}

bool CdDirtySet::CopyBox(int rx0, int ry0, int rx1, int ry1, Rect* box) const {
    *box = Rect{rx0, ry0, rx1, ry1};
    if (Empty()) return false;
    if (overflow_) {
        if (!Intersect(union_, rx0, ry0, rx1, ry1)) return false;
        box->x0 = std::min(box->x0, union_.x0); box->y0 = std::min(box->y0, union_.y0);
        box->x1 = std::max(box->x1, union_.x1); box->y1 = std::max(box->y1, union_.y1);
        return true;
    }
    bool hit = false;
    for (int i = 0; i < count_; ++i) {
        const Rect& r = rects_[i];
        if (!Intersect(r, rx0, ry0, rx1, ry1)) continue;
        hit = true;
        box->x0 = std::min(box->x0, r.x0); box->y0 = std::min(box->y0, r.y0);
        box->x1 = std::max(box->x1, r.x1); box->y1 = std::max(box->y1, r.y1);
    }
    return hit;
}

void CdDirtySet::Covered(const Rect& box) {
    if (overflow_) { Clear(); return; }
    int kept = 0;
    for (int i = 0; i < count_; ++i) {
        const Rect& r = rects_[i];
        const bool inside = r.x0 >= box.x0 && r.y0 >= box.y0 && r.x1 <= box.x1 && r.y1 <= box.y1;
        if (inside) continue;
        if (kept != i) rects_[kept] = r;
        ++kept;
    }
    count_ = kept;
    union_ = Rect{};
    for (int i = 0; i < count_; ++i) {
        const Rect& r = rects_[i];
        if (i == 0) { union_ = r; continue; }
        union_.x0 = std::min(union_.x0, r.x0); union_.y0 = std::min(union_.y0, r.y0);
        union_.x1 = std::max(union_.x1, r.x1); union_.y1 = std::max(union_.y1, r.y1);
    }
}

}  // namespace gs
}  // namespace ps2r
