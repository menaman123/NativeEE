// render/backends/gl/cd_copy_policy.cpp -- CdCopyPolicy implementation.
#include "engine/render/backends/gl/cd_copy_policy.h"

#include <algorithm>

namespace ps2r {
namespace render {

bool CdCopyPolicy::DirtyEmpty(const CdCopyState& st) {
    return st.dirty_x1 <= st.dirty_x0 || st.dirty_y1 <= st.dirty_y0;
}

bool CdCopyPolicy::RectsIntersect(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1,
                                  int by1) {
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

void CdCopyPolicy::ExtendDirty(CdCopyState& st, int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0) return;
    if (DirtyEmpty(st)) {
        st.dirty_x0 = x0; st.dirty_y0 = y0; st.dirty_x1 = x1; st.dirty_y1 = y1;
    } else {
        st.dirty_x0 = std::min(st.dirty_x0, x0);
        st.dirty_y0 = std::min(st.dirty_y0, y0);
        st.dirty_x1 = std::max(st.dirty_x1, x1);
        st.dirty_y1 = std::max(st.dirty_y1, y1);
    }
}

void CdCopyPolicy::Invalidate(CdCopyState& st) {
    st.valid = false;
    st.dirty_x0 = st.dirty_y0 = st.dirty_x1 = st.dirty_y1 = 0;
}

bool CdCopyPolicy::Decide(CdCopyState& st, int rx0, int ry0, int rw, int rh, bool force_full,
                          int tw, int th, int out[4]) {
    if (force_full) {
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
        st.valid = true;
        st.dirty_x0 = st.dirty_y0 = st.dirty_x1 = st.dirty_y1 = 0;
        return true;
    }
    const bool full_r = (rw <= 0 || rh <= 0);
    if (st.valid) {
        if (full_r) {
            if (DirtyEmpty(st)) return false;
        } else if (!RectsIntersect(rx0, ry0, rx0 + rw, ry0 + rh, st.dirty_x0, st.dirty_y0,
                                   st.dirty_x1, st.dirty_y1)) {
            return false;   // the copy already holds every pixel R can read
        }
    }
    int cx0, cy0, cx1, cy1;
    if (full_r) {
        cx0 = 0; cy0 = 0; cx1 = tw; cy1 = th;
    } else {
        cx0 = rx0; cy0 = ry0; cx1 = rx0 + rw; cy1 = ry0 + rh;
        if (!DirtyEmpty(st)) {
            cx0 = std::min(cx0, st.dirty_x0);
            cy0 = std::min(cy0, st.dirty_y0);
            cx1 = std::max(cx1, st.dirty_x1);
            cy1 = std::max(cy1, st.dirty_y1);
        }
        cx0 = std::max(0, std::min(cx0, tw));
        cy0 = std::max(0, std::min(cy0, th));
        cx1 = std::max(0, std::min(cx1, tw));
        cy1 = std::max(0, std::min(cy1, th));
    }
    if (cx0 <= 0 && cy0 <= 0 && cx1 >= tw && cy1 >= th) {
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;   // whole target
    } else {
        out[0] = cx0; out[1] = cy0;
        out[2] = cx1 - cx0; out[3] = cy1 - cy0;
        if (out[2] <= 0 || out[3] <= 0) {                 // degenerate union: whole target
            out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
        }
    }
    st.valid = true;
    st.dirty_x0 = st.dirty_y0 = st.dirty_x1 = st.dirty_y1 = 0;
    return true;
}

}  // namespace render
}  // namespace ps2r
