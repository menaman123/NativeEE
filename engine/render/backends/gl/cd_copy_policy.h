// render/backends/gl/cd_copy_policy.h -- CdCopyPolicy: lazy elision of the shader-side
// blend's destination copy.
//
// The fragment shader reads the destination copy (u_rt_texture) only at
// gl_FragCoord of the draw's own fragments, all inside the draw's bbox ∩ scissor
// rect R. The copy can therefore be SKIPPED when the copy texture is valid and R
// does not intersect any rectangle written since the last copy. The written rects
// are kept as a LIST (CdCopyState::rects, up to kDirtyRects) plus their union: a
// draw whose R misses every listed rect skips even when it hits the union (the
// shadow strips of one frame: each strip writes its own band, the next reads
// another); when the list overflows the union alone decides. Otherwise the copy
// covers R ∪ (the listed rects R intersects) -- or R ∪ union after an overflow --
// re-validating that box: the listed rects inside it are dropped, the rest stay
// dirty (conservative: a rect the box only partly covers stays whole). `force_full`
// (an indexed self-read samples the copy by UV, not by fragment position) always
// performs the whole-target copy and clears everything. rw == 0 means "the draw
// may read the whole target". All rectangles are half-open logical pixels.
// The rects are recorded by the target's post-draw tail into gs::CdDirtySet (the ir
// library: one definition, no backend include in gs/hle/targets).
//
// Channel awareness: the tail records every write in `dirty` and the writes whose colour
// mask included ALPHA in `dirty_alpha` as well. A read that consumes only the copy's
// alpha (`reads_rgb = false`: the destination alpha test of a draw blended by the fixed
// function or not blended) is decided on `dirty_alpha` alone -- the shadow-volume counter
// strips (FBMSK ff000000 set, DATE) then copy once per frame instead of once per strip.
// Every copy refreshes all channels of its box, so a copy drops the rects of BOTH sets
// inside the box.
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
                       int tw, int th, int out[4], bool reads_rgb = true);
    // The copy can no longer be assumed to mirror the attachment.
    static void Invalidate(CdCopyState& st);
    static bool DirtyEmpty(const CdCopyState& st) { return st.dirty.Empty(); }
};

}  // namespace render
}  // namespace ps2r
