// gs/hle/ir/prim_expand.cpp -- see prim_expand.h.
#include "engine/gs/hle/ir/prim_expand.h"

#include <cmath>

namespace ps2r {
namespace gs {

namespace {

// The quad (a - n, a + n, b - n, b + n) as two triangles with a consistent winding
// (the backends cull nothing; the order only has to be a valid triangle list).
void PushQuad(const VertexRec& a, const VertexRec& b, float nx, float ny, std::vector<VertexRec>& out) {
    VertexRec a0 = a, a1 = a, b0 = b, b1 = b;
    a0.x -= nx; a0.y -= ny;
    a1.x += nx; a1.y += ny;
    b0.x -= nx; b0.y -= ny;
    b1.x += nx; b1.y += ny;
    out.push_back(a0); out.push_back(a1); out.push_back(b0);
    out.push_back(a1); out.push_back(b1); out.push_back(b0);
}

}  // namespace

size_t PrimExpand::ToTriangles(const VertexRec* v, size_t n, uint8_t prim_type, std::vector<VertexRec>& out) {
    out.clear();
    if (!IsThin(prim_type) || v == nullptr || n == 0) return 0;
    const uint8_t t = prim_type & 7u;
    if (t == 0) {
        out.reserve(n * 6);
        for (size_t i = 0; i < n; ++i) {
            // A point is its own 1x1 cell: the quad [x - 0.5, x + 0.5] x [y - 0.5, y + 0.5]
            // holds exactly the integer sample point nearest to (x, y).
            VertexRec a = v[i], b = v[i];
            a.y -= kHalfWidth; b.y += kHalfWidth;
            PushQuad(a, b, kHalfWidth, 0.0f, out);
        }
        return out.size();
    }
    const size_t pairs = n / 2;
    out.reserve(pairs * 6);
    for (size_t i = 0; i < pairs; ++i) {
        const VertexRec& a = v[2 * i];
        const VertexRec& b = v[2 * i + 1];
        float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        float nx, ny;
        if (len > 1e-6f && std::isfinite(len)) {
            // Widen along the MINOR axis by half a pixel: for an x-major segment the
            // quad spans y(x) +- 0.5, so every column holds exactly the one sample
            // point nearest the line (the GS DDA / Bresenham line, one pixel per
            // major step); a perpendicular band would take two pixels per step near
            // 45 degrees. Vertical / horizontal lines are the same under both.
            if (std::fabs(dx) >= std::fabs(dy)) { nx = 0.0f; ny = kHalfWidth; }
            else { nx = kHalfWidth; ny = 0.0f; }
        } else {
            // A zero-length line is the GS's single pixel at its endpoint: draw the
            // point cell (no direction to widen along).
            VertexRec a2 = a, b2 = b;
            a2.y -= kHalfWidth; b2.y += kHalfWidth;
            PushQuad(a2, b2, kHalfWidth, 0.0f, out);
            continue;
        }
        PushQuad(a, b, nx, ny, out);
    }
    return out.size();
}

}  // namespace gs
}  // namespace ps2r
