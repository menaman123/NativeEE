// gs/hle/texsrc/uv_bbox.h -- UvBbox: the texel-space bounding box of a textured
// draw's texture coordinates.
//
// The extrema of a projective texture coordinate over a triangle occur at its
// vertices (S/Q is monotone along an edge), so the vertex bbox bounds every texel
// the draw can sample. FST=1 reads the 12.4 UV register (u/16); FST=0 divides
// S,T by Q and scales by the texture size. The scan is a pure function of the
// vertex bytes: it normalises the FPU rounding mode to nearest around the STQ
// arithmetic so producer and consumer threads (the EE side runs round-toward-
// zero) compute identical boxes. A non-positive or non-finite Q, a non-finite or
// absurd (> 1e7) coordinate, or an empty box after the scan is a BAIL: the
// consumer decodes the full texture.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gs/hle/ir/vertex_rec.h"

namespace ps2r {
namespace gs {

class UvBbox {
public:
    enum State : uint8_t { kAbsent = 0, kOk = 1, kBail = 2 };
    struct Result {
        uint8_t state = kAbsent;
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;   // texel space; unspecified on kBail
    };
    // Scan `n` vertices; tex_w / tex_h are the level-0 texel dimensions.
    static Result Scan(const VertexRec* v, size_t n, bool fst, int tex_w, int tex_h);
};

}  // namespace gs
}  // namespace ps2r
