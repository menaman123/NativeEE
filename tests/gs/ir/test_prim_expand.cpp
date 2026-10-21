// tests/gs/ir/test_prim_expand.cpp -- GsPrimExpand: the thin-primitive quads.
// A line's quad covers exactly the integer sample points within half a pixel of
// the segment (the GS one-pixel line) and nothing else; a point covers its own
// cell; attributes follow the endpoint; triangle types are left alone.
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/gs/hle/ir/prim_expand.h"

using ps2r::gs::PrimExpand;
using ps2r::gs::VertexRec;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static VertexRec V(float x, float y, float z, uint8_t r, uint8_t fog = 0) {
    VertexRec v{};
    v.x = x; v.y = y; v.z = z; v.r = r; v.g = r; v.b = r; v.a = 0x80; v.fog = fog; v.s = x; v.t = y; v.q = 1.0f;
    v.uv_u = static_cast<uint16_t>(x * 16); v.uv_v = static_cast<uint16_t>(y * 16);
    return v;
}

// Point-in-triangle (inclusive) over the GS integer sample grid.
static bool InTri(float px, float py, const VertexRec& a, const VertexRec& b, const VertexRec& c) {
    auto e = [](float ax, float ay, float bx, float by, float x, float y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); };
    const float e0 = e(a.x, a.y, b.x, b.y, px, py), e1 = e(b.x, b.y, c.x, c.y, px, py), e2 = e(c.x, c.y, a.x, a.y, px, py);
    return (e0 >= -1e-5f && e1 >= -1e-5f && e2 >= -1e-5f) || (e0 <= 1e-5f && e1 <= 1e-5f && e2 <= 1e-5f);
}
static bool Covered(const std::vector<VertexRec>& t, int i, int j) {
    for (size_t k = 0; k + 2 < t.size(); k += 3)
        if (InTri(static_cast<float>(i), static_cast<float>(j), t[k], t[k + 1], t[k + 2])) return true;
    return false;
}

int main() {
    std::vector<VertexRec> out;
    CHECK(PrimExpand::IsThin(0) && PrimExpand::IsThin(1) && PrimExpand::IsThin(2), "point / line / linestrip are thin");
    CHECK(!PrimExpand::IsThin(3) && !PrimExpand::IsThin(4) && !PrimExpand::IsThin(5) && !PrimExpand::IsThin(6), "triangles / sprites are not");
    CHECK(PrimExpand::MinVerts(0) == 1 && PrimExpand::MinVerts(1) == 2 && PrimExpand::MinVerts(2) == 2 &&
          PrimExpand::MinVerts(3) == 3 && PrimExpand::MinVerts(6) == 3, "minimum vertex counts");
    // Triangle types: untouched.
    VertexRec tri[3] = {V(0, 0, 1, 1), V(4, 0, 1, 1), V(0, 4, 1, 1)};
    CHECK(PrimExpand::ToTriangles(tri, 3, 3, out) == 0 && out.empty(), "triangle type returns 0");
    CHECK(PrimExpand::ToTriangles(tri, 0, 1, out) == 0, "no vertices -> 0");
    CHECK(PrimExpand::ToTriangles(tri, 1, 1, out) == 0, "an unpaired line vertex draws nothing");
    // Horizontal line 200.3 -> 204.8 at y = 142.75 (Sly 2's ink line 63307 in the Paris twin).
    VertexRec ln[2] = {V(200.3125f, 142.75f, 57931, 16, 7), V(204.8125f, 141.75f, 57447, 16, 9)};
    CHECK(PrimExpand::ToTriangles(ln, 2, 1, out) == 6, "one line -> 6 vertices");
    // x-major: every column between the endpoints holds exactly the one sample nearest the line.
    int covered = 0, bad_cols = 0;
    for (int i = 196; i <= 209; ++i) {
        int c = 0, want = 0;
        const float t = (i - ln[0].x) / (ln[1].x - ln[0].x);
        const float yl = ln[0].y + t * (ln[1].y - ln[0].y);
        for (int j = 138; j <= 146; ++j) {
            if (Covered(out, i, j)) { c++; covered++; }
            if (t >= 0.0f && t <= 1.0f && std::fabs(j - yl) < 0.5f) want++;
        }
        if (c != want) bad_cols++;
    }
    CHECK(covered == 4, "the 200.3..204.8 line covers its 4 interior columns once each");
    CHECK(bad_cols == 0, "each column holds exactly the nearest sample (DDA line)");
    // A 45-degree-ish line offset by half a pixel still takes ONE pixel per step.
    VertexRec hl[2] = {V(0.5f, 0.0f, 1, 1), V(8.5f, 7.0f, 1, 1)};
    PrimExpand::ToTriangles(hl, 2, 1, out);
    int per_col_max = 0, cols_hit = 0;
    for (int i = 1; i <= 8; ++i) { int c = 0; for (int j = -1; j <= 9; ++j) c += Covered(out, i, j); if (c) cols_hit++; if (c > per_col_max) per_col_max = c; }
    CHECK(per_col_max == 1 && cols_hit == 8, "near-diagonal line: one pixel per column, every column");
    // Attributes follow the endpoint; z / fog / colour / STQ / UV copied.
    PrimExpand::ToTriangles(ln, 2, 1, out);
    int from_a = 0, from_b = 0;
    for (const VertexRec& v : out) {
        if (v.z == 57931.0f && v.fog == 7 && v.s == ln[0].s && v.uv_u == ln[0].uv_u) from_a++;
        if (v.z == 57447.0f && v.fog == 9 && v.s == ln[1].s && v.uv_u == ln[1].uv_u) from_b++;
        CHECK(v.r == 16 && v.a == 0x80, "colour copied");
    }
    CHECK(from_a == 3 && from_b == 3, "each endpoint owns three quad corners");
    // Minor-axis widening: the corners sit exactly 0.5 px above / below the endpoints.
    PrimExpand::ToTriangles(ln, 2, 1, out);
    for (const VertexRec& v : out)
        CHECK((std::fabs(v.x - ln[0].x) < 1e-5f && std::fabs(std::fabs(v.y - ln[0].y) - 0.5f) < 1e-5f) ||
              (std::fabs(v.x - ln[1].x) < 1e-5f && std::fabs(std::fabs(v.y - ln[1].y) - 0.5f) < 1e-5f),
              "corner at an endpoint x, +-0.5 in y");
    // Vertical and diagonal lines cover one sample per row / step.
    VertexRec vl[2] = {V(10.3f, 20.0f, 1, 1), V(10.3f, 25.0f, 1, 1)};
    PrimExpand::ToTriangles(vl, 2, 2, out);
    int rows = 0;
    for (int j = 18; j <= 27; ++j) { int c = 0; for (int i = 8; i <= 13; ++i) c += Covered(out, i, j); if (c == 1) rows++; CHECK(c <= 1, "vertical line is one pixel wide"); }
    CHECK(rows >= 5, "vertical line of 5 px covers 5+ rows");
    VertexRec dl[2] = {V(0.0f, 0.0f, 1, 1), V(8.0f, 8.0f, 1, 1)};
    PrimExpand::ToTriangles(dl, 2, 1, out);
    int diag = 0, off = 0;
    for (int j = -1; j <= 9; ++j) for (int i = -1; i <= 9; ++i) if (Covered(out, i, j)) { if (i == j) diag++; else off++; }
    CHECK(diag == 9 && off == 0, "45-degree line covers exactly its 9 diagonal samples");
    // Line pairs: 2 lines -> 12 vertices; a trailing odd vertex is dropped.
    VertexRec two[5] = {V(0, 0, 1, 1), V(3, 0, 1, 1), V(0, 2, 1, 1), V(3, 2, 1, 1), V(9, 9, 1, 1)};
    CHECK(PrimExpand::ToTriangles(two, 5, 1, out) == 12, "two lines + a stray vertex -> 12");
    // Zero-length line = the point cell.
    VertexRec zl[2] = {V(5.25f, 5.75f, 1, 1), V(5.25f, 5.75f, 1, 1)};
    CHECK(PrimExpand::ToTriangles(zl, 2, 1, out) == 6 && Covered(out, 5, 6) && !Covered(out, 6, 6) && !Covered(out, 5, 5),
          "zero-length line covers the nearest sample only");
    // Points: each covers exactly its nearest integer sample.
    VertexRec pts[3] = {V(3.2f, 7.6f, 1, 1), V(100.0f, 50.0f, 1, 1), V(0.49f, 0.49f, 1, 1)};
    CHECK(PrimExpand::ToTriangles(pts, 3, 0, out) == 18, "three points -> 18 vertices");
    int pc = 0;
    for (int j = -2; j <= 60; ++j) for (int i = -2; i <= 110; ++i) pc += Covered(out, i, j);
    CHECK(pc == 3 && Covered(out, 3, 8) && Covered(out, 100, 50) && Covered(out, 0, 0), "points cover exactly their three samples");
    std::printf("test_prim_expand: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
