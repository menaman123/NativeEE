// render/backends/gl/cd_copy_policy.h -- CdCopyPolicy: lazy elision of the shader-side
// blend's destination copy.
//
// The fragment shader reads the destination copy (u_rt_texture) only at
// gl_FragCoord of the draw's own fragments, all inside the draw's bbox ∩ scissor
// rect R. The copy can therefore be SKIPPED when the copy texture is valid and R
// does not intersect the union of the rectangles written since the last copy;
// otherwise it is refreshed over R ∪ dirty (re-validating everything stale), after
// which the tracker is valid with an empty dirty union. `force_full` (an indexed
// self-read samples the copy by UV, not by fragment position) always performs the
// whole-target copy. rw == 0 means "the draw may read the whole target". All
// rectangles are half-open logical pixels.
#pragma once

#include <cstdint>

#include "engine/render/backend.h"

namespace ps2r {
namespace render {

class CdCopyPolicy {
public:
    // Returns true when a copy must be performed; out = {x, y, w, h} (w == 0 => whole
    // target). On copy the tracker becomes valid with an empty dirty union.
    static bool Decide(CdCopyState& st, int rx0, int ry0, int rw, int rh, bool force_full,
                       int tw, int th, int out[4]);
    // Extend the dirty union by a committed draw's write rect (degenerate rects ignored).
    static void ExtendDirty(CdCopyState& st, int x0, int y0, int x1, int y1);
    // The copy can no longer be assumed to mirror the attachment.
    static void Invalidate(CdCopyState& st);
    static bool DirtyEmpty(const CdCopyState& st);
    static bool RectsIntersect(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1,
                               int by1);
};

}  // namespace render
}  // namespace ps2r
