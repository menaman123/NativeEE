// render/backends/gl/cd_copy_policy.cpp -- CdCopyPolicy implementation.
#include "engine/render/backends/gl/cd_copy_policy.h"

#include <algorithm>

namespace ps2r {
namespace render {

void CdCopyPolicy::Invalidate(CdCopyState& st) {
    st.valid = false;
    st.dirty.Clear();
}

bool CdCopyPolicy::Decide(CdCopyState& st, int rx0, int ry0, int rw, int rh, bool force_full,
                          int tw, int th, int out[4]) {
    if (force_full) {
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
        st.valid = true;
        st.dirty.Clear();
        return true;
    }
    const bool full_r = (rw <= 0 || rh <= 0);
    gs::CdDirtySet::Rect box;
    if (full_r) {
        if (st.valid && st.dirty.Empty()) return false;
        box = gs::CdDirtySet::Rect{0, 0, tw, th};
    } else if (st.valid) {
        // The copy holds every pixel R can read unless R touches a written rect.
        if (!st.dirty.CopyBox(rx0, ry0, rx0 + rw, ry0 + rh, &box)) return false;
    } else {
        // Not valid yet: the first copy takes R plus everything recorded as dirty.
        box = gs::CdDirtySet::Rect{rx0, ry0, rx0 + rw, ry0 + rh};
        if (!st.dirty.Empty()) {
            const gs::CdDirtySet::Rect& u = st.dirty.Union();
            box.x0 = std::min(box.x0, u.x0); box.y0 = std::min(box.y0, u.y0);
            box.x1 = std::max(box.x1, u.x1); box.y1 = std::max(box.y1, u.y1);
        }
    }
    box.x0 = std::max(0, std::min(box.x0, tw));
    box.y0 = std::max(0, std::min(box.y0, th));
    box.x1 = std::max(0, std::min(box.x1, tw));
    box.y1 = std::max(0, std::min(box.y1, th));
    const bool whole = box.x0 <= 0 && box.y0 <= 0 && box.x1 >= tw && box.y1 >= th;
    if (whole || box.x1 <= box.x0 || box.y1 <= box.y0) {
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;   // whole target (a degenerate box too)
    } else {
        out[0] = box.x0; out[1] = box.y0;
        out[2] = box.x1 - box.x0; out[3] = box.y1 - box.y0;
    }
    const bool was_valid = st.valid;
    st.valid = true;
    if (out[2] == 0 || !was_valid) st.dirty.Clear();   // the whole target or the first copy: all fresh
    else st.dirty.Covered(box);
    return true;
}

}  // namespace render
}  // namespace ps2r
