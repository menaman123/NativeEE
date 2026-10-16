// VifUnpackScalar: the UNPACK reference loop on hand-computed vectors -- every
// format x sign, the S / V2 / V3 / V4-5 expansion rules, MODE adds, the MASK
// table and actions, CL/WL skip and fill, the 1024-QW wrap, TOPS (VIF1 only),
// VU0 masked reads, partial payloads; GetUnpackSize; and the bulk lanes
// byte-identical to the scalar loop on their eligible shapes.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/vif/vif_unpack.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

constexpr uint32_t kUsn = 0x4000;
constexpr uint32_t kTops = 0x8000;

VifRegs DefaultRegs() {
    VifRegs r{};
    r.cycle = 0x0101;
    return r;
}

std::vector<uint8_t> Words(std::initializer_list<uint32_t> ws) {
    std::vector<uint8_t> out;
    for (uint32_t w : ws) {
        out.push_back(static_cast<uint8_t>(w));
        out.push_back(static_cast<uint8_t>(w >> 8));
        out.push_back(static_cast<uint8_t>(w >> 16));
        out.push_back(static_cast<uint8_t>(w >> 24));
    }
    return out;
}

// Run one UNPACK through the scalar loop (bulk lanes off unless `bulk`).
void Run(VuUnit& vu, const VifRegs& regs, int vif, uint32_t cmd, uint32_t imm, uint32_t num,
         const std::vector<uint8_t>& payload, uint8_t phase, bool bulk = false, VifUnpack* census = nullptr) {
    VifUnpack local(VifUnpackSettings{bulk});
    VifUnpack& up = census ? *census : local;
    up.Execute(vif, vu, regs, cmd, imm, num, payload.data(), payload.size(), (cmd & 0x10) != 0, phase);
}

bool QwIs(const VuUnit& vu, uint32_t qw, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t got[4];
    vu.ReadDataMem((qw * 16) & vu.mem_mask(), reinterpret_cast<uint8_t*>(got), 16);
    const bool ok = got[0] == a && got[1] == b && got[2] == c && got[3] == d;
    if (!ok)
        std::printf("  qw %u = %08x %08x %08x %08x, want %08x %08x %08x %08x\n", qw, got[0], got[1], got[2],
                    got[3], a, b, c, d);
    return ok;
}

void Fill(VuUnit& vu, uint8_t byte) {
    std::vector<uint8_t> buf(Vu1State::kMemSize, byte);
    vu.WriteDataMem(0, buf.data(), buf.size());
}

void TestUnpackSize() {
    CHECK(VifUnpack::GetUnpackSize(0x6C, 7, 1, 1) == 112);    // V4-32 x 7
    CHECK(VifUnpack::GetUnpackSize(0x6C, 0, 1, 1) == 4096);   // num 0 -> 256
    CHECK(VifUnpack::GetUnpackSize(0x68, 5, 1, 1) == 60);     // V3-32 x 5
    CHECK(VifUnpack::GetUnpackSize(0x6E, 9, 1, 1) == 36);     // V4-8 x 9
    CHECK(VifUnpack::GetUnpackSize(0x6D, 13, 1, 1) == 104);   // V4-16 x 13
    CHECK(VifUnpack::GetUnpackSize(0x6F, 4, 1, 1) == 8);      // V4-5 x 4 (16 bits each)
    CHECK(VifUnpack::GetUnpackSize(0x62, 3, 1, 1) == 4);      // S-8 x 3 = 3 bytes -> 4
    CHECK(VifUnpack::GetUnpackSize(0x61, 3, 1, 1) == 8);      // S-16 x 3 = 6 -> 8
    CHECK(VifUnpack::GetUnpackSize(0x66, 3, 1, 1) == 8);      // V2-8 x 3 = 6 -> 8
    CHECK(VifUnpack::GetUnpackSize(0x7C, 4, 1, 1) == 64);     // masked bit does not change the size
    CHECK(VifUnpack::GetUnpackSize(0x6C, 6, 4, 2) == 96);     // CL > WL: every vector from the source
    CHECK(VifUnpack::GetUnpackSize(0x6C, 5, 1, 2) == 48);     // CL < WL: 2*1 + min(1,1) = 3 vectors
    CHECK(VifUnpack::GetUnpackSize(0x6C, 6, 2, 4) == 64);     // CL < WL: 1*2 + min(2,2) = 4 vectors
    CHECK(VifUnpack::GetUnpackSize(0x6C, 6, 0, 4) == 96);     // CL 0 falls in the skipping arm
    CHECK(VifUnpack::GetUnpackSize(0x63, 4, 1, 1) == 0);      // undefined format
    CHECK(VifUnpack::GetUnpackSize(0x67, 4, 1, 1) == 0);
    CHECK(VifUnpack::GetUnpackSize(0x6B, 4, 1, 1) == 0);
    CHECK(VifUnpack::IsUnpackCmd(0x60) && VifUnpack::IsUnpackCmd(0x7F) && !VifUnpack::IsUnpackCmd(0x50) &&
          !VifUnpack::IsUnpackCmd(0x80));
}

void TestMaskTable() {
    // row0: W, NW, ROW, COL ; row1: all COL ; row2: all ROW ; row3: all NW
    const uint32_t mask = 0x55AAFFE4u;
    // STMASK codes (EE User's Manual): 00 data, 01 ROW, 10 COL, 11 write-protect.
    CHECK(VifUnpack::MaskActionFor(mask, 0, 0) == MaskAction::Write);
    CHECK(VifUnpack::MaskActionFor(mask, 1, 0) == MaskAction::Row);
    CHECK(VifUnpack::MaskActionFor(mask, 2, 0) == MaskAction::Col);
    CHECK(VifUnpack::MaskActionFor(mask, 3, 0) == MaskAction::NoWrite);
    for (int f = 0; f < 4; f++) {
        CHECK(VifUnpack::MaskActionFor(mask, f, 1) == MaskAction::NoWrite);
        CHECK(VifUnpack::MaskActionFor(mask, f, 2) == MaskAction::Col);
        CHECK(VifUnpack::MaskActionFor(mask, f, 3) == MaskAction::Row);
        CHECK(VifUnpack::MaskActionFor(mask, f, 5) == MaskAction::NoWrite);   // row = cycle & 3
        CHECK(VifUnpack::MaskActionFor(mask, f, 7) == MaskAction::Row);
    }
    CHECK(VifUnpack::MaskActionFor(0x00000055u, 0, 0) == MaskAction::Row);
    CHECK(VifUnpack::MaskActionFor(0x00000055u, 3, 0) == MaskAction::Row);
    CHECK(VifUnpack::MaskActionFor(0x000000FFu, 0, 0) == MaskAction::NoWrite);
    CHECK(VifUnpack::MaskActionFor(0x00000055u, 0, 1) == MaskAction::Write);
}

void TestFormats() {
    VuUnit vu(1);
    const VifRegs r = DefaultRegs();

    // V4-32: raw quadwords, sign bit irrelevant.
    Run(vu, r, 1, 0x6C, 10, 2, Words({1, 2, 3, 4, 5, 6, 7, 8, 0xDEAD}), 0);
    CHECK(QwIs(vu, 10, 1, 2, 3, 4));
    CHECK(QwIs(vu, 11, 5, 6, 7, 8));
    Run(vu, r, 1, 0x6C, 12, 1, Words({0x80000000u, 0xFFFFFFFFu, 0, 1}), 0);
    CHECK(QwIs(vu, 12, 0x80000000u, 0xFFFFFFFFu, 0, 1));

    // V4-16 signed / unsigned: bytes FF FF 00 80 FF 7F 01 00.
    Run(vu, r, 1, 0x6D, 20, 1, Words({0x8000FFFFu, 0x00017FFFu}), 0);
    CHECK(QwIs(vu, 20, 0xFFFFFFFFu, 0xFFFF8000u, 0x7FFF, 1));
    Run(vu, r, 1, 0x6D, kUsn | 21, 1, Words({0x8000FFFFu, 0x00017FFFu}), 0);
    CHECK(QwIs(vu, 21, 0xFFFF, 0x8000, 0x7FFF, 1));

    // V4-8 signed / unsigned: bytes FF 01 7F 80.
    Run(vu, r, 1, 0x6E, 22, 1, Words({0x807F01FFu}), 0);
    CHECK(QwIs(vu, 22, 0xFFFFFFFFu, 1, 0x7F, 0xFFFFFF80u));
    Run(vu, r, 1, 0x6E, kUsn | 23, 1, Words({0x807F01FFu}), 0);
    CHECK(QwIs(vu, 23, 0xFF, 1, 0x7F, 0x80));

    // S-32 / S-16 / S-8 replicate.
    Run(vu, r, 1, 0x60, 30, 2, Words({0x11111111u, 0x22222222u}), 0);
    CHECK(QwIs(vu, 30, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u));
    CHECK(QwIs(vu, 31, 0x22222222u, 0x22222222u, 0x22222222u, 0x22222222u));
    Run(vu, r, 1, 0x61, 32, 2, Words({0x00028000u}), 0);
    CHECK(QwIs(vu, 32, 0xFFFF8000u, 0xFFFF8000u, 0xFFFF8000u, 0xFFFF8000u));
    CHECK(QwIs(vu, 33, 2, 2, 2, 2));
    Run(vu, r, 1, 0x61, kUsn | 34, 1, Words({0x00028000u}), 0);
    CHECK(QwIs(vu, 34, 0x8000, 0x8000, 0x8000, 0x8000));
    Run(vu, r, 1, 0x62, 36, 4, Words({0x7F80FF01u}), 0);
    CHECK(QwIs(vu, 36, 1, 1, 1, 1));
    CHECK(QwIs(vu, 37, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu));
    CHECK(QwIs(vu, 38, 0xFFFFFF80u, 0xFFFFFF80u, 0xFFFFFF80u, 0xFFFFFF80u));
    CHECK(QwIs(vu, 39, 0x7F, 0x7F, 0x7F, 0x7F));
    Run(vu, r, 1, 0x62, kUsn | 40, 2, Words({0x7F80FF01u}), 0);
    CHECK(QwIs(vu, 40, 1, 1, 1, 1));
    CHECK(QwIs(vu, 41, 0xFF, 0xFF, 0xFF, 0xFF));

    // V2-32: {x, y, x, y}; W = 0 when the payload started at an odd word.
    Run(vu, r, 1, 0x64, 50, 1, Words({0xA, 0xB}), 0);
    CHECK(QwIs(vu, 50, 0xA, 0xB, 0xA, 0xB));
    Run(vu, r, 1, 0x64, 51, 1, Words({0xA, 0xB}), 1);
    CHECK(QwIs(vu, 51, 0xA, 0xB, 0xA, 0));
    // V2-16 / V2-8: the phase rule applies to 32-bit only.
    Run(vu, r, 1, 0x65, kUsn | 52, 1, Words({0x00020001u}), 1);
    CHECK(QwIs(vu, 52, 1, 2, 1, 2));
    Run(vu, r, 1, 0x65, 53, 1, Words({0x0002FFFFu}), 0);
    CHECK(QwIs(vu, 53, 0xFFFFFFFFu, 2, 0xFFFFFFFFu, 2));
    Run(vu, r, 1, 0x66, 54, 2, Words({0x04030201u}), 1);
    CHECK(QwIs(vu, 54, 1, 2, 1, 2));
    CHECK(QwIs(vu, 55, 3, 4, 3, 4));

    // V3-32: W = the following word (not consumed); zero when vector parity != phase.
    Run(vu, r, 1, 0x68, 60, 2, Words({1, 2, 3, 4, 5, 6, 0x77}), 0);  // 0x77 = the lookahead word
    CHECK(QwIs(vu, 60, 1, 2, 3, 4));
    CHECK(QwIs(vu, 61, 4, 5, 6, 0));
    Run(vu, r, 1, 0x68, 62, 2, Words({1, 2, 3, 4, 5, 6, 0x77}), 1);
    CHECK(QwIs(vu, 62, 1, 2, 3, 0));
    CHECK(QwIs(vu, 63, 4, 5, 6, 0x77));
    Run(vu, r, 1, 0x68, 64, 1, Words({1, 2, 3, 0}), 0);  // lookahead 0 at a quadword boundary
    CHECK(QwIs(vu, 64, 1, 2, 3, 0));
    // V3-16 / V3-8: W peeks the next element, no parity rule.
    Run(vu, r, 1, 0x69, kUsn | 65, 1, Words({0x00020001u, 0x00040003u}), 1);
    CHECK(QwIs(vu, 65, 1, 2, 3, 4));
    Run(vu, r, 1, 0x69, 66, 1, Words({0x0002FFFFu, 0x00040003u}), 0);
    CHECK(QwIs(vu, 66, 0xFFFFFFFFu, 2, 3, 4));
    Run(vu, r, 1, 0x6A, kUsn | 67, 1, Words({0x04030201u}), 1);
    CHECK(QwIs(vu, 67, 1, 2, 3, 4));
    Run(vu, r, 1, 0x6A, 68, 2, Words({0x04030281u, 0x00000006u}), 0);
    CHECK(QwIs(vu, 68, 0xFFFFFF81u, 2, 3, 4));
    CHECK(QwIs(vu, 69, 4, 6, 0, 0));  // second vector: bytes 4, 6, 0; W peeks the next (zero) byte

    // V4-5: 5-5-5-1 -> {r<<3, g<<3, b<<3, a ? 0x80 : 0}.
    Run(vu, r, 1, 0x6F, 70, 2, Words({0x0421FFFFu}), 0);
    CHECK(QwIs(vu, 70, 248, 248, 248, 0x80));
    CHECK(QwIs(vu, 71, 8, 8, 8, 0));
    Run(vu, r, 1, 0x6F, 72, 1, Words({0x00008000u}), 0);
    CHECK(QwIs(vu, 72, 0, 0, 0, 0x80));
}

void TestModeAndMask() {
    VuUnit vu(1);
    VifRegs r = DefaultRegs();
    r.row[0] = 1; r.row[1] = 2; r.row[2] = 3; r.row[3] = 4;
    r.col[0] = 10; r.col[1] = 20; r.col[2] = 30; r.col[3] = 40;

    // MODE 1: + ROW (u32 wrap).
    r.mode = 1;
    Run(vu, r, 1, 0x6C, kUsn | 100, 1, Words({0xFFFFFFFFu, 0, 0, 0}), 0);
    CHECK(QwIs(vu, 100, 0, 2, 3, 4));
    Run(vu, r, 1, 0x6E, kUsn | 101, 1, Words({0x04030201u}), 0);
    CHECK(QwIs(vu, 101, 2, 4, 6, 8));

    // MODE 2: + COL[write_cycle & 3], write_cycle wrapping at WL.
    r.mode = 2;
    r.cycle = 0x0404;
    Run(vu, r, 1, 0x6C, 110, 5, Words({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
    CHECK(QwIs(vu, 110, 10, 10, 10, 10));
    CHECK(QwIs(vu, 111, 20, 20, 20, 20));
    CHECK(QwIs(vu, 112, 30, 30, 30, 30));
    CHECK(QwIs(vu, 113, 40, 40, 40, 40));
    CHECK(QwIs(vu, 114, 10, 10, 10, 10));
    r.cycle = 0x0202;
    Run(vu, r, 1, 0x6C, 120, 4, Words({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
    CHECK(QwIs(vu, 120, 10, 10, 10, 10));
    CHECK(QwIs(vu, 121, 20, 20, 20, 20));
    CHECK(QwIs(vu, 122, 10, 10, 10, 10));
    CHECK(QwIs(vu, 123, 20, 20, 20, 20));

    // MODE 3: + ROW + COL.
    r.mode = 3;
    r.cycle = 0x0101;
    Run(vu, r, 1, 0x60, 130, 1, Words({100}), 0);
    CHECK(QwIs(vu, 130, 111, 112, 113, 114));

    // MASK: row0 W/ROW/COL/NW, row1 NW, row2 COL, row3 ROW; write_cycle 0..3 (CL=WL=4), then row 0 again.
    r.mode = 0;
    r.cycle = 0x0404;
    r.mask = 0x55AAFFE4u;
    r.row[0] = 0x11; r.row[1] = 0x22; r.row[2] = 0x33; r.row[3] = 0x44;
    r.col[0] = 0xA1; r.col[1] = 0xA2; r.col[2] = 0xA3; r.col[3] = 0xA4;
    Fill(vu, 0xEE);
    std::vector<uint8_t> p;
    for (uint32_t i = 1; i <= 20; i++) {
        auto w = Words({i});
        p.insert(p.end(), w.begin(), w.end());
    }
    Run(vu, r, 1, 0x7C, 200, 5, p, 0);
    CHECK(QwIs(vu, 200, 1, 0x22, 0xA1, 0xEEEEEEEEu));
    CHECK(QwIs(vu, 201, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu));
    CHECK(QwIs(vu, 202, 0xA3, 0xA3, 0xA3, 0xA3));
    CHECK(QwIs(vu, 203, 0x11, 0x22, 0x33, 0x44));
    CHECK(QwIs(vu, 204, 17, 0x22, 0xA1, 0xEEEEEEEEu));
    // Masked with WL=2: rows alternate 0,1,0,1.
    r.cycle = 0x0202;
    Run(vu, r, 1, 0x7C, 210, 4, p, 0);
    CHECK(QwIs(vu, 210, 1, 0x22, 0xA1, 0xEEEEEEEEu));
    CHECK(QwIs(vu, 211, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu));
    CHECK(QwIs(vu, 212, 9, 0x22, 0xA1, 0xEEEEEEEEu));
    CHECK(QwIs(vu, 213, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu, 0xEEEEEEEEu));
    // Mask + mode: the add happens before the mask selects.
    r.mode = 1;
    r.cycle = 0x0101;
    r.mask = 0x0000000Cu;  // field 1 NoWrite, others Write
    Run(vu, r, 1, 0x7C, 220, 1, Words({1, 2, 3, 4}), 0);
    CHECK(QwIs(vu, 220, 0x12, 0xEEEEEEEEu, 0x36, 0x48));
    // The vertex-colour idiom: V4-5 colours, then a masked S-8 alpha over the W field only
    // (xyz write-protected = 0x3F per cycle row); RGB survives, W takes the byte.
    r.mode = 0;
    r.cycle = 0x0104;
    r.mask = 0x3F3F3F3Fu;
    Run(vu, r, 1, 0x6F, kUsn | 230, 2, Words({0x7FFFFFFFu}), 0);       // 0xFFFF white/a1, then 0x7FFF white/a0
    CHECK(QwIs(vu, 230, 0xF8, 0xF8, 0xF8, 0x80));
    CHECK(QwIs(vu, 234, 0xF8, 0xF8, 0xF8, 0x00));
    Run(vu, r, 1, 0x72, kUsn | 230, 2, Words({0x00001E37u}), 0);       // S-8 masked: alpha 0x37, 0x1E
    CHECK(QwIs(vu, 230, 0xF8, 0xF8, 0xF8, 0x37));
    CHECK(QwIs(vu, 234, 0xF8, 0xF8, 0xF8, 0x1E));
}

void TestCycleShapes() {
    VuUnit vu(1);
    VifRegs r = DefaultRegs();
    Fill(vu, 0x00);

    // CL=4 WL=1: write one, skip three; the source is consumed only for writes.
    r.cycle = 0x0104;
    Run(vu, r, 1, 0x6C, 100, 3, Words({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}), 0);
    CHECK(QwIs(vu, 100, 1, 2, 3, 4));
    CHECK(QwIs(vu, 101, 0, 0, 0, 0));
    CHECK(QwIs(vu, 103, 0, 0, 0, 0));
    CHECK(QwIs(vu, 104, 5, 6, 7, 8));
    CHECK(QwIs(vu, 108, 9, 10, 11, 12));
    CHECK(QwIs(vu, 109, 0, 0, 0, 0));  // trailing skip cycles never run
    // CL=4 WL=2: write two, skip two.
    r.cycle = 0x0204;
    Run(vu, r, 1, 0x6C, 120, 3, Words({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}), 0);
    CHECK(QwIs(vu, 120, 1, 2, 3, 4));
    CHECK(QwIs(vu, 121, 5, 6, 7, 8));
    CHECK(QwIs(vu, 122, 0, 0, 0, 0));
    CHECK(QwIs(vu, 123, 0, 0, 0, 0));
    CHECK(QwIs(vu, 124, 9, 10, 11, 12));
    // CL=1 WL=2 (filling write): every cycle writes; the source runs out after
    // CL*(num/WL) vectors, the next vector's leading element takes the
    // lookahead word, the rest read zero.
    r.cycle = 0x0201;
    Run(vu, r, 1, 0x6C, 140, 4, Words({1, 2, 3, 4, 5, 6, 7, 8, 0x77}), 0);
    CHECK(QwIs(vu, 140, 1, 2, 3, 4));
    CHECK(QwIs(vu, 141, 5, 6, 7, 8));
    CHECK(QwIs(vu, 142, 0x77, 0, 0, 0));
    CHECK(QwIs(vu, 143, 0, 0, 0, 0));
    // CL/WL 0 are treated as 1.
    r.cycle = 0x0000;
    Run(vu, r, 1, 0x6C, 150, 2, Words({1, 2, 3, 4, 5, 6, 7, 8}), 0);
    CHECK(QwIs(vu, 150, 1, 2, 3, 4));
    CHECK(QwIs(vu, 151, 5, 6, 7, 8));

    // Wrap at 1024 quadwords.
    r.cycle = 0x0101;
    Run(vu, r, 1, 0x6C, 1022, 3, Words({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}), 0);
    CHECK(QwIs(vu, 1022, 1, 2, 3, 4));
    CHECK(QwIs(vu, 1023, 5, 6, 7, 8));
    CHECK(QwIs(vu, 0, 9, 10, 11, 12));
    // Skip-lane wrap.
    r.cycle = 0x0104;
    Run(vu, r, 1, 0x6C, 1020, 2, Words({1, 2, 3, 4, 5, 6, 7, 8}), 0);
    CHECK(QwIs(vu, 1020, 1, 2, 3, 4));
    CHECK(QwIs(vu, 0, 5, 6, 7, 8));

    // TOPS: added on VIF1 when bit 15 is set, ignored by VIF0.
    r.cycle = 0x0101;
    r.tops = 512;
    Run(vu, r, 1, 0x6C, kTops | 30, 1, Words({1, 2, 3, 4}), 0);
    CHECK(QwIs(vu, 542, 1, 2, 3, 4));
    r.tops = 1000;
    Run(vu, r, 1, 0x6C, kTops | 30, 1, Words({5, 6, 7, 8}), 0);
    CHECK(QwIs(vu, 6, 5, 6, 7, 8));  // (1000 + 30) & 0x3FF
    Run(vu, r, 1, 0x6C, 200, 1, Words({9, 9, 9, 9}), 0);  // no TOPS bit
    CHECK(QwIs(vu, 200, 9, 9, 9, 9));
    VuUnit vu0(0);
    VifRegs r0 = DefaultRegs();
    r0.tops = 512;
    Run(vu0, r0, 0, 0x6C, kTops | 30, 1, Words({1, 2, 3, 4}), 0);
    CHECK(QwIs(vu0, 30, 1, 2, 3, 4));

    // Partial payloads: elements past the end read zero, never crash.
    Run(vu, r, 1, 0x6C, 300, 2, Words({1, 2, 3, 4, 5}), 0);
    CHECK(QwIs(vu, 300, 1, 2, 3, 4));
    CHECK(QwIs(vu, 301, 5, 0, 0, 0));
    Run(vu, r, 1, 0x6F, 302, 3, Words({0x0421FFFFu}), 0);
    CHECK(QwIs(vu, 304, 0, 0, 0, 0));
    Run(vu, r, 1, 0x68, 310, 2, Words({1, 2, 3, 4, 5}), 0);
    CHECK(QwIs(vu, 310, 1, 2, 3, 4));
    CHECK(QwIs(vu, 311, 4, 5, 0, 0));
}

void TestVu0MaskedRead() {
    // VU0: byte address 300*16 = 0x12C0 lands at 0x2C0 through the 4 KB mask; the
    // masked read of the existing quadword goes through the same mask.
    VuUnit vu0(0);
    uint32_t seed[4] = {0xC0C0C0C0u, 0xC1C1C1C1u, 0xC2C2C2C2u, 0xC3C3C3C3u};
    vu0.WriteDataMem(0x2C0, reinterpret_cast<uint8_t*>(seed), 16);
    VifRegs r = DefaultRegs();
    r.mask = 0x000000FCu;  // field 0 Write, fields 1-3 NoWrite
    Run(vu0, r, 0, 0x7C, 300, 1, Words({1, 2, 3, 4}), 0);
    CHECK(QwIs(vu0, 0x2C, 1, 0xC1C1C1C1u, 0xC2C2C2C2u, 0xC3C3C3C3u));
}

// The bulk lanes (settings on) must produce the same VU1 data memory as the
// scalar loop (settings off) and take exactly the shapes they claim.
struct LaneCase {
    const char* name;
    uint32_t cmd, imm, num, cycle;
    uint8_t phase;
    int expect_lane;  // 0 = neither, 1 = bulk, 2 = skip
};

void TestBulkLanesEquivalence() {
    const LaneCase cases[] = {
        {"v432_n7", 0x6C, kUsn | 0, 7, 0x0101, 0, 1},
        {"v432_n256", 0x6C, 100, 0, 0x0101, 0, 1},
        {"v416_s", 0x6D, 40, 13, 0x0101, 0, 1},
        {"v416_u", 0x6D, kUsn | 40, 13, 0x0101, 0, 1},
        {"v48_s", 0x6E, 200, 9, 0x0101, 0, 1},
        {"v48_u", 0x6E, kUsn | 200, 9, 0x0101, 0, 1},
        {"v332_ph1", 0x68, 300, 5, 0x0101, 1, 1},
        {"v332_ph0", 0x68, 300, 5, 0x0101, 0, 1},
        {"v432_wrap", 0x6C, 1020, 9, 0x0101, 0, 1},
        {"v432_tops", 0x6C, kTops | 30, 6, 0x0101, 0, 1},
        {"v432_cl2wl2", 0x6C, 5, 6, 0x0202, 0, 1},
        {"masked_decline", 0x7C, 0, 4, 0x0101, 0, 0},
        {"s32_decline", 0x60, 128, 4, 0x0101, 0, 0},
        {"v45_decline", 0x6F, 140, 4, 0x0101, 0, 0},
        {"v216_decline", 0x65, 140, 4, 0x0101, 0, 0},
        {"skx_v332_cl4_ph0", 0x68, 300, 7, 0x0104, 0, 2},
        {"skx_v332_cl4_ph1", 0x68, 300, 7, 0x0104, 1, 2},
        {"skx_v432_cl4", 0x6C, 40, 6, 0x0104, 0, 2},
        {"skx_v432_cl4_usn", 0x6C, kUsn | 40, 6, 0x0104, 0, 2},
        {"skx_v48_cl4_s", 0x6E, 200, 9, 0x0104, 0, 2},
        {"skx_v48_cl4_u", 0x6E, kUsn | 200, 9, 0x0104, 0, 2},
        {"skx_v432_cl2", 0x6C, 480, 8, 0x0102, 0, 2},
        {"skx_v432_cl4_wrap", 0x6C, 1020, 6, 0x0104, 0, 2},
        {"skx_v332_cl4_wrap", 0x68, 1016, 5, 0x0104, 0, 2},
        {"skx_v48_cl4_n256", 0x6E, 200, 0, 0x0104, 0, 2},
        {"skx_v416_decline", 0x6D, 40, 4, 0x0104, 0, 0},
        {"skx_cl4wl2_decline", 0x6C, 64, 6, 0x0204, 0, 0},
    };
    uint32_t rng = 0xC0FFEEu;
    for (const LaneCase& c : cases) {
        VifRegs r = DefaultRegs();
        r.cycle = c.cycle;
        r.tops = 512;
        r.mask = 0x00000055u;
        const uint32_t n = c.num == 0 ? 256 : c.num;
        const uint32_t bytes = VifUnpack::GetUnpackSize(c.cmd, c.num, VifRegsCl(r), VifRegsWl(r));
        std::vector<uint8_t> payload(bytes + 4);  // + the lookahead word
        for (size_t i = 0; i < payload.size(); i += 4) {
            rng = rng * 1664525u + 1013904223u;
            std::memcpy(&payload[i], &rng, 4);
        }
        VuUnit a(1), b(1);
        for (uint32_t i = 0; i < Vu1State::kMemSize; i++) {
            const uint8_t byte = static_cast<uint8_t>(0xA5 + (i & 0xFF));
            a.WriteDataMem(i, &byte, 1);
            b.WriteDataMem(i, &byte, 1);
        }
        VifUnpack off(VifUnpackSettings{false}), on(VifUnpackSettings{true});
        Run(a, r, 1, c.cmd, c.imm, c.num, payload, c.phase, false, &off);
        Run(b, r, 1, c.cmd, c.imm, c.num, payload, c.phase, true, &on);
        const bool same = std::memcmp(a.state().data, b.state().data, Vu1State::kMemSize) == 0;
        const bool census_ok = off.bulk_cmds() == 0 && off.skip_cmds() == 0 &&
                               on.bulk_cmds() == (c.expect_lane == 1 ? 1u : 0u) &&
                               on.skip_cmds() == (c.expect_lane == 2 ? 1u : 0u) &&
                               on.bulk_qw() + on.skip_qw() == (c.expect_lane ? n : 0u);
        if (!same || !census_ok) {
            std::printf("FAIL lane case %s: same=%d bulk=%llu skip=%llu\n", c.name, same ? 1 : 0,
                        static_cast<unsigned long long>(on.bulk_cmds()),
                        static_cast<unsigned long long>(on.skip_cmds()));
            g_fail++;
        }
        // Short payload: the lane declines and the scalar loop's partial semantics hold.
        if (c.expect_lane != 0 && bytes >= 8) {
            VuUnit a2(1), b2(1);
            std::vector<uint8_t> short_payload(payload.begin(), payload.begin() + bytes - 4);
            VifUnpack off2(VifUnpackSettings{false}), on2(VifUnpackSettings{true});
            Run(a2, r, 1, c.cmd, c.imm, c.num, short_payload, c.phase, false, &off2);
            Run(b2, r, 1, c.cmd, c.imm, c.num, short_payload, c.phase, true, &on2);
            CHECK(std::memcmp(a2.state().data, b2.state().data, Vu1State::kMemSize) == 0);
            CHECK(on2.bulk_cmds() == 0 && on2.skip_cmds() == 0);
        }
    }
}

}  // namespace

int main() {
    TestUnpackSize();
    TestMaskTable();
    TestFormats();
    TestModeAndMask();
    TestCycleShapes();
    TestVu0MaskedRead();
    TestBulkLanesEquivalence();
    if (g_fail) {
        std::printf("VifUnpackScalar: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VifUnpackScalar: PASS\n");
    return 0;
}
