// tests/gs/ir/test_draw_coalescer.cpp -- GsDrawCoalescer: the ink line-pair fold.
// A run A1 B1 A2 B2 folds to two draws (the A lane, then the B lane, vertices in
// stream order); the fold never crosses a register change other than AFAIL, a
// target switch, a colour change, a src_frame / vram_epoch change, a non-candidate
// draw, a Flush (the caller's job / frame record); an orphan B (no partner A of the
// same positions right before it) passes through on its own; trailing A's fold.
#include <cstdio>
#include <vector>

#include "engine/gs/hle/ir/draw_coalescer.h"

using ps2r::gs::DrawCoalescer;
using ps2r::gs::DrawIr;
using ps2r::gs::VertexRec;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static VertexRec V(float x, float y, float z, uint8_t c = 16, uint8_t a = 0x80) {
    VertexRec v{};
    v.x = x; v.y = y; v.z = z; v.r = c; v.g = c; v.b = c; v.a = a; v.q = 1.0f; v.draw_kick = 1;
    return v;
}

// The ink line as the GS state machine flushes it: PRIM LINE, ATST NEVER, AFAIL
// RGB_ONLY (afail 3, the colour pass) or ZB_ONLY (afail 2, the Z pass), ZTST GREATER.
static DrawIr Line(float x0, float y0, float z0, float x1, float y1, float z1, uint8_t afail, uint32_t seq) {
    DrawIr d;
    d.seq = seq;
    d.src_frame = 7;
    d.vram_epoch = 3;
    d.prim_type = 1;
    d.raster.frame.fbp = 0;
    d.raster.frame.fbw = 512;
    d.raster.frame.psm = 0;
    d.raster.zbuf.zbp = 0x1c0000;
    d.raster.zbuf.psm = 1;
    d.raster.test.ate = true;
    d.raster.test.atst = 0;
    d.raster.test.afail = afail;
    d.raster.test.zte = true;
    d.raster.test.ztst = 3;
    d.raster.alpha.a = 0; d.raster.alpha.b = 1; d.raster.alpha.c = 0; d.raster.alpha.d = 1;
    d.raster.scissor.x1 = 511;
    d.raster.scissor.y1 = 447;
    d.verts = {V(x0, y0, z0), V(x1, y1, z1)};
    d.vertex_count = 2;
    return d;
}

static std::vector<DrawIr> Run(DrawCoalescer& k, std::vector<DrawIr> in, bool flush = true) {
    std::vector<DrawIr> out;
    for (DrawIr& d : in) {
        std::vector<DrawIr> pre;
        if (!k.Push(std::move(d), &pre)) {
            for (DrawIr& p : pre) out.push_back(std::move(p));
            out.push_back(std::move(d));
        } else {
            for (DrawIr& p : pre) out.push_back(std::move(p));
        }
    }
    if (flush) k.Flush(&out);
    return out;
}

int main() {
    // 1. Two pairs fold to two draws: A lane (4 verts, afail 3) then B lane (4 verts, afail 2).
    {
        DrawCoalescer k;
        std::vector<DrawIr> in;
        in.push_back(Line(10, 10, 100, 20, 12, 100, 3, 1));
        in.push_back(Line(10, 10, 100, 20, 12, 100, 2, 2));
        in.push_back(Line(20, 12, 100, 30, 15, 101, 3, 3));
        in.push_back(Line(20, 12, 100, 30, 15, 101, 2, 4));
        std::vector<DrawIr> out = Run(k, std::move(in));
        CHECK(out.size() == 2, "two pairs -> two draws");
        if (out.size() == 2) {
            CHECK(out[0].raster.test.afail == 3 && out[0].verts.size() == 4 && out[0].vertex_count == 4, "A lane first, 4 verts");
            CHECK(out[1].raster.test.afail == 2 && out[1].verts.size() == 4, "B lane second, 4 verts");
            CHECK(out[0].seq == 1 && out[1].seq == 2, "lanes keep their first record's seq");
            CHECK(out[0].verts[2].x == 20 && out[0].verts[3].x == 30, "A lane in stream order");
            CHECK(out[1].verts[2].x == 20 && out[1].verts[3].x == 30, "B lane in stream order");
            CHECK(out[0].bbox[0] == 10 && out[0].bbox[3] == 30, "bbox spans the lane");
        }
        const DrawCoalescer::Counters& n = k.Count();
        CHECK(n.in == 4 && n.out == 2 && n.absorbed == 2 && n.pairs == 2 && n.runs == 1 && n.passed == 0, "counters");
    }
    // 2. A non-candidate (a triangle) flushes the run before it and passes through after it.
    {
        DrawCoalescer k;
        std::vector<DrawIr> in;
        in.push_back(Line(0, 0, 1, 5, 5, 1, 3, 1));
        in.push_back(Line(0, 0, 1, 5, 5, 1, 2, 2));
        DrawIr tri = Line(0, 0, 1, 5, 5, 1, 3, 3);
        tri.prim_type = 3;
        tri.verts.push_back(V(9, 9, 1));
        in.push_back(tri);
        in.push_back(Line(1, 1, 1, 6, 6, 1, 3, 4));
        in.push_back(Line(1, 1, 1, 6, 6, 1, 2, 5));
        std::vector<DrawIr> out = Run(k, std::move(in));
        CHECK(out.size() == 5, "run / triangle / run");
        if (out.size() == 5) {
            CHECK(out[0].seq == 1 && out[1].seq == 2 && out[2].prim_type == 3 && out[3].seq == 4 && out[4].seq == 5, "order kept");
            CHECK(out[2].verts.size() == 3, "the triangle untouched");
        }
        CHECK(k.Count().runs == 2 && k.Count().passed == 1, "two runs, one pass-through");
    }
    // 3. Boundaries the fold never crosses: a changed register (ZTST), target (FBP), colour,
    //    src_frame, vram_epoch, FBMSK, scissor -- each starts a new run.
    {
        struct Case { const char* name; void (*mut)(DrawIr&); } cases[] = {
            {"ztst", [](DrawIr& d) { d.raster.test.ztst = 2; }},
            {"fbp", [](DrawIr& d) { d.raster.frame.fbp = 0x70000; }},
            {"zbp", [](DrawIr& d) { d.raster.zbuf.zbp = 0x1d0000; }},
            {"colour", [](DrawIr& d) { d.verts[0].r = d.verts[1].r = 17; }},
            {"src_frame", [](DrawIr& d) { d.src_frame = 8; }},
            {"vram_epoch", [](DrawIr& d) { d.vram_epoch = 4; }},
            {"fbmsk", [](DrawIr& d) { d.raster.frame.fbmsk = 0xff000000; }},
            {"scissor", [](DrawIr& d) { d.raster.scissor.x1 = 300; }},
            {"zmsk", [](DrawIr& d) { d.raster.zbuf.zmsk = true; }},
            {"ctx", [](DrawIr& d) { d.ctx = 1; }},
            {"offset", [](DrawIr& d) { d.off_x = 1.0f; }},
            {"dthe", [](DrawIr& d) { d.raster.dthe = 1; }},
        };
        for (const Case& c : cases) {
            DrawCoalescer k;
            std::vector<DrawIr> in;
            in.push_back(Line(0, 0, 1, 5, 5, 1, 3, 1));
            in.push_back(Line(0, 0, 1, 5, 5, 1, 2, 2));
            DrawIr a2 = Line(1, 1, 1, 6, 6, 1, 3, 3);
            DrawIr b2 = Line(1, 1, 1, 6, 6, 1, 2, 4);
            c.mut(a2);
            c.mut(b2);
            in.push_back(a2);
            in.push_back(b2);
            std::vector<DrawIr> out = Run(k, std::move(in));
            CHECK(out.size() == 4, c.name);
            CHECK(k.Count().runs == 2 && k.Count().absorbed == 0, c.name);
            if (out.size() == 4) CHECK(out[0].seq == 1 && out[1].seq == 2 && out[2].seq == 3 && out[3].seq == 4, c.name);
        }
    }
    // 4. Orphan B: a Z pass whose positions differ from the A before it ends the run and passes
    //    through; a Z pass with no A at all passes through; the lead B of a stream too.
    {
        DrawCoalescer k;
        std::vector<DrawIr> in;
        in.push_back(Line(0, 0, 1, 5, 5, 1, 2, 1));            // orphan: nothing before it
        in.push_back(Line(0, 0, 1, 5, 5, 1, 3, 2));
        in.push_back(Line(0, 0, 1, 5, 5, 1, 2, 3));
        in.push_back(Line(1, 1, 1, 6, 6, 1, 3, 4));
        in.push_back(Line(1, 1, 1, 6, 6, 2, 2, 5));            // z differs from its A: orphan
        in.push_back(Line(2, 2, 1, 7, 7, 1, 3, 6));
        in.push_back(Line(2, 2, 1, 7, 7, 1, 2, 7));
        std::vector<DrawIr> out = Run(k, std::move(in));
        // out: [1] orphan, [2,4 A lane][3 B lane], [5] orphan, [6][7]
        CHECK(out.size() == 6, "two orphans, a 2-pair run, a 1-pair run");
        if (out.size() == 6) {
            CHECK(out[0].seq == 1 && out[0].verts.size() == 2, "lead orphan passes first");
            CHECK(out[1].seq == 2 && out[1].verts.size() == 4 && out[1].raster.test.afail == 3, "A lane holds A2 and A4");
            CHECK(out[2].seq == 3 && out[2].verts.size() == 2 && out[2].raster.test.afail == 2, "B lane holds B3 alone");
            CHECK(out[3].seq == 5 && out[3].verts.size() == 2, "the mismatched B passes through after the run");
            CHECK(out[4].seq == 6 && out[5].seq == 7, "the next pair is its own run");
        }
        CHECK(k.Count().reject_orphan_z == 2, "two orphans counted");
    }
    // 5. Trailing A (no partner) folds into the A lane; a Flush with nothing pending is a no-op;
    //    a candidate must be exactly 2 vertices, untextured, unblended, ATST NEVER, both vertices
    //    one colour.
    {
        DrawCoalescer k;
        std::vector<DrawIr> in;
        in.push_back(Line(0, 0, 1, 5, 5, 1, 3, 1));
        in.push_back(Line(0, 0, 1, 5, 5, 1, 2, 2));
        in.push_back(Line(1, 1, 1, 6, 6, 1, 3, 3));
        std::vector<DrawIr> out = Run(k, std::move(in));
        CHECK(out.size() == 2 && out[0].verts.size() == 4 && out[1].verts.size() == 2, "trailing A folds");
        std::vector<DrawIr> none;
        k.Flush(&none);
        CHECK(none.empty(), "flush with nothing pending");
        DrawIr d = Line(0, 0, 1, 5, 5, 1, 3, 1);
        CHECK(DrawCoalescer::InkCandidate(d), "the ink line is a candidate");
        d.tex_enabled = true;
        CHECK(!DrawCoalescer::InkCandidate(d), "textured: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.alpha_blend = true;
        CHECK(!DrawCoalescer::InkCandidate(d), "blended: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.raster.test.atst = 1;
        CHECK(!DrawCoalescer::InkCandidate(d), "ATST ALWAYS: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.raster.test.ate = false;
        CHECK(!DrawCoalescer::InkCandidate(d), "ATE off: no");
        d = Line(0, 0, 1, 5, 5, 1, 1, 1);
        CHECK(!DrawCoalescer::InkCandidate(d), "AFAIL FB_ONLY (alpha written): no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.raster.test.date = true;
        CHECK(!DrawCoalescer::InkCandidate(d), "DATE: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.verts[1].r = 99;
        CHECK(!DrawCoalescer::InkCandidate(d), "two colours: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.verts.push_back(V(9, 9, 1));
        CHECK(!DrawCoalescer::InkCandidate(d), "3 vertices: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.fog = true;
        CHECK(!DrawCoalescer::InkCandidate(d), "fog: no");
        d = Line(0, 0, 1, 5, 5, 1, 3, 1); d.identity.hle_handler = 5;
        CHECK(!DrawCoalescer::InkCandidate(d), "HLE-stamped: no");
    }
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
