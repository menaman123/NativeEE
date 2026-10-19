// tests/render/test_cd_copy_policy.cpp -- Render_CdCopyPolicyList: the shader-side blend's
// destination-copy elision with the dirty-rect LIST. A draw whose read rect misses every
// rect written since the last copy skips even when it hits their union (the shadow strips);
// a hit copies R plus the rects it touches and drops the rects inside the box, the others
// stay dirty; the list's overflow falls back to the union; force_full / the first copy /
// a whole-target copy clear everything; Invalidate resets. The rects live in gs::CdDirtySet
// (the ir library), recorded by the target's post-draw tail. Off-GL.
#include <cstdint>
#include <cstdio>

#include "engine/render/backends/gl/cd_copy_policy.h"

using ps2r::render::CdCopyPolicy;
using ps2r::render::CdCopyState;
using ps2r::gs::CdDirtySet;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

int main() {
    const int tw = 512, th = 448;
    int o[4];
    // The first copy (not valid): R plus the recorded dirty set; afterwards clean.
    {
        CdCopyState st;
        st.dirty.Extend(0, 0, 10, 10);
        CHECK(CdCopyPolicy::Decide(st, 100, 100, 20, 20, false, tw, th, o), "first copy");
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 120 && o[3] == 120, "first copy box = R u dirty");
        CHECK(st.valid && st.dirty.Count() == 0 && st.dirty.Empty(), "clean after the first copy");
        CHECK(!CdCopyPolicy::Decide(st, 100, 100, 20, 20, false, tw, th, o), "nothing dirty: skip");
    }
    // The strips: two disjoint bands written; a read of a third band hits the UNION of the two
    // (it lies between them) but no listed rect -> skip. A read of the first band copies it alone.
    {
        CdCopyState st;
        CdCopyPolicy::Decide(st, 0, 0, 0, 0, true, tw, th, o);   // valid, clean
        st.dirty.Extend(0, 0, 512, 32);              // strip A rows 0..32
        st.dirty.Extend(0, 96, 512, 128);            // strip C rows 96..128
        CHECK(st.dirty.Count() == 2 && st.dirty.Union().y0 == 0 && st.dirty.Union().y1 == 128, "two rects, union spans both");
        CHECK(!CdCopyPolicy::Decide(st, 0, 48, 512, 16, false, tw, th, o), "the band between them: skip (union would copy)");
        CHECK(st.dirty.Count() == 2, "a skip keeps the list");
        CHECK(CdCopyPolicy::Decide(st, 0, 8, 512, 8, false, tw, th, o), "a read inside strip A copies");
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 512 && o[3] == 32, "the box = R u strip A only");
        CHECK(st.dirty.Count() == 1 && st.dirty.At(0).y0 == 96 && st.dirty.At(0).y1 == 128, "strip A dropped, C stays dirty");
        CHECK(st.dirty.Union().y0 == 96 && st.dirty.Union().y1 == 128, "the union rebuilt from C");
        CHECK(!CdCopyPolicy::Decide(st, 0, 8, 512, 8, false, tw, th, o), "strip A is fresh now");
        CHECK(CdCopyPolicy::Decide(st, 0, 100, 16, 16, false, tw, th, o), "a read of C copies");
        CHECK(o[0] == 0 && o[1] == 96 && o[2] == 512 && o[3] == 32, "the box = R u strip C");
        CHECK(st.dirty.Count() == 0 && st.dirty.Empty(), "clean");
    }
    // A rect the box only partly covers stays dirty, whole.
    {
        CdCopyState st;
        CdCopyPolicy::Decide(st, 0, 0, 0, 0, true, tw, th, o);
        st.dirty.Extend(0, 0, 100, 100);     // A
        st.dirty.Extend(90, 90, 300, 300);   // B overlaps A's corner
        CHECK(CdCopyPolicy::Decide(st, 0, 0, 10, 10, false, tw, th, o), "a read of A's corner copies");
        CHECK(o[0] == 0 && o[1] == 0 && o[2] == 100 && o[3] == 100, "the box = A (R inside it)");
        CHECK(st.dirty.Count() == 1 && st.dirty.At(0).x0 == 90 && st.dirty.At(0).x1 == 300, "B stays dirty whole");
        CHECK(CdCopyPolicy::Decide(st, 95, 95, 2, 2, false, tw, th, o), "B's corner inside the old box still copies (conservative)");
        CHECK(o[0] == 90 && o[1] == 90 && o[2] == 210 && o[3] == 210, "the box = B");
    }
    // Overflow: more rects than the list holds -> the union alone decides until a copy.
    {
        CdCopyState st;
        CdCopyPolicy::Decide(st, 0, 0, 0, 0, true, tw, th, o);
        for (int i = 0; i <= CdDirtySet::kMaxRects; ++i) st.dirty.Extend(0, i * 20, 512, i * 20 + 8);
        CHECK(st.dirty.Overflow() && st.dirty.Count() == 0, "overflowed");
        const int gap_y = 8 + 2;   // between the first two strips: inside the union, outside every strip
        CHECK(CdCopyPolicy::Decide(st, 0, gap_y, 512, 4, false, tw, th, o), "overflow: the union decides -> copy");
        CHECK(o[0] == 0 && o[1] == 0 && o[3] == CdDirtySet::kMaxRects * 20 + 8, "the box = R u union");
        CHECK(!st.dirty.Overflow() && st.dirty.Count() == 0 && st.dirty.Empty(), "a copy clears the overflow");
    }
    // A whole-target read (rw == 0) with anything dirty copies the whole target and clears.
    {
        CdCopyState st;
        CdCopyPolicy::Decide(st, 0, 0, 0, 0, true, tw, th, o);
        CHECK(!CdCopyPolicy::Decide(st, 0, 0, 0, 0, false, tw, th, o), "whole read, nothing dirty: skip");
        st.dirty.Extend(5, 5, 6, 6);
        CHECK(CdCopyPolicy::Decide(st, 0, 0, 0, 0, false, tw, th, o) && o[2] == 0, "whole read, dirty: whole copy");
        CHECK(st.dirty.Count() == 0 && st.dirty.Empty(), "clean");
    }
    // Invalidate resets the list and the union; the next decision copies R u dirty.
    {
        CdCopyState st;
        CdCopyPolicy::Decide(st, 0, 0, 0, 0, true, tw, th, o);
        st.dirty.Extend(0, 0, 8, 8);
        CdCopyPolicy::Invalidate(st);
        CHECK(!st.valid && st.dirty.Count() == 0 && !st.dirty.Overflow() && st.dirty.Empty(), "invalidated");
        CHECK(CdCopyPolicy::Decide(st, 10, 10, 4, 4, false, tw, th, o) && o[0] == 10 && o[2] == 4, "copies R");
    }
    // Degenerate rects are ignored by ExtendDirty.
    {
        CdCopyState st;
        st.dirty.Extend(5, 5, 5, 9);
        CHECK(st.dirty.Count() == 0 && st.dirty.Empty(), "degenerate ignored");
    }
    std::printf("test_cd_copy_policy: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
