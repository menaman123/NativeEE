// tests/gif/test_gs_state_sink.cpp -- GsStateSink + GsSinkFanout: synthetic GIF
// streams through GifParser(fanout{GsStateSink, RegisterOpStream}) must land in
// the GsState façade exactly as the recorder records them. Usage:
//   test_gs_state_sink <case>   (ad | packed | packedflat | image | eop | fanout | path3 | identity | attribution | all)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/gif/hle/gs_sink_fanout.h"
#include "engine/gif/hle/gs_state_sink.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/state/gs_state.h"
#include "tests/lib/capture_sink.h"

using ps2r::gs::CaptureSink;
using ps2r::gs::CountingDrawIrSink;
using ps2r::gs::GsState;
using ps2r::gs::JobKind;
using ps2r::gs::Psm;
using ps2r::gs::PsmLayout;

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

using Bytes = std::vector<uint8_t>;
constexpr int kPacked = 0, kImage = 2;

void put_u64(Bytes& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// A GIFtag: NLOOP 0-14, EOP 15, PRE 46, PRIM 47-57, FLG 58-59, NREG 60-63; descriptors in the high word.
void put_tag(Bytes& v, uint16_t nloop, bool eop, bool pre, uint16_t prim, int flg, const std::vector<uint8_t>& regs) {
    uint64_t lo = static_cast<uint64_t>(nloop & 0x7FFF) | (static_cast<uint64_t>(eop ? 1 : 0) << 15) |
                  (static_cast<uint64_t>(pre ? 1 : 0) << 46) | (static_cast<uint64_t>(prim & 0x7FF) << 47) |
                  (static_cast<uint64_t>(flg & 3) << 58) | (static_cast<uint64_t>(regs.size() & 0xF) << 60);
    uint64_t hi = 0;
    for (size_t i = 0; i < regs.size() && i < 16; ++i) hi |= static_cast<uint64_t>(regs[i] & 0xF) << (4 * i);
    put_u64(v, lo);
    put_u64(v, hi);
}

void put_ad(Bytes& v, uint8_t reg, uint64_t value) {  // PACKED A+D: data lo, address hi
    put_u64(v, value);
    put_u64(v, reg);
}

uint32_t bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// The rig (data only): parser -> fanout{façade sink, recorder}; façade -> counting -> capture.
struct Rig {
    GsState gs;
    CaptureSink capture;
    CountingDrawIrSink counting{&capture};
    GsStateSink state_sink{gs};
    RegisterOpStream ops;
    GsSinkFanout fanout{&state_sink, &ops};
    GifRegs regs;
    GifParser parser{fanout, regs};
};

void Init(Rig& r) { r.gs.SetSink(&r.counting); }
void Feed(Rig& r, const Bytes& v, GifPath path = GifPath::Path2) { r.parser.ReceiveData(path, v.data(), v.size()); }

size_t OpsOfKind(const RegisterOpStream& s, RegisterOpKind k) {
    size_t n = 0;
    for (const RegisterOp& op : s.Ops()) n += (op.kind == static_cast<uint8_t>(k));
    return n;
}

}  // namespace

// A+D: every register write the recorder records reaches the façade's register
// file (FRAME_1 / SCISSOR_1 / TEST_1 / PRIM decoded), the sink's count equals
// the recorder's Reg op count, and the GIFtag reset Q to 1.
static void AdWrites() {
    Rig r;
    Init(r);
    r.gs.Regs().attr.q = 7.0f;  // a stale Q the tag must reset
    Bytes v;
    put_tag(v, 4, true, false, 0, kPacked, {0x0E});
    put_ad(v, 0x4C, (uint64_t)0x20 | ((uint64_t)10 << 16) | ((uint64_t)0 << 24));      // FRAME_1: FBP 0x20 FBW 10 PSM 0
    put_ad(v, 0x40, (uint64_t)0 | ((uint64_t)639 << 16) | ((uint64_t)0 << 32) | ((uint64_t)447 << 48));  // SCISSOR_1
    put_ad(v, 0x47, 1 | (7 << 1) | (0x80 << 4));                                       // TEST_1: ATE ATST=7 AREF 0x80
    put_ad(v, 0x00, 3 | (1 << 3));                                                     // PRIM: triangle, IIP
    Feed(r, v);
    CHECK(r.state_sink.Tags() == 1 && OpsOfKind(r.ops, RegisterOpKind::Tag) == 1, "one tag on both sinks");
    CHECK(r.state_sink.RegisterWrites() == 4, "four A+D register writes reached the façade");
    CHECK(r.state_sink.TagsOn(GifPath::Path2) == 1 && r.state_sink.RegisterWritesOn(GifPath::Path2) == 4 &&
              r.state_sink.TagsOn(GifPath::Path1) == 0 && r.state_sink.TagsOn(GifPath::Path3) == 0,
          "per-path census: the PATH2 tag and its writes, nothing on PATH1/PATH3");
    CHECK(r.parser.Receives(GifPath::Path2) == 1 && r.parser.ReceivedBytes(GifPath::Path2) == v.size() &&
              r.parser.Receives(GifPath::Path1) == 0 && r.parser.Receives(GifPath::Path3) == 0,
          "parse census: one PATH2 delivery of the packet's bytes");
    CHECK(OpsOfKind(r.ops, RegisterOpKind::Reg) == r.state_sink.RegisterWrites(), "recorder Reg ops == façade register writes");
    CHECK(r.gs.Regs().attr.q == 1.0f, "GIFtag reset Q to 1.0");
    const auto& c0 = r.gs.Regs().ctx[0];
    CHECK(c0.frame.fbp == 0x20 * 8192 && c0.frame.fbw == 640 && c0.frame.psm == 0, "FRAME_1 decoded (fbp bytes, fbw pixels)");
    CHECK(c0.scissor.x1 == 639 && c0.scissor.y1 == 447, "SCISSOR_1 decoded");
    CHECK(c0.test.ate && c0.test.atst == 7 && c0.test.aref == 0x80, "TEST_1 decoded");
    CHECK(r.gs.Regs().prim.type == 3 && r.gs.Regs().prim.gouraud, "PRIM decoded");
    CHECK(r.state_sink.Eops() == 1 && OpsOfKind(r.ops, RegisterOpKind::Eop) == 1, "EOP on both sinks");
    CHECK(r.counting.Draws() == 0, "no vertices: EOP flushes nothing");
}

// The PackedItems packet: PRE PRIM = `prim` (triangle 3, optionally | IIP 8), FST off,
// TME off; descriptors RGBAQ ST XYZ2 (strip3 lane shape); three vertices whose RGBAQ
// is (10+i, 20+i, 30+i, 0x80), ST (0.25,0.5,Q2) (0.75,0.5,Q0) (0.25,1.0,Q4), XY 12.4
// 0 / 10 / 10 pixels, Z 0x100, ADC 0.
static void PutPackedTriangle(Bytes& v, uint16_t prim) {
    put_tag(v, 3, true, true, prim, kPacked, {0x01, 0x02, 0x05});
    const uint8_t rgba[3][4] = {{10, 20, 30, 0x80}, {11, 21, 31, 0x80}, {12, 22, 32, 0x80}};
    const float st[3][3] = {{0.25f, 0.5f, 2.0f}, {0.75f, 0.5f, 0.0f}, {0.25f, 1.0f, 4.0f}};
    const uint32_t xy[3][2] = {{0, 0}, {160, 0}, {0, 160}};  // 12.4: 0, 10, 10 pixels
    for (int i = 0; i < 3; ++i) {
        put_u64(v, (uint64_t)rgba[i][0] | ((uint64_t)rgba[i][1] << 32));
        put_u64(v, (uint64_t)rgba[i][2] | ((uint64_t)rgba[i][3] << 32));
        put_u64(v, (uint64_t)bits(st[i][0]) | ((uint64_t)bits(st[i][1]) << 32));
        put_u64(v, bits(st[i][2]));
        put_u64(v, (uint64_t)xy[i][0] | ((uint64_t)xy[i][1] << 32));
        put_u64(v, (uint64_t)0x100);  // Z 0x100, ADC 0
    }
}

// PACKED decoded items: RGBAQ bytes, ST with Q (non-zero applied), UV latch, FOG
// as PRIM.FGE, then an XYZ2 strip of three -> one triangle draw whose vertices
// carry the colour / ST the façade re-packed from the sink's decoded values.
// PRIM.IIP = 1 (Gouraud) so the per-vertex colours survive the kick: with IIP = 0
// the GS flat-shades the triangle with the completing vertex's colour (GS User's
// Manual 3.4.1; PackedFlat below pins that arm).
static void PackedItems() {
    Rig r;
    Init(r);
    Bytes v;
    // PRE PRIM = triangle (3) | IIP (8), FST off, TME off.
    PutPackedTriangle(v, 3 | 8);
    Feed(r, v);
    CHECK(r.gs.Regs().prim.type == 3 && r.gs.Regs().prim.gouraud, "PRE PRIM decoded: triangle, IIP");
    CHECK(r.state_sink.RegisterWrites() == 1, "PRE wrote PRIM through WriteRegister");
    CHECK(r.state_sink.PackedWrites() == 6 && r.state_sink.Kicks() == 3, "6 packed items + 3 kicks");
    CHECK(r.state_sink.KicksOn(GifPath::Path2) == 3 && r.state_sink.KicksOn(GifPath::Path1) == 0, "the 3 kicks counted on PATH2");
    CHECK(OpsOfKind(r.ops, RegisterOpKind::Rgbaq) == 3 && OpsOfKind(r.ops, RegisterOpKind::St) == 3 &&
              OpsOfKind(r.ops, RegisterOpKind::Kick) == 3,
          "recorder saw the same item multiplicities");
    CHECK(r.counting.Draws() == 1 && r.capture.draws.size() == 1, "EOP flushed one triangle");
    if (r.capture.draws.size() == 1) {
        const auto& d = r.capture.draws[0];
        CHECK(d.prim_type == 3 && d.verts.size() == 3, "triangle with three vertices");
        if (d.verts.size() == 3) {
            CHECK(d.verts[0].r == 10 && d.verts[0].g == 20 && d.verts[0].b == 30 && d.verts[0].a == 0x80, "RGBAQ bytes re-packed");
            CHECK(d.verts[2].r == 12 && d.verts[2].b == 32, "third vertex colour");
            CHECK(d.verts[0].s == 0.25f && d.verts[0].t == 0.5f && d.verts[0].q == 2.0f, "ST with Q applied");
            CHECK(d.verts[1].q == 2.0f, "Q == 0 in the second ST leaves the latched Q");
            CHECK(d.verts[2].q == 4.0f, "third ST Q applied");
            CHECK(d.verts[1].x == 10.0f && d.verts[2].y == 10.0f, "12.4 X/Y kicked as pixels");
        }
    }
    // UV then FOG through the generic PACKED path: the UV latch flips, FOG sets PRIM.FGE.
    Bytes w;
    put_tag(w, 1, true, false, 0, kPacked, {0x03, 0x0A});
    put_u64(w, (uint64_t)0x1234 | ((uint64_t)0x0ABC << 32));  // UV
    put_u64(w, 0);
    put_u64(w, 0);                                              // FOG lo
    put_u64(w, (uint64_t)0x5A << 36);                           // FOG hi: F at 36-43
    Feed(r, w);
    CHECK(r.gs.Regs().attr.u == 0x1234 && r.gs.Regs().attr.v == 0x0ABC && r.gs.Regs().attr.uv_written_since_st, "PACKED UV landed (latch set)");
    CHECK(r.gs.Regs().prim.fog, "PACKED FOG byte -> PRIM.FGE");
    CHECK(r.state_sink.PackedWrites() == 8, "two more packed items");
}

// The same packet with PRIM.IIP = 0: flat shading. The GS applies the colour of the
// vertex whose kick completes the primitive to the whole primitive (GS User's Manual
// 3.4.1; PCSX2 HandleProvokingVertexFirst: "PS2 is provoking last vertex") -- the
// engine's VertexKick::PushPrimitive (E192) copies the LAST vertex's RGBA onto the
// triangle's three vertices; ST / Q / XY stay per vertex.
static void PackedFlat() {
    Rig r;
    Init(r);
    Bytes v;
    PutPackedTriangle(v, 3);
    Feed(r, v);
    CHECK(r.gs.Regs().prim.type == 3 && !r.gs.Regs().prim.gouraud, "PRE PRIM decoded: triangle, flat");
    CHECK(r.state_sink.PackedWrites() == 6 && r.state_sink.Kicks() == 3, "6 packed items + 3 kicks");
    CHECK(OpsOfKind(r.ops, RegisterOpKind::Rgbaq) == 3, "recorder still saw three RGBAQ items");
    CHECK(r.counting.Draws() == 1 && r.capture.draws.size() == 1, "EOP flushed one triangle");
    if (r.capture.draws.size() == 1) {
        const auto& d = r.capture.draws[0];
        CHECK(d.prim_type == 3 && d.verts.size() == 3, "triangle with three vertices");
        if (d.verts.size() == 3) {
            bool flat = true;
            for (int i = 0; i < 3; ++i)
                flat = flat && d.verts[i].r == 12 && d.verts[i].g == 22 && d.verts[i].b == 32 && d.verts[i].a == 0x80;
            CHECK(flat, "IIP=0: every vertex carries the completing (last) vertex's RGBA");
            CHECK(d.verts[0].s == 0.25f && d.verts[0].t == 0.5f && d.verts[0].q == 2.0f && d.verts[2].q == 4.0f,
                  "flat shading leaves ST / Q per vertex");
            CHECK(d.verts[1].x == 10.0f && d.verts[2].y == 10.0f, "flat shading leaves positions untouched");
        }
    }
}

// IMAGE: BITBLTBUF/TRXPOS/TRXREG/TRXDIR via A+D, then an IMAGE tag whose
// quadwords land in the façade's VRAM as host->local CT32 pixels (observed
// through VramImage::ReadView), one upload epoch.
static void ImageUpload() {
    Rig r;
    Init(r);
    Bytes v;
    put_tag(v, 4, false, false, 0, kPacked, {0x0E});
    put_ad(v, 0x50, ((uint64_t)0x00 << 32) | ((uint64_t)1 << 48));  // BITBLTBUF: DBP 0 DBW 1 (64 px) DPSM CT32
    put_ad(v, 0x51, 0);                                              // TRXPOS 0,0
    put_ad(v, 0x52, 8 | ((uint64_t)1 << 32));                        // TRXREG 8x1
    put_ad(v, 0x53, 0);                                              // TRXDIR host->local
    put_tag(v, 2, true, false, 0, kImage, {});                       // 2 quadwords = 8 CT32 pixels
    const uint32_t px[8] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555, 0x66666666, 0x77777777, 0x88888888};
    for (int i = 0; i < 8; i += 2) put_u64(v, (uint64_t)px[i] | ((uint64_t)px[i + 1] << 32));
    Feed(r, v, GifPath::Path3);
    CHECK(r.state_sink.ImageQwords() == 2 && OpsOfKind(r.ops, RegisterOpKind::Image) == 2, "two IMAGE quadwords on both sinks");
    CHECK(r.state_sink.ImageQwordsOn(GifPath::Path3) == 2 && r.state_sink.TagsOn(GifPath::Path3) == 2 &&
              r.parser.Receives(GifPath::Path3) == 1 && r.parser.ReceivedBytes(GifPath::Path3) == v.size(),
          "per-path census: both tags and the IMAGE quadwords on PATH3; one PATH3 delivery");
    CHECK(r.state_sink.RegisterWrites() == 4, "four transfer register writes");
    // The pixels sit at their PSMCT32 swizzled addresses (block 0, buffer width 64).
    bool same = true;
    for (uint32_t x = 0; x < 8; ++x) {
        uint32_t w;
        r.gs.Vram().ReadView(PsmLayout::PixelByteAddress(Psm::CT32, x, 0, 0, 1), 4, reinterpret_cast<uint8_t*>(&w));
        same = same && (w == px[x]);
    }
    CHECK(same, "the 8 CT32 pixels are in VRAM at block 0 (host->local)");
    CHECK(r.gs.Xfer().Stats().completed == 1 && !r.gs.Xfer().State().active, "the 8x1 transfer completed");
    // Epoch 1 = the TRXDIR destination stamp, 2 = the completion's settle (OLD's
    // per-completion bump, pinned by GsTransferCursor).
    CHECK(r.gs.Vram().UploadEpoch() == 2, "upload epoch: stamp + completion settle");
    CHECK(r.counting.Jobs() >= 1, "the upload emitted a job (InvalidateTextures / UploadVRAMTexture)");
}

// EOP is the one GifEop flush: a strip spanning two packets keeps its parity
// (the second packet's single kick completes a triangle instead of restarting),
// and a packet with no vertices flushes nothing.
static void EopFlush() {
    Rig r;
    Init(r);
    Bytes a;
    put_tag(a, 2, true, true, 4 /* triangle strip */, kPacked, {0x05});
    put_u64(a, 0); put_u64(a, 0x100);                                        // (0,0)
    put_u64(a, (uint64_t)160 | ((uint64_t)0 << 32)); put_u64(a, 0x100);      // (10,0)
    Feed(r, a);
    CHECK(r.counting.Draws() == 0, "two strip vertices: nothing to flush yet");
    Bytes b;
    put_tag(b, 2, true, false, 0, kPacked, {0x05});
    put_u64(b, (uint64_t)0 | ((uint64_t)160 << 32)); put_u64(b, 0x100);     // (0,10) completes the triangle
    put_u64(b, (uint64_t)160 | ((uint64_t)160 << 32)); put_u64(b, 0x100);   // (10,10) a second one
    Feed(r, b);
    CHECK(r.state_sink.Eops() == 2, "two EOPs");
    CHECK(r.counting.Draws() == 1, "the second EOP flushed the strip once (one draw)");
    // Two strip triangles expanded to 3 vertices each; the second is odd parity
    // (q1 q0 q2), which it can only be if the first EOP did not reset the parity.
    const bool shape = r.capture.draws.size() == 1 && r.capture.draws[0].verts.size() == 6;
    CHECK(shape, "the strip's two triangles in one draw (6 vertices)");
    if (shape) {
        const auto& vt = r.capture.draws[0].verts;
        CHECK(vt[0].x == 0 && vt[0].y == 0 && vt[1].x == 10 && vt[1].y == 0 && vt[2].x == 0 && vt[2].y == 10, "even triangle (q0 q1 q2)");
        CHECK(vt[3].x == 0 && vt[3].y == 10 && vt[4].x == 10 && vt[4].y == 0 && vt[5].x == 10 && vt[5].y == 10,
              "odd triangle (q1 q0 q2): parity kept across the EOP");
    }
    Bytes c;
    put_tag(c, 0, true, false, 0, kPacked, {0x0E});  // NLOOP 0 + EOP
    Feed(r, c);
    CHECK(r.state_sink.Eops() == 3 && r.counting.Draws() == 1, "an empty EOP flushes nothing");
}

// The fan-out forwards every call to every member in order, and a member added
// later sees only what follows.
static void Fanout() {
    RegisterOpStream a, b;
    GsSinkFanout f{&a, nullptr, &b};
    CHECK(f.Size() == 2, "null members are skipped");
    GifTag tag{};
    tag.Parse(0x0001, 0);
    f.BeginTag(tag, GifPath::Path1);
    f.WriteRegister(0x4C, 0x1234);
    f.PackedRgbaq(1, 2, 3, 4);
    f.PackedSt(0.5f, 0.25f, 1.0f);
    f.PackedUv(5, 6);
    f.PackedFog(7);
    f.KickVertex(1.0f, 2.0f, 3.0f, 8, true);
    f.ImageData(9, 10);
    f.EndOfPacket();
    f.FinishPath3();
    CHECK(a.Count() == 10 && b.Count() == 10, "all ten calls reached both members");
    CHECK(a.Hash() == b.Hash() && a.Hash() != RegisterOpStream().Hash(), "identical streams hash identically");
    RegisterOpStream c;
    f.Add(&c);
    f.EndOfPacket();
    CHECK(a.Count() == 11 && c.Count() == 1, "a late member sees only later calls");
}

// FinishPath3 flushes a non-empty draw buffer as a state-change flush (the
// strip restarts) and is a no-op when nothing is queued.
static void Path3Finish() {
    Rig r;
    Init(r);
    Bytes a;
    put_tag(a, 3, false, true, 4 /* strip */, kPacked, {0x05});  // no EOP
    put_u64(a, 0); put_u64(a, 0x100);
    put_u64(a, 160); put_u64(a, 0x100);
    put_u64(a, (uint64_t)160 << 32); put_u64(a, 0x100);
    Feed(r, a, GifPath::Path3);
    CHECK(r.counting.Draws() == 0, "a complete triangle waits for a flush");
    r.parser.FinishPath3();
    CHECK(r.state_sink.Path3Finishes() == 1 && r.counting.Draws() == 1, "FinishPath3 flushed the queued triangle");
    r.parser.FinishPath3();
    CHECK(r.state_sink.Path3Finishes() == 2 && r.counting.Draws() == 1, "nothing queued: no draw");
    // The counting sink chained a DrawRec hash and forwarded to the capture sink.
    CHECK(r.counting.DrawHash() != CountingDrawIrSink::kHashOffset && r.capture.draws.size() == 1, "counting sink hashed and forwarded the draw");
}

// A play session without a recorder turns the recorder-only work off: the
// counting sink's FNV chains (SetHashing) and DrawFlush's snapshot extras
// (SetRecordSnapshot: vertex_hash, bbox). The draws themselves -- count, kind,
// vertices -- are unchanged; only those fields stay at their initial values.
static void RecorderExtrasOff() {
    auto feed_triangle = [](Rig& r) {
        Bytes a;
        put_tag(a, 3, true, true, 4 /* strip */, kPacked, {0x05});
        put_u64(a, 0); put_u64(a, 0x100);
        put_u64(a, 160); put_u64(a, 0x100);
        put_u64(a, (uint64_t)160 << 32); put_u64(a, 0x100);
        Feed(r, a, GifPath::Path3);
    };
    Rig on;
    Init(on);
    feed_triangle(on);
    Rig off;
    Init(off);
    off.counting.SetHashing(false);
    off.gs.Flush().SetRecordSnapshot(false);
    feed_triangle(off);
    CHECK(on.counting.Draws() == 1 && off.counting.Draws() == 1 && on.capture.draws.size() == 1 && off.capture.draws.size() == 1,
          "both rigs flushed one triangle");
    CHECK(on.counting.DrawHash() != CountingDrawIrSink::kHashOffset && on.counting.Hashing(), "hashing on: the DrawRec chain moved");
    CHECK(off.counting.DrawHash() == CountingDrawIrSink::kHashOffset && off.counting.JobHash() == CountingDrawIrSink::kHashOffset,
          "hashing off: both chains stay at the offset basis");
    if (on.capture.draws.size() == 1 && off.capture.draws.size() == 1) {
        const auto& a = on.capture.draws[0];
        const auto& b = off.capture.draws[0];
        CHECK(a.snapshot.vertex_hash != 0 && a.bbox[3] > a.bbox[0], "snapshot on: vertex_hash and bbox filled");
        CHECK(b.snapshot.vertex_hash == 0 && b.bbox[0] == 0 && b.bbox[3] == 0 && b.snapshot.verts_incl == 1,
              "snapshot off: vertex_hash and bbox stay 0, the vertices are still included");
        CHECK(a.verts.size() == b.verts.size() && a.prim_type == b.prim_type && a.vertex_count == b.vertex_count,
              "the draw itself is the same either way");
    }
}

// The identity stamp (invariant 28) crosses the arbiter queue: a staged identity
// rides the next PATH1 submit and is applied when the drain delivers THAT
// packet (not the submit); an unstaged PATH1 packet is raw; a PATH3 packet
// clears; the ordinals survive interleaving with PATH3 traffic; a PATH1
// packet without a slot is an underflow (cleared, counted).
static void IdentityStamp() {
    Rig r;
    Init(r);
    GifArbiter arbiter(r.parser);
    arbiter.SetSubmitObserver(&GsStateSink::OnArbiterSubmit, &r.state_sink);
    arbiter.SetDrainObserver(&GsStateSink::OnArbiterDeliver, &r.state_sink);
    r.state_sink.SetFrameInstance(7);
    auto tri = [](Bytes& v) {  // one PACKED triangle with EOP: one draw per packet
        put_tag(v, 3, true, true, 3 /* triangle */, kPacked, {0x05});
        put_u64(v, 0); put_u64(v, 0x100);
        put_u64(v, (uint64_t)160); put_u64(v, 0x100);
        put_u64(v, ((uint64_t)160 << 32)); put_u64(v, 0x100);
    };
    Bytes k0, k1, p3, k2;
    tri(k0); tri(k1); tri(p3); tri(k2);
    // HLE'd kick 0 and 1 (handler 0xAB, ordinals 0 and 1), then a PATH3 packet,
    // then a raw PATH1 kick -- all queued before any drain.
    r.state_sink.StageKickIdentity(0xAB, 0x1122334455667788ull, 0x3F0, 0);
    arbiter.submit(GifPath::Path1, k0.data(), k0.size());
    r.state_sink.StageKickIdentity(0xAB, 0x1122334455667788ull, 0x3F0, 1);
    arbiter.submit(GifPath::Path1, k1.data(), k1.size());
    r.state_sink.ClearStagedIdentity();
    arbiter.submit(GifPath::Path3, p3.data(), p3.size());
    arbiter.submit(GifPath::Path1, k2.data(), k2.size());
    CHECK(r.state_sink.IdentityPending() == 3, "three PATH1 slots queued (PATH3 takes none)");
    CHECK(r.counting.Draws() == 0, "nothing delivered before the drain");
    arbiter.drain();
    CHECK(r.counting.Draws() == 4 && r.capture.draws.size() == 4, "four draws delivered in order");
    if (r.capture.draws.size() == 4) {
        const auto& d = r.capture.draws;
        CHECK(d[0].identity.hle_handler == 0xAB && d[0].identity.image_hash == 0x1122334455667788ull &&
                  d[0].identity.kick_pc == 0x3F0 && d[0].identity.kick_ord == 0 && d[0].identity.frame_inst == 7,
              "kick 0 stamped at delivery");
        CHECK(d[1].identity.hle_handler == 0xAB && d[1].identity.kick_ord == 1 && d[1].identity.frame_inst == 7,
              "kick 1 keeps its own ordinal");
        CHECK(d[2].identity.hle_handler == 0 && d[2].identity.image_hash == 0 && d[2].identity.kick_ord == 0,
              "PATH3 packet is raw (cleared)");
        CHECK(d[3].identity.hle_handler == 0, "unstaged PATH1 kick is raw");
    }
    CHECK(r.state_sink.IdentityStamped() == 2 && r.state_sink.IdentityRaw() == 1 && r.state_sink.IdentityUnderflows() == 0,
          "census: 2 stamped, 1 raw, 0 underflows");
    CHECK(r.state_sink.IdentityPending() == 0, "FIFO drained");
    // A PATH1 packet the arbiter sees without a submit-observer slot (observer
    // detached) underflows: cleared and counted, the draw still delivered.
    arbiter.SetSubmitObserver(nullptr, nullptr);
    r.state_sink.StageKickIdentity(0xAB, 1, 2, 3);
    arbiter.submit(GifPath::Path1, k0.data(), k0.size());
    arbiter.drain();
    CHECK(r.state_sink.IdentityUnderflows() == 1 && r.counting.Draws() == 5, "underflow counted, draw delivered");
    CHECK(r.capture.draws.size() == 5 && r.capture.draws[4].identity.hle_handler == 0, "underflow delivers raw");
}

// The attribution rows (track 1c-2): PATH2 packets classified by their first
// GIFtag at the submit observer; PATH1 tags / kicks counted under the host's
// bucket hint (28 = none), PATH2 never.
static void Attribution() {
    Rig r;
    Init(r);
    using C = GsStateSink::Path2Class;
    const auto classify = [](const Bytes& v) { return GsStateSink::ClassifyPath2(v.data(), v.size()); };
    Bytes image;
    put_tag(image, 4, true, false, 0, kImage, {});
    CHECK(classify(image) == C::Image, "an IMAGE tag is the image class");
    Bytes texregs;
    put_tag(texregs, 3, true, false, 0, kPacked, {0xE});
    put_ad(texregs, 0x3F, 0);               // TEXFLUSH leads the title's WriteTextureRegisters
    put_ad(texregs, 0x06, 0x2000);
    put_ad(texregs, 0x34, 0);
    CHECK(classify(texregs) == C::TexRegs, "an A+D packet led by TEXFLUSH is texregs");
    Bytes head;
    put_tag(head, 1, true, false, 0, kPacked, {0xE});
    put_ad(head, 0x50, 0);                  // BITBLTBUF: the upload head
    CHECK(classify(head) == C::TransferHead, "an A+D packet led by BITBLTBUF is a transfer head");
    Bytes state;
    put_tag(state, 2, true, false, 0, kPacked, {0xE});
    put_ad(state, 0x42, 0);
    put_ad(state, 0x47, 0);
    CHECK(classify(state) == C::State, "an A+D packet led by ALPHA is state");
    Bytes geometry;
    put_tag(geometry, 2, true, false, 0, kPacked, {0x1, 0x5});   // RGBAQ, XYZ2
    CHECK(classify(geometry) == C::Geometry, "a PACKED tag naming XYZ2 is geometry");
    Bytes primed;
    put_tag(primed, 1, true, true, 6, kPacked, {0xE});
    put_ad(primed, 0x42, 0);
    CHECK(classify(primed) == C::Geometry, "PRE names a primitive: geometry");
    Bytes reglist;
    put_tag(reglist, 1, true, false, 0, 1, {0x1, 0x5});
    CHECK(classify(reglist) == C::Reglist, "a REGLIST tag");
    CHECK(classify({}) == C::Other && GsStateSink::ClassifyPath2(nullptr, 0) == C::Other, "no tag: other");
    // The submit observer counts PATH2 packets by class and bytes; PATH1 pushes a slot only.
    GsStateSink::OnArbiterSubmit(&r.state_sink, GifPath::Path2, texregs.data(), texregs.size());
    GsStateSink::OnArbiterSubmit(&r.state_sink, GifPath::Path2, image.data(), image.size());
    GsStateSink::OnArbiterSubmit(&r.state_sink, GifPath::Path1, geometry.data(), geometry.size());
    CHECK(r.state_sink.Path2Packets(C::TexRegs) == 1 && r.state_sink.Path2Bytes(C::TexRegs) == texregs.size(),
          "texregs counted with its bytes");
    CHECK(r.state_sink.Path2Packets(C::Image) == 1 && r.state_sink.Path2Packets(C::Geometry) == 0,
          "PATH1 submits are not PATH2 classes");
    CHECK(r.state_sink.IdentityPending() == 1, "the PATH1 submit pushed its identity slot");
    // Bucket attribution: PATH1 tags/kicks under the hint, none without it, PATH2 never.
    Bytes kick;
    put_tag(kick, 1, true, true, 0, kPacked, {0x1, 0x5});
    put_u64(kick, 0x80 | (0x80ull << 32)); put_u64(kick, 0x80 | (0x80ull << 32));   // RGBAQ
    put_u64(kick, (100ull << 4) | ((100ull << 4) << 32)); put_u64(kick, 0);         // XYZ2
    Feed(r, kick, GifPath::Path1);
    CHECK(r.state_sink.Path1TagsInBucket(GsStateSink::kBuckets) == 1 && r.state_sink.Path1KicksInBucket(GsStateSink::kBuckets) == 1,
          "no hint: counted under none");
    r.state_sink.SetBucketHint(3);
    Feed(r, kick, GifPath::Path1);
    Feed(r, kick, GifPath::Path1);
    CHECK(r.state_sink.Path1TagsInBucket(3) == 2 && r.state_sink.Path1KicksInBucket(3) == 2, "hint 3: two tags, two kicks");
    Feed(r, kick, GifPath::Path2);
    CHECK(r.state_sink.Path1TagsInBucket(3) == 2 && r.state_sink.KicksOn(GifPath::Path2) == 1, "a PATH2 kick is not a bucket row");
    r.state_sink.SetBucketHint(-1);
    Feed(r, kick, GifPath::Path1);
    CHECK(r.state_sink.Path1TagsInBucket(GsStateSink::kBuckets) == 2, "-1 clears the hint");
    r.state_sink.SetBucketHint(99);
    CHECK(r.state_sink.Path1TagsInBucket(99) == 2, "an out-of-range hint reads as none");
}

int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "all";
    if (c == "ad" || c == "all") AdWrites();
    if (c == "packed" || c == "all") PackedItems();
    if (c == "packedflat" || c == "all") PackedFlat();
    if (c == "image" || c == "all") ImageUpload();
    if (c == "eop" || c == "all") EopFlush();
    if (c == "fanout" || c == "all") Fanout();
    if (c == "path3" || c == "all") Path3Finish();
    if (c == "extras" || c == "all") RecorderExtrasOff();
    if (c == "identity" || c == "all") IdentityStamp();
    if (c == "attribution" || c == "all") Attribution();
    std::printf("test_gs_state_sink %s: %d checks, %d failures\n", c.c_str(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
