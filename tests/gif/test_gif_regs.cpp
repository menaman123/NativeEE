// GifRegsStat: the GIF register file read/write contract (STAT composition,
// TAG0-3 / CNT readback of the in-flight transfer, CTRL RST-then-PSE, MODE & 7,
// read-only registers), the GifTag parse + classification rules, the
// RegisterOp layout pin, the RegisterOpStream encoding and hash chain, and the
// CountingGsSink self-check.
#include <cstdio>
#include <cstring>

#include "counting_gs_sink.h"
#include "engine/core/hash/hash.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/gif_tag.h"
#include "engine/gs/hle/ir/register_op_stream.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static constexpr uint32_t kBase = GifRegs::kBase;

static uint64_t TagLo(uint32_t nloop, bool eop, bool pre, uint32_t prim, GifFormat flg, uint32_t nreg) {
    return static_cast<uint64_t>(nloop & 0x7FFFu) | (static_cast<uint64_t>(eop ? 1u : 0u) << 15) |
           (static_cast<uint64_t>(pre ? 1u : 0u) << 46) | (static_cast<uint64_t>(prim & 0x7FFu) << 47) |
           (static_cast<uint64_t>(static_cast<uint8_t>(flg) & 3u) << 58) |
           (static_cast<uint64_t>(nreg & 0xFu) << 60);
}

static uint64_t TagHi(const uint8_t* regs, int n) {
    uint64_t hi = 0;
    for (int i = 0; i < n; ++i) hi |= static_cast<uint64_t>(regs[i] & 0xFu) << (i * 4);
    return hi;
}

static uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

static void TestLayoutPins() {
    CHECK(sizeof(GifTag) == 24);
    CHECK(sizeof(RegisterOp) == 24);
    CHECK(sizeof(GifTagClass) == 4);
}

static void TestTagParse() {
    GifTag t{};
    const uint8_t regs[16] = {0x2, 0x1, 0x5, 0xF, 0xE, 0x0, 0xA, 0xC, 0xD, 0x3, 0x4, 0x6, 0x7, 0x8, 0x9, 0xB};
    // Reserved bits 16-45 set: they must be ignored.
    const uint64_t lo = TagLo(0x1234, true, true, 0x5A5, GifFormat::Reglist, 3) | (0x3FFFFFFFull << 16);
    t.Parse(lo, TagHi(regs, 16));
    CHECK(t.nloop == 0x1234);
    CHECK(t.eop);
    CHECK(t.pre);
    CHECK(t.prim == 0x5A5);
    CHECK(t.flg == GifFormat::Reglist);
    CHECK(t.nregs == 3);
    for (int i = 0; i < 16; ++i) CHECK(t.regs[i] == regs[i]);

    // NREG 0 normalises to 16; PRIM is 11 bits; NLOOP is 15 bits.
    t.Parse(TagLo(0x7FFF, false, false, 0x7FF, GifFormat::Image, 0), 0);
    CHECK(t.nregs == 16);
    CHECK(t.nloop == 0x7FFF);
    CHECK(t.prim == 0x7FF);
    CHECK(!t.eop);
    CHECK(t.flg == GifFormat::Image);
    for (int i = 0; i < 16; ++i) CHECK(t.regs[i] == 0);
    t.Parse(TagLo(0, false, false, 0, GifFormat::Disabled, 1), 0);
    CHECK(t.flg == GifFormat::Disabled);
    CHECK(t.nregs == 1);
}

static void TestTagClassify() {
    GifTag t{};
    const uint8_t strip3[3] = {0x2, 0x1, 0x5};
    t.Parse(TagLo(7, false, true, 4, GifFormat::Packed, 3), TagHi(strip3, 3));
    GifTagClass c = ClassifyGifTag(t);
    CHECK(!c.has_ad && c.all_known && c.strip3 && !c.rowable);

    const uint8_t strip3f[3] = {0x2, 0x1, 0x4};
    t.Parse(TagLo(7, false, true, 4, GifFormat::Packed, 3), TagHi(strip3f, 3));
    c = ClassifyGifTag(t);
    CHECK(c.strip3 && !c.rowable);

    // {ST, RGBAQ, XYZ3}: all known, not the strip3 triple -> rowable.
    const uint8_t xyz3[3] = {0x2, 0x1, 0xD};
    t.Parse(TagLo(4, false, true, 4, GifFormat::Packed, 3), TagHi(xyz3, 3));
    c = ClassifyGifTag(t);
    CHECK(!c.has_ad && c.all_known && !c.strip3 && c.rowable);

    // {A+D, XYZ2}: has_ad, A+D is not a known decoder descriptor.
    const uint8_t ad[2] = {0xE, 0x5};
    t.Parse(TagLo(3, true, true, 4, GifFormat::Packed, 2), TagHi(ad, 2));
    c = ClassifyGifTag(t);
    CHECK(c.has_ad && !c.all_known && !c.strip3 && !c.rowable);

    // NREG 16 of {ST, RGBAQ, XYZ2, NOP} repeated: rowable (NOP is known).
    uint8_t r16[16];
    for (int i = 0; i < 16; ++i) r16[i] = static_cast<uint8_t>("\x02\x01\x05\x0F"[i % 4]);
    t.Parse(TagLo(2, true, true, 4, GifFormat::Packed, 0), TagHi(r16, 16));
    c = ClassifyGifTag(t);
    CHECK(t.nregs == 16 && c.all_known && !c.strip3 && c.rowable);

    // A descriptor outside the decoded set (TEX0_1 = 0x06) is not all_known.
    const uint8_t tex[2] = {0x6, 0x5};
    t.Parse(TagLo(1, true, false, 0, GifFormat::Packed, 2), TagHi(tex, 2));
    c = ClassifyGifTag(t);
    CHECK(!c.all_known && !c.rowable && !c.has_ad);

    // Descriptors beyond NREG are not consulted.
    const uint8_t beyond[4] = {0x2, 0x1, 0x5, 0xE};
    t.Parse(TagLo(1, true, false, 0, GifFormat::Packed, 3), TagHi(beyond, 4));
    c = ClassifyGifTag(t);
    CHECK(!c.has_ad && c.strip3);
}

static void TestRegsFresh() {
    GifRegs r;
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kCtrl, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kMode, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag1, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag2, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag3, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kCnt, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kP3Cnt, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kP3Tag, 4) == 0);
    CHECK(r.Read(kBase + 0x30u, 4) == 0);  // unmapped offset inside the window
    CHECK(r.Read(kBase + 0xF0u, 4) == 0);
    CHECK(!r.Path3Masked());
    CHECK(!r.Path3MaskedByVif());
    CHECK(!r.Transfer().in_transfer);
}

static void TestModeAndCtrl() {
    GifRegs r;
    // MODE keeps only its low three bits; STAT mirrors M3R (bit 0) and IMT (bit 2).
    r.Write(kBase + GifRegs::kMode, 4, 0xFF);
    CHECK(r.Read(kBase + GifRegs::kMode, 4) == 7);
    CHECK(r.Mode() == 7);
    CHECK(r.Path3Masked());
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x5);
    r.Write(kBase + GifRegs::kMode, 4, 0x2);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x0);  // bit 1 of MODE has no STAT mirror
    CHECK(!r.Path3Masked());
    r.Write(kBase + GifRegs::kMode, 4, 0x5);

    // CTRL.PSE latches into STAT bit 3; other CTRL bits are not stored.
    r.Write(kBase + GifRegs::kCtrl, 4, 8);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0xD);
    CHECK(r.Read(kBase + GifRegs::kCtrl, 4) == 8);
    r.Write(kBase + GifRegs::kCtrl, 4, 0xF6);  // PSE clear, RST clear, junk bits
    CHECK(r.Read(kBase + GifRegs::kCtrl, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x5);
    r.Write(kBase + GifRegs::kCtrl, 4, 8);
    CHECK((r.Read(kBase + GifRegs::kStat, 4) & 8u) != 0);

    // RST then PSE from the same write: MODE zeroed, PSE survives the reset.
    r.SetPath3MaskedByVif(true);
    r.Write(kBase + GifRegs::kCtrl, 4, 9);
    CHECK(r.Read(kBase + GifRegs::kMode, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x8);
    CHECK(r.Read(kBase + GifRegs::kCtrl, 4) == 8);
    CHECK(!r.Path3MaskedByVif());
    // RST alone clears PSE too.
    r.Write(kBase + GifRegs::kCtrl, 4, 1);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kCtrl, 4) == 0);

    // The VIF mask shows as STAT.M3P (bit 1) and nowhere else.
    r.SetPath3MaskedByVif(true);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x2);
    CHECK(r.Read(kBase + GifRegs::kMode, 4) == 0);
    r.SetPath3MaskedByVif(false);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0);

    // Read-only registers drop writes.
    r.Write(kBase + GifRegs::kStat, 4, 0xFFFFFFFFu);
    r.Write(kBase + GifRegs::kTag0, 4, 0xFFFFFFFFu);
    r.Write(kBase + GifRegs::kCnt, 4, 0xFFFFFFFFu);
    r.Write(kBase + GifRegs::kP3Tag, 4, 0xFFFFFFFFu);
    r.Write(kBase + 0x30u, 4, 0xFFFFFFFFu);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kCnt, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kP3Tag, 4) == 0);

    // Access width is ignored: 1/2/4/8-byte reads return the same 32-bit value.
    r.Write(kBase + GifRegs::kMode, 8, 0xFFFFFFFF00000007ull);
    CHECK(r.Read(kBase + GifRegs::kMode, 1) == 7);
    CHECK(r.Read(kBase + GifRegs::kMode, 2) == 7);
    CHECK(r.Read(kBase + GifRegs::kMode, 8) == 7);
}

static void TestTransferReadback() {
    GifRegs r;
    const uint8_t regs[16] = {0x2, 0x1, 0x5, 0xF, 0xE, 0x0, 0xA, 0xC, 0xD, 0x3, 0x4, 0x6, 0x7, 0x8, 0x9, 0xB};
    GifTransferState& ts = r.Transfer();
    ts.tag.Parse(TagLo(0x1234, true, true, 0x5A5, GifFormat::Packed, 3), TagHi(regs, 16));
    ts.in_transfer = true;
    ts.loop = 2;
    ts.reg = 1;

    // OPH (bit 9) and APATH = 3 (bits 10-11) while in transfer, FQC 0.
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0xE00u);
    r.Write(kBase + GifRegs::kMode, 4, 1);
    r.SetPath3MaskedByVif(true);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == (0xE00u | 0x3u));
    CHECK(((r.Read(kBase + GifRegs::kStat, 4) >> 24) & 0x1Fu) == 0);

    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == (0x1234u | (1u << 15)));
    CHECK(r.Read(kBase + GifRegs::kTag1, 4) == ((1u << 14) | (0x5A5u << 15) | (0u << 26) | (3u << 28)));
    CHECK(r.Read(kBase + GifRegs::kTag2, 4) == 0xCA0EF512u);
    CHECK(r.Read(kBase + GifRegs::kTag3, 4) == 0xB987643Du);
    CHECK(r.Read(kBase + GifRegs::kCnt, 4) == ((0x1234u - 2u) | (1u << 16)));

    // FLG lands in TAG1 bits 26-27; NREG 16 shifts out of the 32-bit word and
    // reads 0, which is the hardware encoding of sixteen registers.
    ts.tag.Parse(TagLo(5, false, false, 0, GifFormat::Image, 0), 0);
    CHECK(ts.tag.nregs == 16);
    CHECK(r.Read(kBase + GifRegs::kTag1, 4) == (2u << 26));
    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == 5u);
    ts.loop = 5;
    ts.reg = 0;
    CHECK(r.Read(kBase + GifRegs::kCnt, 4) == 0);

    // A completed tag (in_transfer false) still reads back through TAG0-3 but
    // drops OPH/APATH; CTRL.RST zeroes the transfer entirely.
    ts.in_transfer = false;
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0x3u);
    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == 5u);
    r.Write(kBase + GifRegs::kCtrl, 4, 1);
    CHECK(r.Read(kBase + GifRegs::kStat, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag0, 4) == 0);
    CHECK(r.Read(kBase + GifRegs::kTag1, 4) == 0);
    CHECK(r.Transfer().tag.nregs == 0);
    CHECK(!r.Transfer().in_transfer);
}

static void TestRegisterOpStream() {
    RegisterOpStream s;
    CHECK(s.Count() == 0);
    CHECK(s.Hash() == ps2r::hash::kFnv1a64Offset);

    GifTag t{};
    const uint8_t regs[3] = {0x2, 0x1, 0x5};
    t.Parse(TagLo(7, true, true, 0x00C, GifFormat::Packed, 3), TagHi(regs, 3));
    s.BeginTag(t, GifPath::Path2);
    s.WriteRegister(static_cast<uint8_t>(GsReg::Prim), 0x00C);
    s.PackedSt(0.25f, 1.5f, 1.0f);
    s.PackedRgbaq(10, 20, 30, 0x60);
    s.PackedUv(330, 430);
    s.PackedFog(0x40);
    s.KickVertex(100.0f, 200.0f, 4660.0f, 0x41, true);
    s.ImageData(0x1111111100000000ull, 0x2222222200000001ull);
    s.EndOfPacket();
    s.FinishPath3();
    CHECK(s.Count() == 10);

    const std::vector<RegisterOp>& ops = s.Ops();
    // Every record carries the path of the packet opened by BeginTag; pad is 0.
    for (const RegisterOp& op : ops) {
        CHECK(op.path == 2);
        CHECK(op.pad == 0);
    }
    CHECK(ops[0].kind == static_cast<uint8_t>(RegisterOpKind::Tag));
    CHECK(ops[0].aux == (7u | (1u << 15) | (1u << 16) | (0u << 17) | (3u << 19)));
    CHECK(ops[0].lo == 0x00C);
    CHECK(ops[0].hi == 0x512u);
    CHECK(ops[1].kind == static_cast<uint8_t>(RegisterOpKind::Reg) && ops[1].reg == 0 && ops[1].lo == 0x00C);
    CHECK(ops[2].kind == static_cast<uint8_t>(RegisterOpKind::St) && ops[2].reg == 0x02);
    CHECK(ops[2].lo == (static_cast<uint64_t>(Bits(0.25f)) | (static_cast<uint64_t>(Bits(1.5f)) << 32)));
    CHECK(ops[2].hi == Bits(1.0f));
    CHECK(ops[3].kind == static_cast<uint8_t>(RegisterOpKind::Rgbaq) && ops[3].reg == 0x01);
    CHECK(ops[3].lo == (10u | (20u << 8) | (30u << 16) | (0x60u << 24)));
    CHECK(ops[4].kind == static_cast<uint8_t>(RegisterOpKind::Uv) && ops[4].reg == 0x03);
    CHECK(ops[4].lo == (330u | (430u << 16)));
    CHECK(ops[5].kind == static_cast<uint8_t>(RegisterOpKind::Fog) && ops[5].reg == 0x0A && ops[5].lo == 0x40);
    CHECK(ops[6].kind == static_cast<uint8_t>(RegisterOpKind::Kick) && ops[6].reg == 0);
    CHECK(ops[6].lo == (static_cast<uint64_t>(Bits(100.0f)) | (static_cast<uint64_t>(Bits(200.0f)) << 32)));
    CHECK(ops[6].hi == (static_cast<uint64_t>(Bits(4660.0f)) | (0x41ull << 32) | (1ull << 40)));
    CHECK(ops[7].kind == static_cast<uint8_t>(RegisterOpKind::Image) && ops[7].reg == 0x54);
    CHECK(ops[7].lo == 0x1111111100000000ull && ops[7].hi == 0x2222222200000001ull);
    CHECK(ops[8].kind == static_cast<uint8_t>(RegisterOpKind::Eop) && ops[8].lo == 0 && ops[8].hi == 0);
    CHECK(ops[9].kind == static_cast<uint8_t>(RegisterOpKind::Finish));

    // Hash == FNV-1a chained record by record from the offset basis.
    uint64_t h = ps2r::hash::kFnv1a64Offset;
    for (const RegisterOp& op : ops) h = ps2r::hash::Fnv1a64(&op, sizeof(op), h);
    CHECK(h == s.Hash());
    CHECK(h != ps2r::hash::kFnv1a64Offset);

    // The same call sequence reproduces the same hash; a draw flip changes it.
    RegisterOpStream s2;
    s2.BeginTag(t, GifPath::Path2);
    s2.WriteRegister(static_cast<uint8_t>(GsReg::Prim), 0x00C);
    s2.PackedSt(0.25f, 1.5f, 1.0f);
    s2.PackedRgbaq(10, 20, 30, 0x60);
    s2.PackedUv(330, 430);
    s2.PackedFog(0x40);
    s2.KickVertex(100.0f, 200.0f, 4660.0f, 0x41, true);
    s2.ImageData(0x1111111100000000ull, 0x2222222200000001ull);
    s2.EndOfPacket();
    s2.FinishPath3();
    CHECK(s2.Hash() == s.Hash());
    CHECK(std::memcmp(s2.Ops().data(), s.Ops().data(), sizeof(RegisterOp) * s.Count()) == 0);
    RegisterOpStream s3;
    s3.KickVertex(100.0f, 200.0f, 4660.0f, 0x41, false);
    RegisterOpStream s4;
    s4.KickVertex(100.0f, 200.0f, 4660.0f, 0x41, true);
    CHECK(s3.Hash() != s4.Hash());

    // Clear restores the empty state; the path resets until the next BeginTag.
    s.Clear();
    CHECK(s.Count() == 0 && s.Hash() == ps2r::hash::kFnv1a64Offset);
    s.EndOfPacket();
    CHECK(s.Ops()[0].path == 0);
}

// A hash-only stream (the leg's recorder) folds every record into the same hash
// as a retaining one but keeps no record: after N frames' worth of sink calls its
// retained bytes are 0 and Count() still counts. Red before BUG-083's fix: the
// recorder retained every op (24 bytes per call) for the whole leg -- the title's
// uploads and vertices grew it by ~8-11 MB per frame until the OS killed the leg.
static void TestRegisterOpStreamHashOnlyBounded() {
    RegisterOpStream retaining(/*retain_records=*/true);
    RegisterOpStream hash_only(/*retain_records=*/false);
    CHECK(retaining.RetainsRecords() && !hash_only.RetainsRecords());
    GifTag t{};
    const uint8_t regs[1] = {0x0E};
    t.Parse(TagLo(512, true, false, 0, GifFormat::Image, 1), TagHi(regs, 1));
    // 64 "frames" of 8 uploads x 512 image quadwords each, with a kick between.
    for (int frame = 0; frame < 64; ++frame) {
        for (int up = 0; up < 8; ++up) {
            for (RegisterOpStream* s : {&retaining, &hash_only}) {
                s->BeginTag(t, GifPath::Path3);
                for (int q = 0; q < 512; ++q)
                    s->ImageData(0x0100000000000000ull * frame + q, 0x0200000000000000ull * up + q);
                s->EndOfPacket();
                s->KickVertex(1.0f * frame, 2.0f * up, 3.0f, 0, true);
            }
        }
    }
    const size_t expected_ops = 64u * 8u * (1u + 512u + 1u + 1u);
    CHECK(retaining.Count() == expected_ops);
    CHECK(hash_only.Count() == expected_ops);
    CHECK(hash_only.Hash() == retaining.Hash());
    CHECK(hash_only.Hash() != ps2r::hash::kFnv1a64Offset);
    // The bounded-footprint pin: the hash-only stream holds no record bytes.
    CHECK(hash_only.Ops().empty());
    CHECK(hash_only.RetainedBytes() == 0);
    CHECK(retaining.Ops().size() == expected_ops);
    CHECK(retaining.RetainedBytes() >= expected_ops * sizeof(RegisterOp));
    // Clear releases a retaining stream's storage too.
    retaining.Clear();
    CHECK(retaining.RetainedBytes() == 0 && retaining.Count() == 0);
}

static void TestCountingSink() {
    CountingGsSink c;
    GifTag t{};
    c.BeginTag(t, GifPath::Path1);
    c.WriteRegister(0x1C, 0x55);
    c.PackedRgbaq(1, 2, 3, 4);
    c.PackedSt(0.f, 0.f, 0.f);
    c.PackedUv(1, 2);
    c.PackedFog(9);
    c.KickVertex(0.f, 0.f, 0.f, 0, true);
    c.KickVertex(0.f, 0.f, 0.f, 0, false);
    c.ImageData(0, 0);
    c.EndOfPacket();
    c.FinishPath3();
    CHECK(c.begin_tag == 1 && c.write_register == 1 && c.packed_rgbaq == 1 && c.packed_st == 1);
    CHECK(c.packed_uv == 1 && c.packed_fog == 1 && c.kick_vertex == 2 && c.kick_draw == 1);
    CHECK(c.image_data == 1 && c.end_of_packet == 1 && c.finish_path3 == 1);
    CHECK(c.last_reg == 0x1C && c.last_value == 0x55);
    CHECK(c.Total() == 11);
    c.Reset();
    CHECK(c.Total() == 0 && c.kick_draw == 0 && c.last_reg == 0);
}

int main() {
    TestLayoutPins();
    TestTagParse();
    TestTagClassify();
    TestRegsFresh();
    TestModeAndCtrl();
    TestTransferReadback();
    TestRegisterOpStream();
    TestRegisterOpStreamHashOnlyBounded();
    TestCountingSink();
    if (g_fail) {
        std::printf("GifRegsStat: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("GifRegsStat: PASS\n");
    return 0;
}
