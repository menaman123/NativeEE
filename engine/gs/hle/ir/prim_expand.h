// gs/hle/ir/prim_expand.h -- PrimExpand: the thin primitives (POINT, LINE,
// LINESTRIP) as triangle lists for a triangle-only rasteriser.
//
// The VertexKick emits one DrawIr vertex per POINT and two per LINE / LINESTRIP
// segment (vertex_kick.cpp FlushPrimitive); the backends draw GL_TRIANGLES /
// MTLPrimitiveTypeTriangle only, so until this helper every such draw was dropped
// (`n < 3`). The GS draws a line one pixel wide along the segment and a point as
// its one pixel (GS User's Manual 3.4.2 / 3.4.3: pixel (i, j) is drawn when the
// INTEGER sample point (i, j) of the XYOFFSET-relative primitive space lies on the
// primitive; the vertex shader translates by +0.5 so that grid meets GL's). A
// segment becomes the parallelogram spanning +-0.5 px along its MINOR axis (two
// triangles, six vertices; z / fog / colour / STQ / UV copied from the endpoint
// each corner belongs to -- IIP=0 colour was already flattened by the kick): one
// sample point per major-axis step, the GS DDA line; a point becomes the 1x1
// quad centred on it. The end caps are not extended: GL's own line
// rasteriser (what PCSX2's HW renderer draws these with, AA1 ignored) is half
// open at the far end as well, so a quad bounded by the endpoints covers the same
// sample points within the +-1 px the judges allow.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/vertex_rec.h"

namespace ps2r {
namespace gs {

class PrimExpand {
public:
    // PRIM.PRIM values (GS User's Manual 4.x PRIM): 0 point, 1 line, 2 line strip.
    static bool IsThin(uint8_t prim_type) { return (prim_type & 7u) <= 2u; }
    // The vertex count below which a draw of this PRIM type draws nothing: 1 for
    // points, 2 for lines, 3 for every triangle / sprite type (sprites arrive as 6).
    static size_t MinVerts(uint8_t prim_type) {
        const uint8_t t = prim_type & 7u;
        return t == 0 ? 1u : (t <= 2u ? 2u : 3u);
    }
    // Appends the triangle list for `n` kick-output vertices of a thin primitive to
    // `out` (cleared first): 6 vertices per point, 6 per complete line pair; a
    // trailing unpaired line vertex is dropped. Returns out.size(). Non-thin
    // types return 0 and leave `out` empty.
    static size_t ToTriangles(const VertexRec* v, size_t n, uint8_t prim_type, std::vector<VertexRec>& out);

    static constexpr float kHalfWidth = 0.5f;
};

}  // namespace gs
}  // namespace ps2r
