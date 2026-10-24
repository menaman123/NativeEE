// gs/hle/ir/cd_dirty_set.h -- CdDirtySet: the rectangles a colour target was written over
// since the shader-side blend's destination copy was last taken -- their UNION (the coarse
// test every reader had) and, while they fit, the rects THEMSELVES, so a draw whose read
// rect misses every written rect skips the copy even when it lands inside the union (one
// frame's shadow strips: each writes its own band). The list overflows into the union
// alone (`Overflow()`) until the next copy clears it. Half-open logical pixels. Lives in
// the ir library: the target's post-draw tail (gs/hle/targets) records the rects, the GL
// backend's CdCopyPolicy decides on them -- one definition.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

class CdDirtySet {
public:
    static constexpr int kMaxRects = 12;
    struct Rect {
        int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    // A committed draw's write rect (degenerate rects ignored).
    void Extend(int x0, int y0, int x1, int y1);
    void Clear();
    bool Empty() const { return union_.x1 <= union_.x0 || union_.y1 <= union_.y0; }
    const Rect& Union() const { return union_; }
    // The rects themselves are usable (no overflow since the last clear).
    bool Listed() const { return !overflow_; }
    bool Overflow() const { return overflow_; }
    int Count() const { return count_; }
    const Rect& At(int i) const { return rects_[i]; }
    // The box a copy over read rect R must cover: R plus every listed rect R touches (listed)
    // or R plus the union (overflow). False = R touches nothing dirty (no copy needed).
    bool CopyBox(int rx0, int ry0, int rx1, int ry1, Rect* box) const;
    // A copy covered `box`: the listed rects inside it are fresh and dropped, the others stay
    // (a partly covered rect stays whole); the union is rebuilt. After an overflow everything
    // is cleared when the box holds the whole union (always the case for a box this set
    // decided); a box another set decided that misses part of the union keeps it all.
    void Covered(const Rect& box);

    static bool Intersect(const Rect& a, int bx0, int by0, int bx1, int by1) {
        return a.x0 < bx1 && bx0 < a.x1 && a.y0 < by1 && by0 < a.y1;
    }

private:
    Rect union_;
    Rect rects_[kMaxRects];
    int count_ = 0;
    bool overflow_ = false;
};

}  // namespace gs
}  // namespace ps2r
