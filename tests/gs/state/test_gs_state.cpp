// tests/gs/state/test_gs_state.cpp -- GsRegisters / VertexKick / DrawFlush /
// GsState units. Usage: test_gs_state <case>
//   inv01 inv02 inv02b inv28 inv32 inv38 decode kick facade priv
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/direct_ops.h"
#include "engine/gs/hle/ir/draw_ir_lift.h"
#include "engine/gs/state/gs_state.h"
#include "tests/lib/capture_sink.h"

using namespace ps2r::gs;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(c)) {                                                         \
            ++g_fail;                                                       \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
        }                                                                   \
    } while (0)

namespace {

uint64_t Xyz(uint32_t x16, uint32_t y16, uint32_t z) {  // 64-bit XYZ: X 0-15 Y 16-31 Z 32-63
    return (uint64_t)(x16 & 0xFFFF) | ((uint64_t)(y16 & 0xFFFF) << 16) | ((uint64_t)z << 32);
}
uint64_t Rgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a, float q) {
    uint32_t qb;
    std::memcpy(&qb, &q, 4);
    return (uint64_t)r | ((uint64_t)g << 8) | ((uint64_t)b << 16) | ((uint64_t)a << 24) | ((uint64_t)qb << 32);
}
uint64_t Prim(uint32_t type, bool iip, bool tme, bool fge, bool abe, bool aa1, bool fst, bool ctxt) {
    return type | (iip << 3) | (tme << 4) | (fge << 5) | (abe << 6) | (aa1 << 7) | (fst << 8) | (ctxt << 9);
}
bool Feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }

struct Pt { float x, y; };
bool VertsXy(const std::vector<VertexRec>& v, const std::vector<Pt>& e) {
    if (v.size() != e.size()) return false;
    for (size_t i = 0; i < v.size(); ++i)
        if (!Feq(v[i].x, e[i].x) || !Feq(v[i].y, e[i].y)) return false;
    return true;
}

void KickAt(GsState& gs, float x, float y, bool draw = true, uint32_t z = 0x100) {
    gs.WriteRegister(draw ? 0x05 : 0x0D, Xyz((uint32_t)(x * 16), (uint32_t)(y * 16), z));
}

}  // namespace

// Invariant 1: the draw's TME is the live PRIM.TME at its FIRST kick.
static void Inv01() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.WriteRegister(0x00, Prim(3, 0, 1, 0, 0, 0, 0, 0));  // triangle, TME=1
    KickAt(gs, 1, 1);
    KickAt(gs, 2, 1);
    // PRMODE selected with TME=0 mid-draw: no type change, no flush (PRMODECONT still 1 -> ignored)
    gs.WriteRegister(0x1B, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 1, 2);
    CHECK(sink.draws.empty(), "no flush before EOP");
    gs.KickBoundary();
    CHECK(sink.draws.size() == 1 && sink.draws[0].tex_enabled, "TME latched at the first kick");
    // Second draw: PRIM write flips TME -> the next draw's first kick sees TME=0.
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 1, 1);
    gs.WriteRegister(0x1A, 0);                                    // PRMODECONT -> PRMODE selected (TME=0 there)
    gs.WriteRegister(0x1B, Prim(3, 0, 1, 0, 0, 0, 0, 0));          // PRMODE TME=1: would change -> flush of the 1-vertex? no: draw_buffer empty
    KickAt(gs, 2, 1);
    KickAt(gs, 1, 2);
    gs.KickBoundary();
    // The latch is taken by every kick that finds the draw buffer EMPTY; for a
    // triangle that is each of its first three kicks, so the PRMODE TME=1 written
    // before the completing kick is what the draw carries.
    CHECK(sink.draws.size() == 2 && sink.draws[1].tex_enabled, "latch re-taken while the buffer is still empty");
    CHECK(gs.Regs().prim.texture, "live TME is 1 after PRMODE");
    // With a triangle already in the buffer, a same-type PRIM flipping TME flushes
    // it at once (the attributes in force at ITS kicks): the draw keeps TME=1.
    gs.WriteRegister(0x1A, 1);
    gs.WriteRegister(0x00, Prim(3, 0, 1, 0, 0, 0, 0, 0));
    KickAt(gs, 1, 1); KickAt(gs, 2, 1); KickAt(gs, 1, 2);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    CHECK(sink.draws.size() == 3 && sink.draws[2].tex_enabled, "attribute PRIM flushes: draw keeps TME=1 of its kicks");
    KickAt(gs, 5, 5);
    CHECK(sink.draws.size() == 3, "the next kick opens the new draw");
    KickAt(gs, 6, 5); KickAt(gs, 5, 6);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 4 && !sink.draws[3].tex_enabled, "next draw latched TME=0 at its first kick");
}

// Invariant 2: raster/texture setters flush the queued draw first; PRIM only on a
// type change; DIMX/DTHE/TEXFLUSH never; the flush kind resets strip parity.
static void Inv02() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    CHECK(sink.draws.empty(), "queued");
    gs.WriteRegister(0x45, 1);  // DTHE: no flush
    gs.WriteRegister(0x44, 0x1234);  // DIMX: no flush
    gs.WriteRegister(0x3F, 0);  // TEXFLUSH: no flush
    CHECK(sink.draws.empty(), "DTHE/DIMX/TEXFLUSH do not flush");
    gs.WriteRegister(0x4C, 0x10);  // FRAME_1 -> flush
    CHECK(sink.draws.size() == 1 && sink.draws[0].raster.frame.fbp == 0, "FRAME flushed the draw with the OLD frame");
    CHECK(sink.draws[0].raster.dthe == 1 && sink.draws[0].raster.dimx == 0x1234, "dither state captured at flush");
    // PRIM with the same type and only IIP changed: no flush (flat colour is
    // applied per primitive by the kick).
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.WriteRegister(0x00, Prim(3, 1, 0, 0, 0, 0, 0, 0));
    CHECK(sink.draws.size() == 1, "same-type IIP-only PRIM does not flush");
    KickAt(gs, 5, 5); KickAt(gs, 6, 5); KickAt(gs, 5, 6);
    gs.WriteRegister(0x00, Prim(6, 0, 0, 0, 0, 0, 0, 0));  // type change -> flush
    CHECK(sink.draws.size() == 2 && sink.draws[1].verts.size() == 6 && sink.draws[1].prim_type == 3, "type change flushes both triangles");
    CHECK(sink.draws[1].verts[3].x == 5.0f, "second triangle appended (IIP change did not split)");
    // Each setter family flushes: TEX0 CLAMP TEX1 TEX2 XYOFFSET SCISSOR ALPHA TEST FBA ZBUF SCANMSK TEXA FOGCOL COLCLAMP PABE MIPTBP
    const uint8_t regs[] = {0x06, 0x08, 0x14, 0x16, 0x18, 0x40, 0x42, 0x47, 0x4A, 0x4E, 0x22, 0x3B, 0x3D, 0x46, 0x49, 0x34};
    gs.WriteRegister(0x00, Prim(0, 0, 0, 0, 0, 0, 0, 0));  // points: one kick = one primitive
    size_t n = sink.draws.size();
    for (uint8_t r : regs) {
        KickAt(gs, 3, 3);
        gs.WriteRegister(r, 0x7ull);
        CHECK(sink.draws.size() == n + 1, "setter flushed");
        n = sink.draws.size();
    }
    // A same-type PRIM changing TME with pending verts flushes them at once (Inv02b);
    // the StateChange flush resets the strip parity.
    gs.WriteRegister(0x00, Prim(4, 0, 0, 0, 0, 0, 0, 0));  // strip
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    n = sink.draws.size();
    gs.WriteRegister(0x00, Prim(4, 0, 1, 0, 0, 0, 0, 0));  // same type, TME change: immediate flush
    CHECK(sink.draws.size() == n + 1 && sink.draws.back().verts.size() == 3 && !sink.draws.back().tex_enabled,
          "PRIM attribute change flushes the strip with its own TME");
    KickAt(gs, 1, 1);
    CHECK(sink.draws.size() == n + 1, "the next kick starts the new draw");
    CHECK(gs.Kick().StripCount() == 0, "StateChange flush reset the strip parity");
}

// Invariant 28: every emitted draw carries the identity stamp + seq/epoch fields.
// Invariant 2b: a same-type PRIM that changes a draw-carried attribute (ABE here)
// flushes the queued primitives with the OLD bits -- Sly 2's ink lines: LINE
// PRIM 0xc1 (ABE AA1) + TEST RGB_ONLY + 2 kicks, then PRIM 0x01 + TEST ZB_ONLY +
// the same 2 kicks. The colour pass must carry ABE=1, the Z pass ABE=0.
static void Inv02b() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.WriteRegister(0x00, 0xc1);                 // LINE, ABE, AA1
    gs.WriteRegister(0x47, 0x73001);              // ATE ATST NEVER AFAIL RGB_ONLY ZTE ZTST GREATER
    KickAt(gs, 200, 143); KickAt(gs, 204, 141);
    CHECK(sink.draws.empty(), "line queued");
    gs.WriteRegister(0x00, 0x01);                 // LINE, no ABE: the Z-only pass
    CHECK(sink.draws.size() == 1, "attribute change flushed the colour pass at once");
    CHECK(sink.draws[0].alpha_blend && sink.draws[0].prim_type == 1 && sink.draws[0].verts.size() == 2,
          "colour pass keeps ABE=1 with its two vertices");
    CHECK(sink.draws[0].raster.test.afail == 3, "colour pass keeps TEST RGB_ONLY");
    gs.WriteRegister(0x47, 0x72001);              // AFAIL ZB_ONLY
    KickAt(gs, 200, 143); KickAt(gs, 204, 141);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 2 && !sink.draws[1].alpha_blend && sink.draws[1].raster.test.afail == 2,
          "Z pass: ABE=0, ZB_ONLY");
    // FGE / FST / CTXT flips split too; AA1 alone (not draw-carried) does not.
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 1, 0, 0));
    CHECK(sink.draws.size() == 2, "AA1-only PRIM does not flush");
    gs.WriteRegister(0x00, Prim(3, 0, 0, 1, 0, 0, 0, 0));
    CHECK(sink.draws.size() == 3 && !sink.draws[2].fog, "FGE flip flushes the queued triangle with FGE=0");
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 1, 0, 0, 1, 0));
    CHECK(sink.draws.size() == 4 && !sink.draws[3].use_fst, "FST flip flushes with FST=0");
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 1, 0, 0, 1, 1));
    CHECK(sink.draws.size() == 5 && sink.draws[4].ctx == 0, "CTXT flip flushes with the old context");
    gs.KickBoundary();
}

static void Inv28() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.SetIdentity(0xABCD, 0x1122334455667788ull, 0x3F0, 7, 42);
    gs.SetSrcFrame(9);
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.KickBoundary();
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1);
    gs.ClearIdentity();
    gs.KickBoundary();
    CHECK(sink.draws.size() == 2, "two draws");
    const DrawIr& a = sink.draws[0];
    CHECK(a.identity.hle_handler == 0xABCD && a.identity.image_hash == 0x1122334455667788ull && a.identity.kick_pc == 0x3F0 &&
              a.identity.kick_ord == 7 && a.identity.frame_inst == 42,
          "identity stamped");
    CHECK(a.seq == 0 && sink.draws[1].seq == 1 && a.src_frame == 9, "seq and src_frame");
    CHECK(sink.draws[1].identity.hle_handler == 0 && sink.draws[1].identity.image_hash == 0, "cleared identity = raw");
    CHECK(a.snapshot.verts_incl == 1 && a.snapshot.vertex_hash == GsdlFormat::VertexHash(a.verts.data(), 3) &&
              a.vertex_count == 3,
          "vertex hash / count stamped");
    float bb[6];
    GsdlFormat::Bbox(a.verts.data(), 3, bb);
    CHECK(std::memcmp(bb, a.bbox, sizeof bb) == 0, "bbox stamped");
    // Round trip through the on-disk record keeps the identity.
    DrawRec rec = a.ToDrawRec();
    DrawIr back = DrawIr::FromDrawRec(rec, a.verts.data(), a.verts.size());
    CHECK(back.identity.hle_handler == 0xABCD && back.identity.kick_ord == 7 && back.identity.frame_inst == 42, "identity survives the record");
}

// Invariant 32: the run-merge key includes the dither state (and every raster field);
// its VALUE equals the original runtime's ComputeMergeKeyAllPrim (known vectors).
static void Inv32() {
    DrawIr a;
    a.prim_type = 4;
    a.raster.frame.fbp = 0x40000;
    a.verts.push_back(VertexRec{});
    DrawIr b = a;
    CHECK(DrawFlush::RunKeyEquals(a, b), "identical draws share a key");
    b.raster.dthe = 1;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "DTHE splits the run");
    b = a;
    b.raster.dimx = 0x0000000000000001ull;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "DIMX splits the run");
    b = a;
    b.raster.alpha.fix = 3;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "FIX splits");
    b = a;
    b.raster.test.ztst = 2;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "ZTST splits");
    b = a;
    b.raster.scissor.x1 = 100;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "scissor splits");
    b = a;
    b.raster.scissor.x1 = 100; b.raster.scissor.y0 = 0;
    DrawIr bs = a;
    bs.raster.scissor.y0 = 100; bs.raster.scissor.x1 = 0;
    CHECK(!DrawFlush::RunKeyEquals(b, bs), "scissor fields are position-keyed (x0 y0 x1 y1), not a set");
    b = a;
    b.tex_enabled = true;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "texture enable splits");
    DrawIr c = b;
    c.tex.tbp = 0x1000;
    CHECK(!DrawFlush::RunKeyEquals(b, c), "TBP splits when textured");
    c = b;
    c.tex.minu = 5;
    CHECK(!DrawFlush::RunKeyEquals(b, c), "CLAMP splits when textured");
    c = a;
    c.tex.tbp = 0x1000;  // untextured: texture fields are not keyed
    CHECK(DrawFlush::RunKeyEquals(a, c), "untextured draws ignore TEX0");
    b = a;
    b.verts.push_back(VertexRec{});
    b.seq = 99;
    b.identity.hle_handler = 5;
    CHECK(DrawFlush::RunKeyEquals(a, b), "vertex count / seq / identity are not part of the key");
    CHECK(DrawFlush::RunKey(a) != 0, "key never 0 for a non-empty draw");
    b = a;
    b.verts.clear();
    CHECK(DrawFlush::RunKey(b) == 0, "empty vertex array keys to the 0 sentinel");
    b = a;
    b.shuffle_type = 1;
    CHECK(!DrawFlush::RunKeyEquals(a, b), "shuffle type splits");

    // Known vectors: OLD render.h ComputeMergeKeyAllPrim over the same field
    // values (generator: OLD's Fnv1aStep + the function body verbatim over a
    // minimal batch, /tmp/p6/fix6b2_keyvec.cpp).
    DrawIr k;
    k.verts.push_back(VertexRec{});
    k.prim_type = 4;
    k.raster.frame.fbp = 0x40000; k.raster.frame.fbw = 640; k.raster.frame.psm = 0; k.raster.frame.fbmsk = 0xFF000000u;
    k.raster.zbuf.zbp = 0x80000; k.raster.zbuf.psm = 1; k.raster.zbuf.zmsk = true;
    k.raster.scissor.x0 = 0; k.raster.scissor.y0 = 8; k.raster.scissor.x1 = 639; k.raster.scissor.y1 = 447;
    k.alpha_blend = true;
    k.raster.alpha.a = 0; k.raster.alpha.b = 1; k.raster.alpha.c = 0; k.raster.alpha.d = 1; k.raster.alpha.fix = 0x80;
    k.raster.colclamp = true; k.raster.pabe = false; k.raster.fba = true;
    k.raster.fogcol_r = 10; k.raster.fogcol_g = 20; k.raster.fogcol_b = 30;
    k.raster.dthe = 1; k.raster.dimx = 0x7F3F5F1F6E2E4E0Eull;
    k.raster.test.ate = true; k.raster.test.atst = 6; k.raster.test.aref = 0x40; k.raster.test.afail = 3;
    k.raster.test.date = false; k.raster.test.datm = true; k.raster.test.zte = true; k.raster.test.ztst = 2;
    k.gouraud = true; k.fog = false; k.use_fst = true; k.scanmsk = 2; k.shuffle_type = 0;
    CHECK(DrawFlush::RunKey(k) == 0xf7533519e738430aull, "untextured known vector == OLD ComputeMergeKeyAllPrim");
    k.tex_enabled = true;
    k.tex.fst = true;
    k.tex.tbp = 0x1000; k.tex.tbw = 256; k.tex.psm = 0x13; k.tex.tw = 8; k.tex.th = 7; k.tex.tcc = 1; k.tex.tfx = 0;
    k.tex.cbp = 0x2000; k.tex.cpsm = 0; k.tex.csm = 0; k.tex.csa = 3; k.tex.cld = 1; k.tex.mmag = true; k.tex.mmin = 5;
    k.tex.wms = 0; k.tex.wmt = 1; k.tex.minu = 0; k.tex.maxu = 255; k.tex.minv = 0; k.tex.maxv = 127;
    k.tex.texa_ta0 = 0x80; k.tex.texa_ta1 = 0x00; k.tex.texa_aem = true;
    CHECK(DrawFlush::RunKey(k) == 0xbc6fc9cd8ec1a36aull, "textured known vector == OLD ComputeMergeKeyAllPrim");
}

// Invariant 38 (sprite half): corners ordered min/max on x and y with s/u and t/v
// following, colour/z/fog flat from vertex 2, NO snapping to pixel centres.
static void Inv38() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.WriteRegister(0x00, Prim(6, 0, 1, 0, 0, 0, 1, 0));  // sprite, TME, FST
    gs.WriteRegister(0x01, Rgbaq(10, 20, 30, 40, 1.0f));
    gs.WriteRegister(0x03, 0x0080u | (0x0100u << 16));  // U=8.0 V=16.0
    gs.WriteRegister(0x05, Xyz(100 * 16 + 5, 50 * 16 + 3, 0x20));  // (100.3125, 50.1875)
    gs.WriteRegister(0x01, Rgbaq(1, 2, 3, 4, 1.0f));
    gs.WriteRegister(0x03, 0x0020u | (0x0040u << 16));  // U=2.0 V=4.0
    gs.WriteRegister(0x05, Xyz(20 * 16, 10 * 16, 0x30));  // (20, 10) -- top-left of the rectangle
    gs.KickBoundary();
    CHECK(sink.draws.size() == 1 && sink.draws[0].verts.size() == 6, "sprite -> 6 vertices");
    const std::vector<VertexRec>& v = sink.draws[0].verts;
    CHECK(VertsXy(v, {{20, 10}, {100.3125f, 10}, {20, 50.1875f}, {100.3125f, 10}, {100.3125f, 50.1875f}, {20, 50.1875f}}),
          "tl tr bl tr br bl with ordered corners, fractional coords kept (no snapping)");
    CHECK(v[0].uv_u == 0x20 && v[0].uv_v == 0x40 && v[1].uv_u == 0x80 && v[1].uv_v == 0x40 && v[2].uv_u == 0x20 &&
              v[2].uv_v == 0x100 && v[4].uv_u == 0x80 && v[4].uv_v == 0x100,
          "u/v travel with their corners");
    CHECK(Feq(v[0].s, 2.0f) && Feq(v[0].t, 4.0f) && Feq(v[4].s, 8.0f) && Feq(v[4].t, 16.0f), "s/t from UV/16 (FST)");
    bool flat = true;
    for (const VertexRec& x : v) flat &= x.r == 1 && x.g == 2 && x.b == 3 && x.a == 4 && x.z == 0x30;
    CHECK(flat, "colour and z flat from the second vertex");
    // Mode 0 leaves the corners as kicked.
    GsState g0;
    CaptureSink s0;
    g0.SetSink(&s0);
    g0.SetQuirks(VertexKickQuirks{0});
    g0.WriteRegister(0x00, Prim(6, 0, 0, 0, 0, 0, 0, 0));
    KickAt(g0, 100, 50); KickAt(g0, 20, 10);
    g0.KickBoundary();
    CHECK(s0.draws.size() == 1 && VertsXy(s0.draws[0].verts, {{100, 50}, {20, 50}, {100, 10}, {20, 50}, {20, 10}, {100, 10}}),
          "mode 0: no corner ordering");
    // Invariant 38 has two halves; this test pins the roundSprite normalisation half
    // only. The XGKick-deferral half (the VU1 kick is deferred until the microprogram's
    // packet is complete) is Phase 5's (engine/vu) and is not registered under Phase 6.
    std::printf("Gs_Inv38: roundSprite half pinned here; XGKick-deferral half owned by Phase 5 (engine/vu)\n");
}

// Register decode vectors: at least three per register, every field.
static void DecodeRegs() {
    GsRegisters r;
    // TEX0 (0x06/0x07)
    struct T0 { uint64_t v; uint32_t tbp, tbw, psm, tw, th, tcc, tfx, cbp, cpsm, csm, csa, cld; };
    const T0 t0[3] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {~0ull, 0x3FFF * 256, 0x3F * 64, 0x3F, 0xF, 0xF, 1, 3, 0x3FFF * 256, 0xF, 1, 0x1F, 7},
        {(uint64_t)0x1234 | (5ull << 14) | (0x13ull << 20) | (7ull << 26) | (6ull << 30) | (1ull << 34) | (2ull << 35) |
             (0x2ABCull << 37) | (0xAull << 51) | (1ull << 55) | (0x15ull << 56) | (4ull << 61),
         0x1234 * 256, 5 * 64, 0x13, 7, 6, 1, 2, 0x2ABC * 256, 0xA, 1, 0x15, 4}};
    for (int c = 0; c < 2; ++c)
        for (const T0& t : t0) {
            r.Write(0x06 + c, t.v);
            const GsRegisters::Tex0& x = r.ctx[c].tex0;
            CHECK(x.tbp == t.tbp && x.tbw == t.tbw && x.psm == t.psm && x.tw == t.tw && x.th == t.th && x.tcc == t.tcc &&
                      x.tfx == t.tfx && x.cbp == t.cbp && x.cpsm == t.cpsm && x.csm == t.csm && x.csa == t.csa && x.cld == t.cld,
                  "TEX0 decode");
            CHECK(r.dirty_regs & GsRegisters::kDirtyTex0, "TEX0 dirty");
        }
    // CLAMP
    r.Write(0x08, 0); CHECK(r.ctx[0].clamp.wms == 0 && r.ctx[0].clamp.maxv == 0, "CLAMP 0");
    r.Write(0x09, ~0ull);
    CHECK(r.ctx[1].clamp.wms == 3 && r.ctx[1].clamp.wmt == 3 && r.ctx[1].clamp.minu == 0x3FF && r.ctx[1].clamp.maxu == 0x3FF &&
              r.ctx[1].clamp.minv == 0x3FF && r.ctx[1].clamp.maxv == 0x3FF, "CLAMP ones");
    r.Write(0x08, 1ull | (2ull << 2) | (0x11ull << 4) | (0x22ull << 14) | (0x33ull << 24) | (0x44ull << 34));
    CHECK(r.ctx[0].clamp.wms == 1 && r.ctx[0].clamp.wmt == 2 && r.ctx[0].clamp.minu == 0x11 && r.ctx[0].clamp.maxu == 0x22 &&
              r.ctx[0].clamp.minv == 0x33 && r.ctx[0].clamp.maxv == 0x44, "CLAMP mixed");
    // TEX1
    r.Write(0x14, 0); CHECK(!r.ctx[0].tex1.lcm && r.ctx[0].tex1.mxl == 0 && r.ctx[0].tex1.k == 0, "TEX1 0");
    r.Write(0x15, ~0ull);
    CHECK(r.ctx[1].tex1.lcm && r.ctx[1].tex1.mxl == 7 && r.ctx[1].tex1.mmag && r.ctx[1].tex1.mmin == 7 && r.ctx[1].tex1.mtba &&
              r.ctx[1].tex1.l == 3 && r.ctx[1].tex1.k == 0xFFF, "TEX1 ones");
    r.Write(0x14, 1ull | (5ull << 2) | (1ull << 5) | (4ull << 6) | (1ull << 9) | (2ull << 19) | (0xABCull << 32));
    CHECK(r.ctx[0].tex1.lcm && r.ctx[0].tex1.mxl == 5 && r.ctx[0].tex1.mmag && r.ctx[0].tex1.mmin == 4 && r.ctx[0].tex1.mtba &&
              r.ctx[0].tex1.l == 2 && r.ctx[0].tex1.k == 0xABC, "TEX1 mixed");
    // TEX2: CLUT fields only, TBP untouched, no dirty bit
    r.Write(0x06, t0[2].v);
    r.dirty_regs = 0;
    r.Write(0x16, (0x02ull << 20) | (0x0100ull << 37) | (0x2ull << 51) | (1ull << 55) | (0x3ull << 56) | (1ull << 61));
    CHECK(r.ctx[0].tex0.psm == 0x02 && r.ctx[0].tex0.cbp == 0x100 * 256 && r.ctx[0].tex0.cpsm == 2 && r.ctx[0].tex0.csm == 1 &&
              r.ctx[0].tex0.csa == 3 && r.ctx[0].tex0.cld == 1 && r.ctx[0].tex0.tbp == 0x1234 * 256 && r.ctx[0].tex0.tw == 7,
          "TEX2 decode keeps TBP/TW");
    CHECK(r.dirty_regs == 0, "TEX2 sets no dirty bit");
    r.Write(0x17, 0); CHECK(r.ctx[1].tex0.cld == 0 && r.ctx[1].tex0.psm == 0, "TEX2 0");
    r.Write(0x17, ~0ull); CHECK(r.ctx[1].tex0.cld == 7 && r.ctx[1].tex0.csa == 0x1F && r.ctx[1].tex0.psm == 0x3F, "TEX2 ones");
    // XYOFFSET
    r.Write(0x18, 0); CHECK(r.ctx[0].xyoffset.x == 0 && r.ctx[0].xyoffset.y == 0, "XYOFFSET 0");
    r.Write(0x19, ~0ull); CHECK(Feq(r.ctx[1].xyoffset.x, 4095.9375f) && Feq(r.ctx[1].xyoffset.y, 4095.9375f), "XYOFFSET ones");
    r.Write(0x18, (uint64_t)(2048 * 16 + 8) | ((uint64_t)(1024 * 16 + 4) << 32));
    CHECK(Feq(r.ctx[0].xyoffset.x, 2048.5f) && Feq(r.ctx[0].xyoffset.y, 1024.25f), "XYOFFSET mixed (12.4)");
    // SCISSOR
    r.Write(0x40, 0); CHECK(r.ctx[0].scissor.x1 == 0, "SCISSOR 0");
    r.Write(0x41, ~0ull); CHECK(r.ctx[1].scissor.x0 == 0x7FF && r.ctx[1].scissor.x1 == 0x7FF && r.ctx[1].scissor.y0 == 0x7FF && r.ctx[1].scissor.y1 == 0x7FF, "SCISSOR ones");
    r.Write(0x40, 10ull | (639ull << 16) | (20ull << 32) | (447ull << 48));
    CHECK(r.ctx[0].scissor.x0 == 10 && r.ctx[0].scissor.x1 == 639 && r.ctx[0].scissor.y0 == 20 && r.ctx[0].scissor.y1 == 447, "SCISSOR mixed");
    // ALPHA
    r.Write(0x42, 0); CHECK(r.ctx[0].alpha.a == 0 && r.ctx[0].alpha.d == 0 && r.ctx[0].alpha.fix == 0, "ALPHA 0");
    r.Write(0x43, ~0ull); CHECK(r.ctx[1].alpha.a == 3 && r.ctx[1].alpha.b == 3 && r.ctx[1].alpha.c == 3 && r.ctx[1].alpha.d == 3 && r.ctx[1].alpha.fix == 0xFF, "ALPHA ones");
    r.Write(0x42, 0ull | (1ull << 2) | (2ull << 4) | (1ull << 6) | (0x80ull << 32));
    CHECK(r.ctx[0].alpha.a == 0 && r.ctx[0].alpha.b == 1 && r.ctx[0].alpha.c == 2 && r.ctx[0].alpha.d == 1 && r.ctx[0].alpha.fix == 0x80, "ALPHA mixed");
    // TEST (0x47 ctx1, 0x48 ctx2)
    r.Write(0x47, 0); CHECK(!r.ctx[0].test.ate && !r.ctx[0].test.zte && r.ctx[0].test.ztst == 0, "TEST 0");
    r.Write(0x48, ~0ull); CHECK(r.ctx[1].test.ate && r.ctx[1].test.atst == 7 && r.ctx[1].test.aref == 0xFF && r.ctx[1].test.afail == 3 && r.ctx[1].test.date && r.ctx[1].test.datm && r.ctx[1].test.zte && r.ctx[1].test.ztst == 3, "TEST ones");
    r.Write(0x47, 1ull | (6ull << 1) | (0x40ull << 4) | (2ull << 12) | (1ull << 14) | (0ull << 15) | (1ull << 16) | (2ull << 17));
    CHECK(r.ctx[0].test.ate && r.ctx[0].test.atst == 6 && r.ctx[0].test.aref == 0x40 && r.ctx[0].test.afail == 2 && r.ctx[0].test.date && !r.ctx[0].test.datm && r.ctx[0].test.zte && r.ctx[0].test.ztst == 2, "TEST mixed");
    CHECK(r.ctx[1].test.ztst == 3 && r.env.pabe == false, "TEST_1 write left ctx 2 and PABE alone");
    // FRAME / ZBUF
    r.Write(0x4C, 0); CHECK(r.ctx[0].frame.fbp == 0 && r.ctx[0].frame.fbmsk == 0, "FRAME 0");
    r.Write(0x4D, ~0ull); CHECK(r.ctx[1].frame.fbp == 0x1FF * 8192 && r.ctx[1].frame.fbw == 0x3F * 64 && r.ctx[1].frame.psm == 0x3F && r.ctx[1].frame.fbmsk == 0xFFFFFFFF, "FRAME ones");
    r.Write(0x4C, 0x23ull | (10ull << 16) | (0x02ull << 24) | (0x00FF00FFull << 32));
    CHECK(r.ctx[0].frame.fbp == 0x23 * 8192 && r.ctx[0].frame.fbw == 640 && r.ctx[0].frame.psm == 2 && r.ctx[0].frame.fbmsk == 0x00FF00FF, "FRAME mixed");
    r.Write(0x4E, 0); CHECK(r.ctx[0].zbuf.zbp == 0 && !r.ctx[0].zbuf.zmsk, "ZBUF 0");
    r.Write(0x4F, ~0ull); CHECK(r.ctx[1].zbuf.zbp == 0x1FF * 8192 && r.ctx[1].zbuf.psm == 0xF && r.ctx[1].zbuf.zmsk, "ZBUF ones");
    r.Write(0x4E, 0x10ull | (0x1ull << 24) | (1ull << 32)); CHECK(r.ctx[0].zbuf.zbp == 0x10 * 8192 && r.ctx[0].zbuf.psm == 1 && r.ctx[0].zbuf.zmsk, "ZBUF mixed");
    // FBA / PABE / COLCLAMP / DTHE / SCANMSK
    r.Write(0x4A, 1); r.Write(0x4B, 0); CHECK(r.ctx[0].fba && !r.ctx[1].fba, "FBA per ctx"); r.Write(0x4A, 2); CHECK(!r.ctx[0].fba, "FBA bit 0 only");
    r.Write(0x49, 1); CHECK(r.env.pabe, "PABE 1"); r.Write(0x49, 2); CHECK(!r.env.pabe, "PABE bit 0"); r.Write(0x49, 3); CHECK(r.env.pabe, "PABE 3");
    r.Write(0x46, 0); CHECK(!r.env.colclamp, "COLCLAMP 0"); r.Write(0x46, 1); CHECK(r.env.colclamp, "COLCLAMP 1"); r.Write(0x46, 0xFE); CHECK(!r.env.colclamp, "COLCLAMP bit0");
    r.Write(0x45, 1); CHECK(r.env.dthe, "DTHE 1"); r.Write(0x45, 0); CHECK(!r.env.dthe, "DTHE 0"); r.Write(0x45, 0x101); CHECK(r.env.dthe, "DTHE bit0");
    r.Write(0x44, 0x7654321076543210ull); CHECK(r.env.dimx == 0x7654321076543210ull, "DIMX raw"); r.Write(0x44, 0); CHECK(r.env.dimx == 0, "DIMX 0"); r.Write(0x44, ~0ull); CHECK(r.env.dimx == ~0ull, "DIMX ones");
    r.Write(0x22, 0); CHECK(r.env.scanmsk == 0, "SCANMSK 0"); r.Write(0x22, 2); CHECK(r.env.scanmsk == 2, "SCANMSK 2"); r.Write(0x22, 7); CHECK(r.env.scanmsk == 3, "SCANMSK 2 bits");
    // TEXA / FOGCOL
    r.Write(0x3B, 0); CHECK(r.env.texa_ta0 == 0 && !r.env.texa_aem && r.env.texa_ta1 == 0, "TEXA 0");
    r.Write(0x3B, ~0ull); CHECK(r.env.texa_ta0 == 0xFF && r.env.texa_aem && r.env.texa_ta1 == 0xFF, "TEXA ones");
    r.Write(0x3B, 0x12ull | (1ull << 15) | (0x34ull << 32)); CHECK(r.env.texa_ta0 == 0x12 && r.env.texa_aem && r.env.texa_ta1 == 0x34, "TEXA mixed");
    r.Write(0x3D, 0); CHECK(r.env.fogcol_r == 0 && r.env.fogcol_b == 0, "FOGCOL 0");
    r.Write(0x3D, ~0ull); CHECK(r.env.fogcol_r == 0xFF && r.env.fogcol_g == 0xFF && r.env.fogcol_b == 0xFF, "FOGCOL ones");
    r.Write(0x3D, 0x11ull | (0x22ull << 8) | (0x33ull << 16)); CHECK(r.env.fogcol_r == 0x11 && r.env.fogcol_g == 0x22 && r.env.fogcol_b == 0x33, "FOGCOL mixed");
    // MIPTBP: raw; same value = no dirty
    r.dirty_regs = 0;
    r.Write(0x34, 0x55); CHECK(r.ctx[0].miptbp1 == 0x55 && (r.dirty_regs & GsRegisters::kDirtyTex1), "MIPTBP1 stored + dirty");
    r.dirty_regs = 0; r.Write(0x34, 0x55); CHECK(r.dirty_regs == 0, "same MIPTBP value: no dirty");
    r.Write(0x37, 0x99); CHECK(r.ctx[1].miptbp2 == 0x99, "MIPTBP2 ctx2"); r.Write(0x36, ~0ull); CHECK(r.ctx[0].miptbp2 == ~0ull, "MIPTBP2 ones");
    // PRIM / PRMODE / PRMODECONT
    r.Write(0x00, Prim(4, 1, 1, 1, 1, 1, 1, 1));
    CHECK(r.prim.type == 4 && r.prim.gouraud && r.prim.texture && r.prim.fog && r.prim.alpha_blend && r.prim.antialiasing && r.prim.use_uv && r.prim.context == 1, "PRIM all");
    r.Write(0x00, Prim(6, 0, 0, 0, 0, 0, 0, 0)); CHECK(r.prim.type == 6 && !r.prim.texture && r.prim.context == 0, "PRIM none");
    r.Write(0x00, Prim(2, 0, 1, 0, 1, 0, 1, 0)); CHECK(r.prim.type == 2 && r.prim.texture && r.prim.alpha_blend && r.prim.use_uv && !r.prim.gouraud, "PRIM mixed");
    r.Write(0x1B, Prim(0, 1, 0, 1, 0, 1, 0, 1)); CHECK(r.prim.texture && !r.prim.gouraud, "PRMODE ignored while PRMODECONT=1");
    r.Write(0x1A, 0); CHECK(!r.env.prmodecont && r.prim.gouraud && !r.prim.texture && r.prim.fog && !r.prim.alpha_blend && r.prim.antialiasing && !r.prim.use_uv && r.prim.context == 1 && r.prim.type == 2, "PRMODECONT 0 reloads from PRMODE, type kept");
    r.Write(0x00, Prim(3, 1, 1, 1, 1, 1, 1, 0)); CHECK(r.prim.type == 3 && !r.prim.texture && r.prim.context == 1, "PRIM attrs ignored while PRMODECONT=0");
    r.Write(0x1B, Prim(0, 0, 1, 0, 1, 0, 1, 0)); CHECK(r.prim.texture && r.prim.alpha_blend && r.prim.use_uv && r.prim.context == 0, "PRMODE live");
    r.Write(0x1A, 1); CHECK(r.env.prmodecont && r.prim.texture, "PRMODECONT 1 keeps the bits until the next PRIM");
    // RGBAQ / ST / UV / FOG register forms
    r.Write(0x01, Rgbaq(1, 2, 3, 4, 2.5f)); CHECK(r.attr.r == 1 && r.attr.g == 2 && r.attr.b == 3 && r.attr.a == 4 && r.attr.q == 2.5f, "RGBAQ");
    r.Write(0x01, Rgbaq(9, 9, 9, 9, 0.0f)); CHECK(r.attr.q == 2.5f && r.attr.r == 9, "RGBAQ Q=0 leaves Q");
    r.Write(0x01, ~0ull); CHECK(r.attr.a == 0xFF && std::isnan(r.attr.q), "RGBAQ ones (NaN Q stored: != 0)");
    r.Write(0x01, Rgbaq(0, 0, 0, 0, 1.0f));
    float s = 1.5f, t = -2.0f; uint32_t sb, tb; std::memcpy(&sb, &s, 4); std::memcpy(&tb, &t, 4);
    r.attr.uv_written_since_st = true;
    r.Write(0x02, (uint64_t)sb | ((uint64_t)tb << 32)); CHECK(r.attr.s == 1.5f && r.attr.t == -2.0f && !r.attr.uv_written_since_st && r.attr.q == 1.0f, "ST register: S T, no Q, clears uv flag");
    r.Write(0x02, 0); CHECK(r.attr.s == 0 && r.attr.t == 0, "ST 0");
    r.Write(0x03, 0x1234ull | (0x2345ull << 16)); CHECK(r.attr.u == 0x1234 && r.attr.v == 0x2345 && r.attr.uv_written_since_st, "UV 14-bit");
    r.Write(0x03, ~0ull); CHECK(r.attr.u == 0x3FFF && r.attr.v == 0x3FFF, "UV ones"); r.Write(0x03, 0); CHECK(r.attr.u == 0 && r.attr.v == 0, "UV 0");
    r.Write(0x0A, 0xABull << 56); CHECK(r.attr.fog_coef == 0xAB, "FOG register byte"); r.Write(0x0A, 0); CHECK(r.attr.fog_coef == 0, "FOG 0"); r.Write(0x0A, ~0ull); CHECK(r.attr.fog_coef == 0xFF, "FOG ones");
    // PACKED forms
    r.PackedRgbaq(0x11ull | (0x22ull << 32), 0x33ull | (0x44ull << 32)); CHECK(r.attr.r == 0x11 && r.attr.g == 0x22 && r.attr.b == 0x33 && r.attr.a == 0x44 && r.attr.q == 1.0f, "PACKED RGBAQ leaves Q");
    r.PackedSt((uint64_t)sb | ((uint64_t)tb << 32), 0); CHECK(r.attr.s == 1.5f && r.attr.t == -2.0f && r.attr.q == 1.0f, "PACKED ST with Q=0 keeps Q");
    float q = 0.25f; uint32_t qb; std::memcpy(&qb, &q, 4);
    r.PackedSt(0, qb); CHECK(r.attr.q == 0.25f && !r.attr.uv_written_since_st, "PACKED ST stores Q != 0");
    r.PackedUv(0x0F0Full | (0x2F2Full << 32)); CHECK(r.attr.u == 0x0F0F && r.attr.v == 0x2F2F && r.attr.uv_written_since_st, "PACKED UV");
    r.prim.fog = false; r.PackedFog(0x7Full << 36); CHECK(r.prim.fog, "PACKED FOG byte -> FGE (bug-compatible)");
    r.PackedFog(0); CHECK(!r.prim.fog, "PACKED FOG 0 -> FGE 0"); r.PackedFog(1ull << 36); CHECK(r.prim.fog, "PACKED FOG bit 36");
    // TEXFLUSH + unknown
    r.env.texture_dirty = false; CHECK(r.Write(0x3F, 0) && r.env.texture_dirty && r.env.texflush_seen, "TEXFLUSH");
    CHECK(!r.Write(0x1C, 0) && !r.Write(0x50, 0) && !r.Write(0x04, 0), "TEXCLUT / transfer / kick addresses not owned");
}

// Kick sequences: exact expected vertex lists per primitive type.
static void KickSeqs() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    auto last = [&]() -> const std::vector<VertexRec>& { return sink.draws.back().verts; };
    // Triangle: 3 kicks -> 3 verts; 4th starts a new triangle.
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1); KickAt(gs, 5, 5);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {0, 1}}), "triangle; 4th vertex waits in the queue");
    CHECK(gs.Kick().Queue().size() == 1, "queue keeps the pending vertex across EOP");
    // Strip: parity swap on odd triangles; EOP flush keeps parity (P6-FLUSH-TWINS).
    gs.WriteRegister(0x00, Prim(4, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 0, 1); KickAt(gs, 1, 1);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}}), "strip: even (q0 q1 q2), odd (q1 q0 q2)");
    CHECK(gs.Kick().StripCount() == 2, "EOP flush keeps strip parity");
    KickAt(gs, 0, 2);  // continues the strip: third triangle, even parity -> (q0 q1 q2) = (0,1)(1,1)(0,2)
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 1}, {1, 1}, {0, 2}}), "strip continues across EOP with even parity");
    KickAt(gs, 1, 2);  // fourth triangle: odd -> (q1 q0 q2) = (0,2)(1,1)(1,2)
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 2}, {1, 1}, {1, 2}}), "odd parity after EOP");
    // Strip restart via XYZ3: parity reset, vertex registered.
    KickAt(gs, 9, 9, false);
    CHECK(gs.Kick().StripCount() == 0, "XYZ3 resets parity");
    KickAt(gs, 9, 8); KickAt(gs, 8, 9);
    gs.KickBoundary();
    // The window still holds the two previous vertices: the restart only resets parity.
    CHECK(VertsXy(last(), {{1, 2}, {9, 9}, {9, 8}, {9, 8}, {9, 9}, {8, 9}}), "restarted strip: window kept, parity from 0");
    // Degenerate strip triangle: parity advances, nothing emitted.
    gs.WriteRegister(0x00, Prim(4, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 0, 0); KickAt(gs, 1, 1);
    CHECK(!gs.Kick().HasPending() && gs.Kick().StripCount() == 1, "degenerate culled, parity advanced");
    KickAt(gs, 2, 2);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{1, 1}, {0, 0}, {2, 2}}), "next triangle uses odd parity");
    // Fan: keep first + last.
    gs.WriteRegister(0x00, Prim(5, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 1, 1); KickAt(gs, 0, 1);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}}), "fan");
    // Line / LineStrip / Point
    gs.WriteRegister(0x00, Prim(1, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 2, 0); KickAt(gs, 3, 0);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {2, 0}, {3, 0}}), "lines");
    gs.WriteRegister(0x00, Prim(2, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 2, 0);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {1, 0}, {2, 0}}), "line strip");
    gs.WriteRegister(0x00, Prim(0, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 4, 4); KickAt(gs, 5, 5);
    gs.KickBoundary();
    CHECK(VertsXy(last(), {{4, 4}, {5, 5}}), "points");
    // ADC (PACKED XYZ2 with bit 111): triangle consumed without drawing.
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    gs.WritePacked(0x05, 0x10ull | (0x10ull << 32), 0);
    gs.WritePacked(0x05, 0x20ull | (0x10ull << 32), 0);
    gs.WritePacked(0x05, 0x10ull | (0x20ull << 32), 1ull << 47);
    CHECK(!gs.Kick().HasPending() && gs.Kick().Queue().empty(), "ADC consumes the triangle without drawing");
    // Fan ADC keeps first + last; sprite ADC clears.
    gs.WriteRegister(0x00, Prim(5, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 1, 0); KickAt(gs, 1, 1, false);
    CHECK(gs.Kick().Queue().size() == 2 && gs.Kick().Queue()[1].x == 1.0f && gs.Kick().Queue()[1].y == 1.0f, "fan ADC keeps first and last");
    // XYOFFSET subtraction, UV vs ST vertex attributes, fog byte from XYZF2.
    gs.WriteRegister(0x00, Prim(0, 0, 0, 0, 0, 0, 0, 0));
    gs.WriteRegister(0x18, (uint64_t)(100 * 16) | ((uint64_t)(200 * 16) << 32));
    float s = 0.5f, t = 0.75f; uint32_t sb, tb; std::memcpy(&sb, &s, 4); std::memcpy(&tb, &t, 4);
    gs.WriteRegister(0x02, (uint64_t)sb | ((uint64_t)tb << 32));
    gs.WriteRegister(0x01, Rgbaq(7, 8, 9, 10, 3.0f));
    gs.WriteRegister(0x04, Xyz(150 * 16, 250 * 16, 0x123456) | (0x7Eull << 56));  // XYZF2 with fog 0x7E
    gs.KickBoundary();
    const VertexRec& v = last()[0];
    CHECK(Feq(v.x, 50.0f) && Feq(v.y, 50.0f) && v.z == (float)0x123456 && v.fog == 0x7E && v.s == 0.5f && v.t == 0.75f &&
              v.q == 3.0f && v.r == 7 && v.a == 10 && v.draw_kick == 1,
          "XYOFFSET / STQ / fog / colour on the vertex");
    gs.WriteRegister(0x03, 0x30ull | (0x40ull << 16));  // UV after ST: s=u/16 t=v/16 q=1
    KickAt(gs, 100, 200);
    gs.KickBoundary();
    CHECK(Feq(last()[0].s, 3.0f) && Feq(last()[0].t, 4.0f) && last()[0].q == 1.0f && last()[0].uv_u == 0x30 && last()[0].x == 0.0f,
          "UV recency: s/t from UV, q = 1");
    CHECK(sink.draws.back().tex.fst, "fst recorded from the UV recency flag");
}

// Flat shading (GS User's Manual 3.4.1, PRIM.IIP = 0): the primitive takes the RGBA
// of the vertex whose kick completes it -- the LAST vertex (PCSX2: provoking last
// vertex) -- for triangles, strips (both parities), fans and lines; IIP = 1 keeps
// each vertex's own colour. The colour-grade mask chain's light-volume strips are
// flat strips whose triangles alternate G = 2 / G = 254 on their completing vertex.
static void FlatShading() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    auto last = [&]() -> const std::vector<VertexRec>& { return sink.draws.back().verts; };
    auto color = [&](float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        gs.WriteRegister(0x01, Rgbaq(r, g, b, a, 1.0f));
        KickAt(gs, x, y);
    };
    auto rgba_is = [](const VertexRec& v, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return v.r == r && v.g == g && v.b == b && v.a == a;
    };
    // Flat strip: triangle 0 completes at vertex 2 (G = 254), triangle 1 at vertex 3 (G = 2).
    gs.WriteRegister(0x00, Prim(4, 0, 0, 0, 1, 0, 0, 0));
    color(0, 0, 0, 2, 0, 30); color(1, 0, 0, 2, 0, 238); color(0, 1, 0, 254, 0, 164); color(1, 1, 0, 2, 0, 77);
    gs.KickBoundary();
    CHECK(last().size() == 6, "flat strip: two triangles");
    bool ok = true;
    for (int i = 0; i < 3; ++i) ok = ok && rgba_is(last()[i], 0, 254, 0, 164);
    for (int i = 3; i < 6; ++i) ok = ok && rgba_is(last()[i], 0, 2, 0, 77);
    CHECK(ok, "flat strip: every vertex carries the completing vertex's RGBA (both parities)");
    CHECK(VertsXy(last(), {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}}), "flat strip: positions untouched");
    // Gouraud strip: colours stay per vertex.
    gs.WriteRegister(0x00, Prim(4, 1, 0, 0, 1, 0, 0, 0));
    color(0, 0, 0, 2, 0, 30); color(1, 0, 0, 2, 0, 238); color(0, 1, 0, 254, 0, 164);
    gs.KickBoundary();
    CHECK(rgba_is(last()[0], 0, 2, 0, 30) && rgba_is(last()[1], 0, 2, 0, 238) && rgba_is(last()[2], 0, 254, 0, 164),
          "gouraud strip: per-vertex colours kept");
    // Flat triangle list, fan and line: the last vertex's colour.
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    color(0, 0, 1, 1, 1, 1); color(1, 0, 2, 2, 2, 2); color(0, 1, 3, 3, 3, 3);
    gs.KickBoundary();
    CHECK(rgba_is(last()[0], 3, 3, 3, 3) && rgba_is(last()[1], 3, 3, 3, 3) && rgba_is(last()[2], 3, 3, 3, 3), "flat triangle");
    gs.WriteRegister(0x00, Prim(5, 0, 0, 0, 0, 0, 0, 0));
    color(0, 0, 1, 1, 1, 1); color(1, 0, 2, 2, 2, 2); color(1, 1, 3, 3, 3, 3); color(0, 1, 4, 4, 4, 4);
    gs.KickBoundary();
    CHECK(last().size() == 6 && rgba_is(last()[0], 3, 3, 3, 3) && rgba_is(last()[2], 3, 3, 3, 3) &&
              rgba_is(last()[3], 4, 4, 4, 4) && rgba_is(last()[5], 4, 4, 4, 4),
          "flat fan: each triangle takes its completing vertex's colour");
    gs.WriteRegister(0x00, Prim(1, 0, 0, 0, 0, 0, 0, 0));
    color(0, 0, 1, 1, 1, 1); color(1, 0, 2, 2, 2, 2);
    gs.KickBoundary();
    CHECK(rgba_is(last()[0], 2, 2, 2, 2) && rgba_is(last()[1], 2, 2, 2, 2), "flat line");
}

// Façade bridge (G-C1): the register-op stream and the lifted draw-IR stream of
// the same packet drive GsState to identical DrawIr sequences.
// Invariant 40: TEX1.K (bits 43:32) is a SIGNED 12-bit s7.4 LOD parameter (GS User's
// Manual 3.4.12: LOD = (log2(1/|Q|) << L) + K); the draw carries its sign-extended
// value, so a title's negative bias (the Crash title's TEX1 0xffffffbb00000128 ->
// K = 0xfbb = -69 = -4.3125) selects the FINEST level, not a +251.7 bias that clamps
// every mip-mapped read at MXL (the coarsest authored level).
static void Inv40() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.WriteRegister(0x14, 0xffffffbb00000128ull);   // TEX1 ctx1: LCM 0 MXL 2 MMAG 1 MMIN 4 L 0 K 0xfbb
    gs.WriteRegister(0x00, Prim(3, 0, 1, 0, 0, 0, 0, 0));  // triangle, TME
    KickAt(gs, 1, 1); KickAt(gs, 2, 1); KickAt(gs, 1, 2);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 1 && sink.draws[0].tex_enabled, "one textured draw");
    const TexReadView& t = sink.draws[0].tex;
    CHECK(t.mxl == 2 && t.mmin == 4 && t.mmag && !t.lcm && t.l == 0, "TEX1 fields decoded");
    CHECK(t.k == -69, "K 0xfbb is -69 (s7.4 = -4.3125), sign-extended into the draw");
    gs.WriteRegister(0x14, 0x0000007b00000000ull);   // K = +0x07b = 123 (+7.6875): positive values unchanged
    KickAt(gs, 1, 1); KickAt(gs, 2, 1); KickAt(gs, 1, 2);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 2 && sink.draws[1].tex.k == 123, "a positive K passes through");
    gs.WriteRegister(0x14, 0x0000080000000000ull);   // K = 0x800: the most negative value, -128.0
    KickAt(gs, 1, 1); KickAt(gs, 2, 1); KickAt(gs, 1, 2);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 3 && sink.draws[2].tex.k == -2048, "K 0x800 is -2048 (-128.0)");
}

static void FacadeEmitsDrawIr() {
    // PACKED packet: PRE PRIM(tri strip, IIP, TME, FST), NLOOP 4, regs ST RGBAQ UV XYZ2 -> 4 vertices; EOP.
    std::vector<uint8_t> pkt;
    auto put64 = [&](uint64_t v) { for (int i = 0; i < 8; ++i) pkt.push_back((v >> (8 * i)) & 0xFF); };
    const uint64_t prim = Prim(4, 1, 1, 0, 1, 0, 1, 0);
    const uint64_t tag_lo = 4ull | (1ull << 15) | (1ull << 46) | (prim << 47) | (0ull << 58) | (4ull << 60);  // NLOOP=4 EOP PRE PRIM PACKED NREG=4
    const uint64_t tag_hi = 0x2ull | (0x1ull << 4) | (0x3ull << 8) | (0x5ull << 12);            // ST RGBAQ UV XYZ2
    put64(tag_lo); put64(tag_hi);
    for (uint32_t i = 0; i < 4; ++i) {
        float s = 0.1f * i, t = 0.2f * i, q = 1.0f + i; uint32_t sb, tb, qb;
        std::memcpy(&sb, &s, 4); std::memcpy(&tb, &t, 4); std::memcpy(&qb, &q, 4);
        put64((uint64_t)sb | ((uint64_t)tb << 32)); put64(qb);                           // ST
        put64((uint64_t)(10 + i) | ((uint64_t)(20 + i) << 32)); put64((uint64_t)(30 + i) | ((uint64_t)(0x80) << 32));  // RGBAQ
        put64((uint64_t)(i * 16) | ((uint64_t)(i * 32) << 32)); put64(0);                // UV
        put64((uint64_t)((100 + i * 10) * 16) | ((uint64_t)((50 + (i & 1) * 10) * 16) << 32)); put64((uint64_t)(0x1000 + i) | ((i == 3 ? 1ull : 0ull) << 47));  // XYZ2 (last ADC)
    }
    // Path A: DirectOps
    std::vector<DirectOps::Op> ops;
    CHECK(DirectOps::PacketToOps(pkt.data(), pkt.size(), ops), "ops convert");
    GsState a;
    CaptureSink sa;
    a.SetSink(&sa);
    a.WriteRegister(0x4C, 0x20ull | (10ull << 16));  // some frame state before the packet
    a.GifTagRead();
    for (const DirectOps::Op& op : ops) {
        if (op.reg == DirectOps::kOpFlush) a.KickBoundary();
        else if (op.reg == DirectOps::kOpSrcId) a.SetIdentity((uint32_t)(op.hi >> 32), op.lo, (uint16_t)(op.hi & 0x3FFF), (uint16_t)(op.hi >> 16), 0);
        else if (op.reg == DirectOps::kOpSrcName) {}
        else if (op.reg & DirectOps::kModeReglist) a.WriteRegister((uint8_t)(op.reg & 0xFF), op.lo);
        else a.WritePacked((uint8_t)op.reg, op.lo, op.hi);
    }
    // Path B: DrawIrLift with masked attribute stores + KickVertex
    DrawIrLift::Stream st;
    CHECK(DrawIrLift::PacketToDrawIR(pkt.data(), pkt.size(), st), "lift converts");
    GsState b;
    CaptureSink sb2;
    b.SetSink(&sb2);
    b.WriteRegister(0x4C, 0x20ull | (10ull << 16));
    b.GifTagRead();
    auto apply = [&](const DrawIrLift::Vert& v) {
        GsRegisters::Attr& at = b.Regs().attr;
        if (v.mask & DrawIrLift::kMaskST) { at.s = v.s; at.t = v.t; }
        if (v.mask & DrawIrLift::kMaskQ) at.q = v.q;
        if (v.mask & DrawIrLift::kMaskUV) { at.u = v.u; at.v = v.v; }
        if (v.mask & DrawIrLift::kMaskUVW) at.uv_written_since_st = (v.flags & DrawIrLift::kFlagUVWVal) != 0;
        if (v.mask & DrawIrLift::kMaskRGBA) { at.r = v.r; at.g = v.g; at.b = v.b; at.a = v.a; }
        if (v.mask & DrawIrLift::kMaskFOGF) b.Regs().prim.fog = (v.flags & DrawIrLift::kFlagFOGFVal) != 0;
    };
    for (const DrawIrLift::Item& it : st.items) {
        switch (it.kind) {
            case DrawIrLift::kState: b.WriteRegister((uint8_t)it.reg, it.lo); break;
            case DrawIrLift::kPrim: b.WriteRegister(0x00, it.lo); break;
            case DrawIrLift::kDrawRun:
                for (uint32_t k = 0; k < it.count; ++k) {
                    const DrawIrLift::Vert& v = st.verts[it.first + k];
                    apply(v);
                    b.KickVertex(v.x, v.y, v.z, v.f, (v.flags & DrawIrLift::kFlagDraw) != 0);
                }
                break;
            case DrawIrLift::kAttr: apply(st.verts[it.first]); break;
            case DrawIrLift::kFlush: b.KickBoundary(); break;
            default: break;
        }
    }
    CHECK(sa.draws.size() == 1 && sb2.draws.size() == 1, "one draw each");
    if (sa.draws.size() == 1 && sb2.draws.size() == 1) {
        const DrawIr& x = sa.draws[0];
        const DrawIr& y = sb2.draws[0];
        DrawRec rx = x.ToDrawRec(), ry = y.ToDrawRec();
        CHECK(std::memcmp(&rx, &ry, sizeof(DrawRec)) == 0, "DrawRec identical across the two paths");
        CHECK(x.verts.size() == y.verts.size() && x.verts.size() == 3 &&
                  std::memcmp(x.verts.data(), y.verts.data(), x.verts.size() * sizeof(VertexRec)) == 0,
              "vertex arrays identical (3 verts: two kicks form one strip triangle... plus the ADC restart)");
        CHECK(x.prim_type == 4 && x.gouraud && x.tex_enabled && x.use_fst && x.alpha_blend && x.raster.frame.fbp == 0x20 * 8192 &&
                  x.raster.frame.fbw == 640,
              "PRIM bits and frame state in the DrawIr");
        CHECK(x.verts[0].uv_u == 0 && x.verts[1].uv_u == 16 && x.verts[2].uv_u == 32 && Feq(x.verts[2].s, 2.0f) && x.verts[2].q == 1.0f,
              "UV recency won over ST (FST packet)");
        CHECK(x.verts[0].r == 10 && x.verts[2].b == 32 && x.verts[0].a == 0x80, "PACKED RGBAQ bytes");
        CHECK(x.raster.dthe == 0 && x.tex.mip_tbp[0] == 0, "fields the record lacks default to zero");
    }
}

// Privileged registers: (a) DISPFB/DISPLAY/BGCOLOR jobs carry the LIVE upload
// epoch (a TRXDIR stamp after the last draw flush counts); (b) CSR.RESET zeroes
// the register file but keeps the DISPLAY window change latches, so rewriting
// the same geometry after a reset emits no second SetWindow job.
static void PrivRegs() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    auto jobs_of = [&sink](JobKind k) {
        size_t n = 0;
        for (const JobRec& j : sink.jobs) n += (j.type == (uint32_t)k);
        return n;
    };
    // A draw flushes at EOP with epoch 0 (nothing written yet).
    gs.WriteRegister(0x00, Prim(3, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 0, 0); KickAt(gs, 10, 0); KickAt(gs, 0, 10);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 1 && sink.draws[0].vram_epoch == 0 && gs.Vram().UploadEpoch() == 0, "draw flushed at epoch 0");
    // TRXDIR host->local 4x1 CT32 at block 0x400: the destination stamp bumps the live epoch.
    gs.WriteRegister(0x50, (uint64_t)0x400 << 32 | (uint64_t)1 << 48);
    gs.WriteRegister(0x51, 0);
    gs.WriteRegister(0x52, 4 | ((uint64_t)1 << 32));
    gs.WriteRegister(0x53, 0);
    CHECK(gs.Vram().UploadEpoch() == 1, "TRXDIR stamped the destination (live epoch 1)");
    // DISPFB1: FBP 0-8 (2048 words) FBW 9-14 (64 px) PSM 15-19.
    const uint64_t dispfb = (uint64_t)0x10 | ((uint64_t)10 << 9) | ((uint64_t)0 << 15);
    gs.WritePrivileged(0x12000070, dispfb);
    CHECK(jobs_of(JobKind::SetFrontBuffer) == 1, "DISPFB emits SetFrontBuffer");
    CHECK(sink.jobs.back().vram_epoch == gs.Vram().UploadEpoch() && sink.jobs.back().vram_epoch == 1,
          "priv job stamped with the LIVE upload epoch, not the last flush's");
    CHECK(sink.jobs.back().arg1 == 0x10 * 8192 && sink.jobs.back().arg2 == 640 && sink.jobs.back().arg3 == 0, "SetFrontBuffer args");
    // Another stamp with no draw in between: the next priv job follows it.
    gs.WriteRegister(0x53, 0);
    CHECK(gs.Vram().UploadEpoch() == 2, "second TRXDIR: live epoch 2");
    gs.WritePrivileged(0x120000E0, 0x00332211);  // BGCOLOR
    CHECK(sink.jobs.back().type == (uint32_t)JobKind::SetClearColor && sink.jobs.back().vram_epoch == 2,
          "BGCOLOR job at epoch 2");
    // (b) DISPLAY1 = DX 0 DY 0 MAGH 3 MAGV 0 DW 2559 DH 447 -> 640x448 window.
    const uint64_t display = ((uint64_t)3 << 23) | ((uint64_t)2559 << 32) | ((uint64_t)447 << 44);
    gs.WritePrivileged(0x12000080, display);
    CHECK(jobs_of(JobKind::SetWindow) == 1, "DISPLAY emits SetWindow");
    CHECK(sink.jobs.back().arg1 == 1 && sink.jobs.back().arg2 == ((640u << 16) | 640u) &&
              sink.jobs.back().arg3 == ((448u << 16) | 448u) && sink.jobs.back().arg4 == ((2560u << 16) | 448u),
          "SetWindow args (circuit, packed w, packed h, vck)");
    gs.WritePrivileged(0x12000080, display);
    CHECK(jobs_of(JobKind::SetWindow) == 1, "same DISPLAY value: idempotent");
    gs.WritePrivileged(0x12001000, 1u << 9);  // CSR RESET
    CHECK(gs.ReadPrivileged(0x12001000) == 0 && gs.ReadPrivileged(0x12000080) == 0 && gs.ReadPrivileged(0x12000070) == 0,
          "RESET zeroes the register file and CSR");
    gs.WritePrivileged(0x12000080, display);
    CHECK(jobs_of(JobKind::SetWindow) == 1, "DISPLAY rewrite of the same geometry after RESET: window latch survived, no job");
    gs.WritePrivileged(0x12000070, dispfb);
    CHECK(jobs_of(JobKind::SetFrontBuffer) == 2, "DISPFB after RESET re-emits (its latch IS the register value)");
    const uint64_t display2 = ((uint64_t)3 << 23) | ((uint64_t)2559 << 32) | ((uint64_t)223 << 44);
    gs.WritePrivileged(0x12000080, display2);
    CHECK(jobs_of(JobKind::SetWindow) == 2 && sink.jobs.back().arg3 == ((224u << 16) | 224u), "a new geometry still emits");
    // (c) SMODE2 interlaced FRAME mode (INT=1 FFMD=1): every buffer line is read in each
    // field, so the 448-line raster scans a 224-line buffer -- the SetWindow job keeps the
    // 448 window and carries content 224; the mode change itself re-emits the latched
    // DISPLAY; FIELD mode (FFMD=0) restores 448/448.
    gs.WritePrivileged(0x12000080, display);   // 640x448 again (frame mode off): window 448, content 448
    CHECK(jobs_of(JobKind::SetWindow) == 3 && sink.jobs.back().arg3 == ((448u << 16) | 448u), "field mode: content = raster");
    gs.WritePrivileged(0x12000020, 3);          // SMODE2 INT | FFMD
    CHECK(jobs_of(JobKind::SetWindow) == 4 && sink.jobs.back().arg1 == 1 &&
              sink.jobs.back().arg2 == ((640u << 16) | 640u) && sink.jobs.back().arg3 == ((448u << 16) | 224u),
          "frame mode: SMODE2 re-emits the window with half the content lines");
    gs.WritePrivileged(0x12000020, 3);
    CHECK(jobs_of(JobKind::SetWindow) == 4, "same SMODE2: no re-emit");
    gs.WritePrivileged(0x12000080, display2);   // DH 223 under frame mode: window 224, content 112
    CHECK(jobs_of(JobKind::SetWindow) == 5 && sink.jobs.back().arg3 == ((224u << 16) | 112u), "frame mode applies to a new DISPLAY too");
    gs.WritePrivileged(0x12000020, 1);          // back to FIELD mode
    CHECK(jobs_of(JobKind::SetWindow) == 6 && sink.jobs.back().arg3 == ((224u << 16) | 224u), "field mode again: content = raster");
    CHECK(gs.ReadPrivileged(0x12000020) == 1, "SMODE2 reads back");
    // CSR: write-1-to-clear of the event bits.
    gs.WriteRegister(0x61, 0);  // FINISH
    CHECK((gs.ReadPrivileged(0x12001000) & GsPrivRegs::kCsrFinish) != 0, "FINISH sets CSR.FINISH");
    gs.WritePrivileged(0x12001000, GsPrivRegs::kCsrFinish);
    CHECK((gs.ReadPrivileged(0x12001000) & GsPrivRegs::kCsrFinish) == 0, "write-1 clears it");
    // (c) the job deferrer: a display-register write stores the register at once but
    // hands the job's push to the deferrer; the job (seq / epoch) is stamped when the
    // deferred push runs; without a deferrer the push is in place again.
    {
        std::vector<std::function<void()>> deferred;
        gs.Priv().SetJobDeferrer([&deferred](std::function<void()> push) { deferred.push_back(std::move(push)); });
        const size_t jobs_before = sink.jobs.size();
        const uint64_t dispfb2 = (uint64_t)0x20 | ((uint64_t)10 << 9);
        gs.WritePrivileged(0x12000070, dispfb2);
        CHECK(gs.ReadPrivileged(0x12000070) == dispfb2, "deferred: the register stores at once");
        CHECK(sink.jobs.size() == jobs_before && deferred.size() == 1, "deferred: the job waits in the deferrer");
        const uint32_t seq_before = gs.Seq();
        deferred.back()();
        CHECK(sink.jobs.size() == jobs_before + 1 && sink.jobs.back().type == (uint32_t)JobKind::SetFrontBuffer &&
                  sink.jobs.back().arg1 == 0x20u * 8192u && sink.jobs.back().seq == seq_before,
              "deferred: the push lands the job, stamped at push time");
        gs.Priv().SetJobDeferrer(nullptr);
        gs.WritePrivileged(0x12000070, dispfb);
        CHECK(sink.jobs.size() == jobs_before + 2, "no deferrer: the push is in place");
    }
}

static void LibraryDrawSubmission() {
    GsState gs;
    CaptureSink sink;
    gs.SetSink(&sink);
    gs.SetSrcFrame(17);
    gs.SetIdentity(41, 99, 2, 3, 17);
    gs.WriteRegister(0x00, Prim(0, 0, 0, 0, 0, 0, 0, 0));
    KickAt(gs, 1, 2);
    gs.Flush().Flush(DrawFlush::FlushKind::StateChange);
    CHECK(sink.draws.size() == 1, "preceding legacy draw flushed");
    DrawIr draw;
    draw.seq = 999;
    draw.src_frame = 999;
    draw.prim_type = 3;
    draw.vertex_count = 3;
    draw.verts.resize(3);
    draw.verts[0].x = 13;
    draw.raster.frame.fbp = 0x10000;
    gs.SubmitDrawIr(draw);
    KickAt(gs, 3, 4);
    gs.KickBoundary();
    CHECK(sink.draws.size() == 3, "legacy, library, legacy draws share the publisher");
    if (sink.draws.size() == 3) {
        for (size_t index = 0; index < sink.draws.size(); ++index) {
            CHECK(sink.draws[index].seq == index, "common monotonic sequence");
            CHECK(sink.draws[index].src_frame == 17, "current frame stamped at delivery");
        }
        CHECK(sink.draws[1].raster.frame.fbp == 0x10000 && sink.draws[1].verts[0].x == 13,
              "explicit state and geometry preserved");
        CHECK(sink.draws[1].vram_epoch == gs.Vram().UploadEpoch(), "current resource epoch stamped");
        CHECK(sink.draws[1].identity.hle_handler == 0 &&
              sink.draws[2].identity.hle_handler == 41, "library draw preserves both producer identities");
        CHECK(sink.draws[2].prim_type == 0 && sink.draws[2].raster.frame.fbp == 0,
              "library submission does not mutate retained primitive or target state");
    }
}

int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "all";
    if (c == "inv01" || c == "all") Inv01();
    if (c == "inv02" || c == "all") Inv02();
    if (c == "inv02b" || c == "all") Inv02b();
    if (c == "inv28" || c == "all") Inv28();
    if (c == "inv32" || c == "all") Inv32();
    if (c == "inv38" || c == "all") Inv38();
    if (c == "inv40" || c == "all") Inv40();
    if (c == "decode" || c == "all") DecodeRegs();
    if (c == "kick" || c == "all") KickSeqs();
    if (c == "flat" || c == "all") FlatShading();
    if (c == "facade" || c == "all") {
        FacadeEmitsDrawIr();
        LibraryDrawSubmission();
    }
    if (c == "priv" || c == "all") PrivRegs();
    std::printf("test_gs_state %s: %d checks, %d failures\n", c.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
