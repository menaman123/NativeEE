// tests/gs/ir/test_halfword_shuffle.cpp -- HalfwordShuffle: the "texture shuffle"
// sprite column pass over a 16-bit view of a 32-bit buffer as a per-pixel half-word
// copy (GS User's Manual 8.3 CT32 / CT16 block layouts; TEXA; TEST; FRAME.FBMSK; FBA;
// DTHE). Pins: Detect accepts exactly the canonical column pass (both parities, self
// and sibling source, the 0x3fff / 0x7fffbfff masks, DECAL and MODULATE by 0x80,
// ATE off / ALWAYS / NEVER+FB_ONLY / NEVER+RGB_ONLY with RGB_ONLY folded onto mask
// bit 15) and reports the Op; every gate rejects on its own -- a blend term, a
// non-sprite, FST off, a 32-bit FRAME or TEX, TBW != FBW, SCANMSK, DATE, a per-pixel
// Z test, a scissor short of the extent, column geometry, h16 % 64, FBA = 1 (the CT16
// write forces the A bit), TCC = 0 (the output alpha is the vertex alpha) and DTHE
// (a dithered write is not a copy); Fbmsk16From32 folds RGBA8 onto 5551.
#include <cstdio>

#include "engine/gs/hle/ir/halfword_shuffle.h"

using ps2r::gs::DrawIr;
using ps2r::gs::HalfwordShuffle;
using ps2r::gs::VertexRec;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            std::printf("  FAIL line %d: ", __LINE__);                \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

constexpr uint32_t kFbw = 512;   // 32 sprite columns
constexpr int kH16 = 448;        // the 16-bit extent: two 224-row fields, 7 * 64
constexpr uint32_t kFbp = 0x0;
constexpr uint32_t kSiblingTbp = 0x70000;

VertexRec Vert(float x, float y, float u, float v, uint8_t rgba) {
    VertexRec r{};
    r.x = x;
    r.y = y;
    r.z = 0.f;
    r.uv_u = static_cast<uint16_t>(u * 16.f);
    r.uv_v = static_cast<uint16_t>(v * 16.f);
    r.r = r.g = r.b = r.a = rgba;
    return r;
}

// One 8-wide column at x = col spanning [0, h16), textured from the column shifted by
// 8 the other way (u = x -/+ 8, v = y, texel centres at +0.5), as six vertices.
void PushColumn(DrawIr* ir, int col, int h16, uint8_t rgba) {
    const float x0 = static_cast<float>(col), x1 = x0 + 8.f;
    const float y0 = 0.f, y1 = static_cast<float>(h16);
    const float src = static_cast<float>((col % 16) ? col - 8 : col + 8);
    const float u0 = src + 0.5f, u1 = u0 + 8.f, v0 = 0.5f, v1 = v0 + y1;
    ir->verts.push_back(Vert(x0, y0, u0, v0, rgba));
    ir->verts.push_back(Vert(x1, y0, u1, v0, rgba));
    ir->verts.push_back(Vert(x0, y1, u0, v1, rgba));
    ir->verts.push_back(Vert(x1, y0, u1, v0, rgba));
    ir->verts.push_back(Vert(x1, y1, u1, v1, rgba));
    ir->verts.push_back(Vert(x0, y1, u0, v1, rgba));
}

// The canonical column pass as the title issues it: sprites, FST, DECAL, TCC = 1, a
// CT16 FRAME and CT16 TEX of equal FBW over the same base, no blend, ATE off, Z test
// ALWAYS, scissor over the whole extent, FBA / DTHE / SCANMSK off.
DrawIr Canonical(bool hi, uint32_t fbmsk = 0x3fffu, uint8_t rgba = 0x80) {
    DrawIr ir;
    ir.prim_type = 6;
    ir.tex_enabled = true;
    ir.alpha_blend = false;
    ir.scanmsk = 0;
    ir.tex.tbp = kFbp;
    ir.tex.tbw = kFbw;
    ir.tex.psm = 0x02;
    ir.tex.tcc = 1;
    ir.tex.tfx = 1;
    ir.tex.fst = true;
    ir.tex.texa_ta0 = 0x00;
    ir.tex.texa_ta1 = 0x80;
    ir.tex.texa_aem = false;
    ir.raster.frame.fbp = kFbp;
    ir.raster.frame.fbw = kFbw;
    ir.raster.frame.psm = 0x02;
    ir.raster.frame.fbmsk = fbmsk;
    ir.raster.test.ate = false;
    ir.raster.test.zte = true;
    ir.raster.test.ztst = 1;
    ir.raster.scissor.x0 = 0;
    ir.raster.scissor.y0 = 0;
    ir.raster.scissor.x1 = static_cast<uint16_t>(kFbw - 1);
    ir.raster.scissor.y1 = static_cast<uint16_t>(kH16 - 1);
    ir.raster.fba = false;
    ir.raster.dthe = 0;
    for (uint32_t c = 0; c < kFbw / 16; ++c) PushColumn(&ir, static_cast<int>(c * 16 + (hi ? 8 : 0)), kH16, rgba);
    return ir;
}

void ExpectAccept(const DrawIr& ir, bool hi, uint16_t mask16, bool self, const char* what) {
    HalfwordShuffle::Op op;
    const bool ok = HalfwordShuffle::Detect(ir, &op);
    CHECK(ok, "%s: rejected", what);
    if (!ok) return;
    CHECK(op.dest_hi == hi, "%s: dest_hi %d", what, op.dest_hi ? 1 : 0);
    CHECK(op.mask16 == mask16, "%s: mask16 %04x (want %04x)", what, op.mask16, mask16);
    CHECK(op.cols == static_cast<int>(kFbw), "%s: cols %d", what, op.cols);
    CHECK(op.rows32 == kH16 / 2, "%s: rows32 %d", what, op.rows32);
    CHECK(op.self == self, "%s: self %d", what, op.self ? 1 : 0);
    CHECK(op.ta0 == ir.tex.texa_ta0 && op.ta1 == ir.tex.texa_ta1 && op.aem == ir.tex.texa_aem,
          "%s: TEXA not carried (%02x %02x %d)", what, op.ta0, op.ta1, op.aem ? 1 : 0);
}

void ExpectReject(const DrawIr& ir, const char* what) {
    HalfwordShuffle::Op op;
    CHECK(!HalfwordShuffle::Detect(ir, &op), "%s: accepted", what);
}

}  // namespace

int main() {
    // Fbmsk16From32: RGBA8 mask bits 3-7 / 11-15 / 19-23 / 31 -> the 5551 fields.
    CHECK(HalfwordShuffle::Fbmsk16From32(0x3fffu) == 0x00ffu, "fold 0x3fff");
    // 0x7fffbfff: bit 31 (A) and bit 14 (G bit 6 = CT16 G field bit 3 = 5551 bit 8) open.
    CHECK(HalfwordShuffle::Fbmsk16From32(0x7fffbfffu) == 0x7effu, "fold 0x7fffbfff");
    CHECK(HalfwordShuffle::Fbmsk16From32(0xff000000u) == 0x8000u, "fold 0xff000000");
    CHECK(HalfwordShuffle::Fbmsk16From32(0u) == 0u, "fold 0");
    CHECK(HalfwordShuffle::Fbmsk16From32(0xffffffffu) == 0xffffu, "fold all");
    CHECK(HalfwordShuffle::Is16BitColor(0x02) && HalfwordShuffle::Is16BitColor(0x0A) &&
              !HalfwordShuffle::Is16BitColor(0x00) && !HalfwordShuffle::Is16BitColor(0x01),
          "Is16BitColor");

    // The canonical pass, both parities, both masks, self and sibling source.
    for (int hi = 0; hi < 2; ++hi) {
        ExpectAccept(Canonical(hi != 0, 0x3fffu), hi != 0, 0x00ff, true, "canonical 0x3fff");
        ExpectAccept(Canonical(hi != 0, 0x7fffbfffu), hi != 0, 0x7eff, true, "canonical 0x7fffbfff");
        DrawIr sib = Canonical(hi != 0);
        sib.tex.tbp = kSiblingTbp;
        ExpectAccept(sib, hi != 0, 0x00ff, false, "sibling source");
    }
    // The texel function: DECAL, or MODULATE by exactly 0x80 (identity); any other
    // vertex colour under MODULATE scales the texel.
    {
        DrawIr m = Canonical(false, 0x3fffu, 0x80);
        m.tex.tfx = 0;
        ExpectAccept(m, false, 0x00ff, true, "MODULATE by 0x80");
        DrawIr m7f = Canonical(false, 0x3fffu, 0x7f);
        m7f.tex.tfx = 0;
        ExpectReject(m7f, "MODULATE by 0x7f");
        DrawIr hl = Canonical(false);
        hl.tex.tfx = 2;
        ExpectReject(hl, "HIGHLIGHT");
    }
    // A uniform frame write: ATE off, ATST ALWAYS, NEVER + FB_ONLY, NEVER + RGB_ONLY
    // (the A bit folded into the keep mask).
    {
        DrawIr a = Canonical(false);
        a.raster.test.ate = true;
        a.raster.test.atst = 1;
        a.raster.test.afail = 0;
        ExpectAccept(a, false, 0x00ff, true, "ATST ALWAYS");
        DrawIr fb = Canonical(false);
        fb.raster.test.ate = true;
        fb.raster.test.atst = 0;
        fb.raster.test.afail = 1;
        ExpectAccept(fb, false, 0x00ff, true, "NEVER + FB_ONLY");
        DrawIr rgb = Canonical(true);
        rgb.raster.test.ate = true;
        rgb.raster.test.atst = 0;
        rgb.raster.test.afail = 3;
        ExpectAccept(rgb, true, 0x80ff, true, "NEVER + RGB_ONLY folds bit 15");
        DrawIr keep = Canonical(false);
        keep.raster.test.ate = true;
        keep.raster.test.atst = 0;
        keep.raster.test.afail = 0;
        ExpectReject(keep, "NEVER + KEEP (nothing written)");
        DrawIr zb = Canonical(false);
        zb.raster.test.ate = true;
        zb.raster.test.atst = 0;
        zb.raster.test.afail = 2;
        ExpectReject(zb, "NEVER + ZB_ONLY (no frame write)");
        for (uint8_t atst = 2; atst < 8; ++atst) {
            DrawIr pp = Canonical(false);
            pp.raster.test.ate = true;
            pp.raster.test.atst = atst;
            pp.raster.test.afail = 1;
            ExpectReject(pp, "per-pixel ATST");
        }
        DrawIr zoff = Canonical(false);
        zoff.raster.test.zte = false;
        ExpectAccept(zoff, false, 0x00ff, true, "ZTE off");
    }
    // The blend arm: ABE with A = B and D = Cs is Cv = Cs (no term); anything else is.
    {
        DrawIr ok = Canonical(false);
        ok.alpha_blend = true;
        ok.raster.alpha.a = 1;
        ok.raster.alpha.b = 1;
        ok.raster.alpha.c = 2;
        ok.raster.alpha.d = 0;
        ExpectAccept(ok, false, 0x00ff, true, "ABE A == B, D = Cs");
        DrawIr ab = Canonical(false);
        ab.alpha_blend = true;
        ab.raster.alpha.a = 0;
        ab.raster.alpha.b = 1;
        ab.raster.alpha.d = 0;
        ExpectReject(ab, "ABE A != B");
        DrawIr dcd = Canonical(false);
        dcd.alpha_blend = true;
        dcd.raster.alpha.a = 0;
        dcd.raster.alpha.b = 0;
        dcd.raster.alpha.d = 1;
        ExpectReject(dcd, "ABE A == B, D = Cd");
    }
    // Shape gates, one at a time.
    {
        DrawIr tri = Canonical(false);
        tri.prim_type = 4;
        ExpectReject(tri, "non-sprite");
        DrawIr notex = Canonical(false);
        notex.tex_enabled = false;
        ExpectReject(notex, "TME off");
        DrawIr stq = Canonical(false);
        stq.tex.fst = false;
        ExpectReject(stq, "FST off");
        DrawIr f32 = Canonical(false);
        f32.raster.frame.psm = 0x00;
        ExpectReject(f32, "32-bit FRAME");
        DrawIr t32 = Canonical(false);
        t32.tex.psm = 0x00;
        ExpectReject(t32, "32-bit TEX");
        DrawIr tbw = Canonical(false);
        tbw.tex.tbw = kFbw / 2;
        ExpectReject(tbw, "TBW != FBW");
        DrawIr sm = Canonical(false);
        sm.scanmsk = 2;
        ExpectReject(sm, "SCANMSK");
        DrawIr date = Canonical(false);
        date.raster.test.date = true;
        ExpectReject(date, "DATE");
        DrawIr z = Canonical(false);
        z.raster.test.zte = true;
        z.raster.test.ztst = 2;
        ExpectReject(z, "per-pixel Z (GEQUAL)");
        DrawIr scx = Canonical(false);
        scx.raster.scissor.x1 = static_cast<uint16_t>(kFbw - 2);
        ExpectReject(scx, "scissor short in x");
        DrawIr scy = Canonical(false);
        scy.raster.scissor.y1 = static_cast<uint16_t>(kH16 - 2);
        ExpectReject(scy, "scissor short in y");
        DrawIr sc0 = Canonical(false);
        sc0.raster.scissor.y0 = 1;
        ExpectReject(sc0, "scissor y0 > 0");
    }
    // The three semantic gates (FBA, TCC, DTHE), each alone on the canonical pass.
    {
        DrawIr fba = Canonical(false);
        fba.raster.fba = true;
        ExpectReject(fba, "FBA = 1 forces the written A bit");
        DrawIr tcc = Canonical(false);
        tcc.tex.tcc = 0;
        ExpectReject(tcc, "TCC = 0 writes the vertex alpha");
        DrawIr dthe = Canonical(false);
        dthe.raster.dthe = 1;
        ExpectReject(dthe, "DTHE = 1 dithers the CT16 write");
    }
    // Column geometry: a missing column, a duplicate parity, a 4-wide column, the wrong
    // texture shift, a height not a multiple of 64, a vertex count not a sprite multiple.
    {
        DrawIr missing = Canonical(false);
        missing.verts.resize(missing.verts.size() - 6);
        ExpectReject(missing, "one column short");
        DrawIr mixed = Canonical(false);
        for (int i = 0; i < 6; ++i) mixed.verts[static_cast<size_t>(i)].x += 8.f;  // column 0 -> 8 (other parity)
        ExpectReject(mixed, "mixed parity");
        DrawIr narrow = Canonical(false);
        for (size_t i = 0; i < 6; ++i)
            if (narrow.verts[i].x > 0.f) narrow.verts[i].x = 4.f;
        ExpectReject(narrow, "4-wide column");
        DrawIr shift = Canonical(false);
        for (size_t i = 0; i < 6; ++i) shift.verts[i].uv_u = static_cast<uint16_t>(shift.verts[i].uv_u + 16);
        ExpectReject(shift, "u = x + 9");
        DrawIr vshift = Canonical(false);
        for (size_t i = 0; i < 6; ++i) vshift.verts[i].uv_v = static_cast<uint16_t>(vshift.verts[i].uv_v + 16);
        ExpectReject(vshift, "v = y + 1");
        DrawIr h = Canonical(false);
        for (VertexRec& v : h.verts)
            if (v.y > 0.f) v.y = static_cast<float>(kH16 - 32);
        for (VertexRec& v : h.verts)
            if (v.uv_v > 8) v.uv_v = static_cast<uint16_t>((kH16 - 32) * 16 + 8);
        h.raster.scissor.y1 = static_cast<uint16_t>(kH16 - 33);
        ExpectReject(h, "h16 % 64 != 0");
        DrawIr odd = Canonical(false);
        odd.verts.pop_back();
        ExpectReject(odd, "vertex count not a multiple of 6");
        DrawIr fbw = Canonical(false);
        fbw.raster.frame.fbw = fbw.tex.tbw = 640;
        ExpectReject(fbw, "FBW not covered by the columns");
    }
    std::printf("HalfwordShuffle: %d checks, %d failures -> %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
