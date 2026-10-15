// gs/hle/classrdr/texture_shuffle_snap.h -- TextureShuffleSnap: the column-grid
// snap for split-texture-shuffle strip draws.
//
// GS rule: the GS samples texels at the DDA's integer coordinate of (u + 0.5)
// authored UVs -- for an 8-px-wide FST sprite strip whose x extent equals its u
// extent and whose x - u offset is 0 or 8 mod 16 (the 32 -> 16 bpp "offset
// shuffle": a 16-bit FRAME reading a 32-bit buffer as 16 bpp), the GS reads
// texels [8k, 8k+8) for output columns [8k+8, 8k+16). A host rasteriser samples
// at pixel centres and reads one texel late, which crosses into the next
// halfword lane on the last column of every 8-group. Snapping U to the nearest
// multiple of 8 and V to snap8(v) + nearbyint(v - snap8(v)) (the sub-8 row
// offset kept, the +0.5 bias removed) makes the centre sample land on the GS
// texel. Detection is all-or-nothing per draw; positions are never touched.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gs/hle/ir/vertex_rec.h"

namespace ps2r {
namespace gs {

class TextureShuffleSnap {
public:
    static constexpr float kEps = 0.75f;   // snap tolerance (>= the 0.5 authoring bias)

    static bool IsPsm16(uint8_t psm);
    static float SnapToColumn8(float v);   // nearbyint(v / 8) * 8 (round half even)
    static float SnapRow(float v);         // snap8(v) + nearbyint(v - snap8(v))
    // The column-shuffle signature over a triangle-expanded sprite stream (six
    // vertices per quad): textured FST sprites into a 16-bit FRAME reading a
    // 16-bit PSM, every quad with x-width == u-width (a multiple of 8, >= 8),
    // |x0 - u0| = 0 or 8 mod 16, and every UV corner within kEps of the snap grid.
    static bool DetectColumnShuffle(const VertexRec* verts, size_t count, uint8_t prim, bool fst,
                                    uint8_t frame_psm, uint8_t tex_psm, bool tex_enabled);
};

}  // namespace gs
}  // namespace ps2r
