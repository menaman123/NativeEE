// tests/ipu/test_mpeg_decode.cpp -- the MpegDecode arithmetic (PHASE7_PLAN
// section 4.2): a DC-only block through InverseDct is flat at round(dc / 8);
// the zigzag scan is a permutation of 0..63; the DC size VLC tables decode
// their spec codes (ISO/IEC 13818-2 Tables B-12 / B-13) and the differential
// sign rule; the bit reader is MSB-first; the Table B-14 / B-15 run/level codes,
// escape and EOB and the non-intra first-coefficient rule; the quantiser scale
// (Table 7-6), inverse quantisation with saturation and mismatch control; Table
// B-9 coded block patterns; the alternate scan; the BDEC block forms; the
// YCbCr -> RGB32 macroblock conversion on constant blocks.
#include "engine/ipu/mpeg_decode.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using ps2r::ipu::BitCursor;
using ps2r::ipu::MpegDecode;
using ps2r::ipu::VlcEntry;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Builds a byte vector from a string of '0'/'1' characters (MSB first), padded
// with zero bits to a whole byte.
std::vector<uint8_t> BitsFromString(const char* s) {
    std::vector<uint8_t> out;
    uint8_t cur = 0;
    int n = 0;
    for (const char* p = s; *p; ++p) {
        if (*p != '0' && *p != '1') continue;
        cur = static_cast<uint8_t>((cur << 1) | (*p == '1' ? 1 : 0));
        if (++n == 8) {
            out.push_back(cur);
            cur = 0;
            n = 0;
        }
    }
    if (n) out.push_back(static_cast<uint8_t>(cur << (8 - n)));
    return out;
}

void TestZigzagBijection() {
    const uint8_t* zz = MpegDecode::Zigzag();
    bool seen[64] = {};
    for (int i = 0; i < 64; ++i) {
        CHECK(zz[i] < 64);
        CHECK(!seen[zz[i]]);
        seen[zz[i]] = true;
    }
    for (int i = 0; i < 64; ++i) CHECK(seen[i]);
    // Spec anchors of Figure 7-2: scan 0 -> 0, 1 -> 1, 2 -> 8, 3 -> 16, 63 -> 63.
    CHECK(zz[0] == 0 && zz[1] == 1 && zz[2] == 8 && zz[3] == 16 && zz[63] == 63);
    // Default intra matrix corners (6.3.11).
    const uint8_t* iq = MpegDecode::DefaultIntraQuantMatrix();
    CHECK(iq[0] == 8 && iq[1] == 16 && iq[8] == 16 && iq[63] == 83);
}

void TestIdctDcOnly() {
    // A block whose only coefficient is the DC term is flat: each of the two
    // separable passes scales by C(0) / 2 = 1 / (2 sqrt 2), so the value is dc / 8.
    for (int dc : {0, 8, 64, 800, -8, -2040, 2047, -2048, 13, -13}) {
        int16_t block[64] = {};
        block[0] = static_cast<int16_t>(dc);
        MpegDecode::InverseDct(block);
        const float exact = static_cast<float>(dc) / 8.0f;
        int expect = static_cast<int>(roundf(exact));
        if (expect > 255) expect = 255;
        if (expect < -256) expect = -256;
        for (int i = 0; i < 64; ++i) {
            // Within one unit of the exact value (float rounding at .5 boundaries).
            CHECK(std::abs(block[i] - expect) <= 1);
            CHECK(block[i] == block[0]);  // flat
        }
    }
    // Exact for multiples of 8 well inside the range.
    int16_t block[64] = {};
    block[0] = 640;
    MpegDecode::InverseDct(block);
    for (int i = 0; i < 64; ++i) CHECK(block[i] == 80);
    // A single first-row AC coefficient yields a horizontal cosine pattern that
    // varies along x and is constant down y.
    int16_t ac[64] = {};
    ac[1] = 400;
    MpegDecode::InverseDct(ac);
    CHECK(ac[0] > 0 && ac[7] < 0 && ac[0] == -ac[7]);
    for (int y = 1; y < 8; ++y) CHECK(ac[y * 8] == ac[0]);
}

void TestBitReader() {
    const uint8_t bytes[] = {0xA5, 0x0F};  // 1010 0101 0000 1111
    BitCursor c{bytes, sizeof(bytes), 0};
    CHECK(MpegDecode::HasBits(c, 16));
    CHECK(!MpegDecode::HasBits(c, 17));
    CHECK(MpegDecode::PeekBits(c, 4) == 0xA);
    CHECK(MpegDecode::ReadBits(c, 3) == 0b101);
    CHECK(c.bit_pos == 3);
    CHECK(MpegDecode::PeekBits(c, 5) == 0b00101);
    MpegDecode::SkipBits(c, 5);
    CHECK(MpegDecode::ReadBits(c, 8) == 0x0F);
    CHECK(!MpegDecode::HasBits(c, 1));
    CHECK(MpegDecode::PeekBits(c, 1) == 0);  // past the end reads 0
    CHECK(MpegDecode::PeekBits(c, 0) == 0);
}

void TestDcVlcTables() {
    const VlcEntry* lum = MpegDecode::DcLumTable();
    const VlcEntry* chr = MpegDecode::DcChromTable();
    // Table B-12 (luminance): 100 -> size 0, 00 -> 1, 01 -> 2, 101 -> 3, 110 -> 4,
    // 1110 -> 5, ..., 1 1111 1110 -> 10, 1 1111 1111 -> 11.
    CHECK(lum[0].code == 0b100 && lum[0].len == 3 && lum[0].value == 0);
    CHECK(lum[1].code == 0b00 && lum[1].len == 2 && lum[1].value == 1);
    CHECK(lum[2].code == 0b01 && lum[2].len == 2 && lum[2].value == 2);
    CHECK(lum[3].code == 0b101 && lum[3].len == 3 && lum[3].value == 3);
    CHECK(lum[4].code == 0b110 && lum[4].len == 3 && lum[4].value == 4);
    CHECK(lum[5].code == 0b1110 && lum[5].len == 4 && lum[5].value == 5);
    CHECK(lum[10].code == 0b111111110 && lum[10].len == 9 && lum[10].value == 10);
    CHECK(lum[11].code == 0b111111111 && lum[11].len == 9 && lum[11].value == 11);
    // Table B-13 (chrominance): 00 -> 0, 01 -> 1, 10 -> 2, 110 -> 3, ..., 11 1111 1111 -> 11.
    CHECK(chr[0].code == 0b00 && chr[0].len == 2 && chr[0].value == 0);
    CHECK(chr[1].code == 0b01 && chr[1].len == 2 && chr[1].value == 1);
    CHECK(chr[2].code == 0b10 && chr[2].len == 2 && chr[2].value == 2);
    CHECK(chr[3].code == 0b110 && chr[3].len == 3 && chr[3].value == 3);
    CHECK(chr[10].code == 0b1111111110 && chr[10].len == 10 && chr[10].value == 10);
    CHECK(chr[11].code == 0b1111111111 && chr[11].len == 10 && chr[11].value == 11);
    // Every code in each table is prefix-free against the others.
    for (const VlcEntry* t : {lum, chr}) {
        for (int i = 0; i < MpegDecode::kDcTableEntries; ++i) {
            for (int j = 0; j < MpegDecode::kDcTableEntries; ++j) {
                if (i == j) continue;
                if (t[i].len <= t[j].len) {
                    const uint32_t prefix = t[j].code >> (t[j].len - t[i].len);
                    CHECK(prefix != t[i].code);
                }
            }
        }
    }
}

void TestDecodeDcCoeff() {
    // Luminance: size code "01" (size 2) then differential "11" = 3 -> +3.
    {
        std::vector<uint8_t> b = BitsFromString("01 11");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 3);
        CHECK(c.bit_pos == 4);
    }
    // Luminance: size 2, differential "00" = 0 < 2 -> 0 - 3 = -3.
    {
        std::vector<uint8_t> b = BitsFromString("01 00");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == -3);
    }
    // Luminance: size code "100" = size 0 -> differential 0, consumes 3 bits.
    {
        std::vector<uint8_t> b = BitsFromString("100 11111");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(c.bit_pos == 3);
    }
    // Chrominance: "110" = size 3, differential "101" = 5 -> +5.
    {
        std::vector<uint8_t> b = BitsFromString("110 101");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 1) == 5);
        CHECK(c.bit_pos == 6);
    }
    // Chrominance: "1110" = size 4, differential "0110" = 6 < 8 -> 6 - 15 = -9.
    {
        std::vector<uint8_t> b = BitsFromString("1110 0110");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 2) == -9);
    }
    // Luminance size 8 ("1111 110", 7 bits) with differential "1111 1111" = 255.
    {
        std::vector<uint8_t> b = BitsFromString("1111110 11111111");
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 255);
        CHECK(c.bit_pos == 15);
    }
    // Differential bits missing: size code "1 1111 1110" (size 10) matched but only
    // 7 bits follow -> 0, and the cursor runs PAST the end (the "needs more bits"
    // signal: a block decoder must not take a truncated DC code for a complete one).
    {
        std::vector<uint8_t> b = BitsFromString("111111110 0000000");
        CHECK(b.size() == 2);
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(c.bit_pos == 9 + 10);
        CHECK(!MpegDecode::HasBits(c, 0));
    }
    // Size code cut off: "1111" at the end of the data could be any of sizes 5..11 ->
    // 0, cursor past the end.
    {
        std::vector<uint8_t> b = BitsFromString("0000 1111");
        BitCursor c{b.data(), b.size(), 4};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(!MpegDecode::HasBits(c, 0));
    }
    // A truncated DC code in a block decode fails the block (cursor past the end).
    {
        std::vector<uint8_t> b = BitsFromString("0000 1111");
        BitCursor c{b.data(), b.size(), 4};
        int dc_pred[3] = {128, 128, 128};
        int16_t block[64];
        CHECK(!MpegDecode::DecodeIntraBlock(c, block, 0, 2, MpegDecode::DefaultIntraQuantMatrix(), dc_pred, 0,
                                            false, false));
        CHECK(!MpegDecode::HasBits(c, 0));
    }
    // Empty stream: no code can match -> 0, cursor past the end.
    {
        BitCursor c{nullptr, 0, 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(c.bit_pos == 9);  // pushed by the longest luminance code
        BitCursor cc{nullptr, 0, 0};
        CHECK(MpegDecode::DecodeDcCoeff(cc, 1) == 0);
        CHECK(cc.bit_pos == 10);  // the longest chrominance code
    }
}

void TestDecodeAcCoeffs() {
    // Spec-derived vectors for Table B-14 (table zero, intra blocks: the first
    // coefficient after the DC is at scan position 1 and "10" is the end-of-block).
    const uint8_t* zz = MpegDecode::Zigzag();
    auto nonzero = [](const int16_t* block) {
        int n = 0;
        for (int i = 0; i < 64; ++i) n += block[i] != 0;
        return n;
    };
    // "11 s" = run 0, level 1, s = 0 -> +1 at scan 1 (raster 1); then EOB "10".
    {
        std::vector<uint8_t> b = BitsFromString("11 0 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(block[zz[1]] == 1 && zz[1] == 1);
        CHECK(nonzero(block) == 1);
        CHECK(c.bit_pos == 5);
    }
    // "011 s" = run 1, level 1, s = 1 -> -1 at scan 1 + 1 = 2 (raster 8); EOB.
    {
        std::vector<uint8_t> b = BitsFromString("011 1 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(block[zz[2]] == -1 && zz[2] == 8);
        CHECK(nonzero(block) == 1);
        CHECK(c.bit_pos == 6);
    }
    // "0100 s" = run 0, level 2 -> +2 at scan 1; then "0101 s" = run 2, level 1 -> -1 at
    // scan 2 + 2 = 4 (raster 9); EOB.
    {
        std::vector<uint8_t> b = BitsFromString("0100 0 0101 1 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(block[1] == 2 && block[9] == -1 && zz[4] == 9);
        CHECK(nonzero(block) == 2);
        CHECK(c.bit_pos == 12);
    }
    // ESCAPE "000001" run = 000101 (5) level = 0000 0111 1111 (127) -> scan 1 + 5 = 6
    // (raster 3); then EOB.
    {
        std::vector<uint8_t> b = BitsFromString("000001 000101 000001111111 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(block[zz[6]] == 127 && zz[6] == 3);
        CHECK(c.bit_pos == 6 + 6 + 12 + 2);
    }
    // ESCAPE with a negative 12-bit two's-complement level: 1111 1111 1110 = -2 at scan 1.
    {
        std::vector<uint8_t> b = BitsFromString("000001 000000 111111111110 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(block[1] == -2);
    }
    // The MPEG-1 escape (MP1): 8-bit level, 1000 0000 + 8 more bits = level - 256.
    {
        std::vector<uint8_t> b = BitsFromString("000001 000000 10000000 01111111 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block, false, true));
        CHECK(block[1] == 127 - 256);
        CHECK(c.bit_pos == 6 + 6 + 16 + 2);
    }
    // Table B-15 (table one, CTRL.IVF = 1): "10 s" = run 0 level 1; "010 s" = run 1
    // level 1 (-1 at scan 3, raster 16); "0111 s" = run 0 level 3 (+3 at scan 4,
    // raster 9); EOB is "0110".
    {
        std::vector<uint8_t> b = BitsFromString("10 0 010 1 0111 0 0110");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block, true));
        CHECK(block[1] == 1 && block[16] == -1 && block[9] == 3);
        CHECK(nonzero(block) == 3);
        CHECK(c.bit_pos == 3 + 4 + 5 + 4);
    }
    // Alternate scan (CTRL.AS): "11 0" = run 0 level +1 lands at alternate-scan
    // position 1 = raster 8 instead of raster 1.
    {
        std::vector<uint8_t> b = BitsFromString("11 0 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeAcCoeffs(c, block, false, false, true));
        CHECK(block[8] == 1 && block[1] == 0);
    }
    // Non-intra block (DecodeNonIntraCoeffs): the first coefficient reads "1 s" as run 0
    // level 1 (EOB cannot occur first), later ones "11 s"; here "1 0" -> +1 at scan 0,
    // "11 1" -> -1 at scan 1, EOB "10".
    {
        std::vector<uint8_t> b = BitsFromString("1 0 11 1 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeNonIntraCoeffs(c, block));
        CHECK(block[0] == 1 && block[1] == -1);
        CHECK(nonzero(block) == 2);
        CHECK(c.bit_pos == 7);
    }
    // Non-intra: "1 1" = -1 at scan 0 then EOB.
    {
        std::vector<uint8_t> b = BitsFromString("1 1 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeNonIntraCoeffs(c, block));
        CHECK(block[0] == -1 && nonzero(block) == 1);
        CHECK(c.bit_pos == 4);
    }
    // Non-intra: a run/level code first ("011 0" = run 1 level +1 at scan 1), EOB.
    {
        std::vector<uint8_t> b = BitsFromString("011 0 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeNonIntraCoeffs(c, block));
        CHECK(block[0] == 0 && block[1] == 1);
    }
    // Failures: a code that matches nothing (24 zero bits) -> false, cursor unchanged;
    // a run past the last coefficient (escape run 63 at scan 1) -> false; a code cut
    // off by the end of the data ("011" without its sign bit) -> false.
    {
        std::vector<uint8_t> b = BitsFromString("000000000000000000000000");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(!MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(c.bit_pos == 0);
    }
    {
        std::vector<uint8_t> b = BitsFromString("000001 111111 000000000001 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        CHECK(!MpegDecode::DecodeAcCoeffs(c, block));
    }
    {
        // "011" at the end of the data with no sign bit after it: cut off.
        std::vector<uint8_t> b = BitsFromString("00000 011");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 5};
        CHECK(!MpegDecode::DecodeAcCoeffs(c, block));
        CHECK(c.bit_pos == 5);
    }
}

void TestQuantiserScale() {
    // 7.4.2.2 Table 7-6: q_scale_type = 0 is linear (2 * code); q_scale_type = 1 is the
    // non-linear column: 1..8 -> 1..8, 9 -> 10, 10 -> 12, ..., 16 -> 24, 17 -> 28,
    // ..., 24 -> 56, 25 -> 64, ..., 31 -> 112.
    CHECK(MpegDecode::QuantiserScale(1, false) == 2);
    CHECK(MpegDecode::QuantiserScale(16, false) == 32);
    CHECK(MpegDecode::QuantiserScale(31, false) == 62);
    CHECK(MpegDecode::QuantiserScale(1, true) == 1);
    CHECK(MpegDecode::QuantiserScale(8, true) == 8);
    CHECK(MpegDecode::QuantiserScale(9, true) == 10);
    CHECK(MpegDecode::QuantiserScale(16, true) == 24);
    CHECK(MpegDecode::QuantiserScale(17, true) == 28);
    CHECK(MpegDecode::QuantiserScale(24, true) == 56);
    CHECK(MpegDecode::QuantiserScale(25, true) == 64);
    CHECK(MpegDecode::QuantiserScale(31, true) == 112);
    // The code is five bits; the non-linear table is monotonic.
    for (int code = 2; code < 32; ++code) {
        CHECK(MpegDecode::QuantiserScale(code, true) > MpegDecode::QuantiserScale(code - 1, true));
    }
}

void TestDequant() {
    uint8_t w[64];
    for (uint8_t& v : w) v = 16;
    // Non-intra (7.4.2.3): F'' = ((2 QF + sign QF) * W * qs) / 32 truncating toward zero.
    // QF = 1, W = 16, qs = 2 -> (3 * 16 * 2) / 32 = 3; QF = -1 -> -3. Sum = 0 (even) ->
    // mismatch control toggles F[63]'s LSB (7.4.4).
    {
        int16_t block[64] = {};
        block[0] = 1;
        block[1] = -1;
        MpegDecode::DequantNonIntra(block, w, 2);
        CHECK(block[0] == 3 && block[1] == -3);
        CHECK(block[63] == 1);
    }
    // An odd coefficient sum leaves F[63] alone.
    {
        int16_t block[64] = {};
        block[0] = 1;
        MpegDecode::DequantNonIntra(block, w, 2);
        CHECK(block[0] == 3 && block[63] == 0);
    }
    // F[63] itself taking part: QF[63] = 1 -> 3 (odd sum, stays 3); with QF[0] = 1 too the
    // sum is 6 -> F[63] becomes 2.
    {
        int16_t block[64] = {};
        block[63] = 1;
        MpegDecode::DequantNonIntra(block, w, 2);
        CHECK(block[63] == 3);
        int16_t two[64] = {};
        two[0] = 1;
        two[63] = 1;
        MpegDecode::DequantNonIntra(two, w, 2);
        CHECK(two[0] == 3 && two[63] == 2);
    }
    // Saturation (7.4.3) to [-2048, 2047].
    {
        int16_t block[64] = {};
        block[5] = 2047;
        block[6] = -2048;
        MpegDecode::DequantNonIntra(block, w, 112);
        CHECK(block[5] == 2047 && block[6] == -2048);
    }
    // Intra (7.4.2.3): F'' = (QF * W * qs) / 16 for the AC terms (the DC term is the
    // caller's); QF = 2, W = 16, qs = 4 -> 8. The DC term passes through the saturation.
    {
        int16_t block[64] = {};
        block[0] = 3000;
        block[1] = 2;
        block[2] = -3;
        MpegDecode::DequantIntra(block, w, 4);
        CHECK(block[0] == 2047 && block[1] == 8 && block[2] == -12);
    }
}

void TestCodedBlockPattern() {
    // Table B-9 anchors: "111" -> 60 (all four Y, no chroma), "1101" -> 4 (Y3 only),
    // "1010" -> 32 (Y0 only), "0011 00" -> 63 (everything), "0001 1111" -> 7,
    // "0000 0011 1" -> 31, "0000 0000 1" -> 0 (the MPEG-2 row).
    struct Vec { const char* bits; int cbp; int len; };
    const Vec vectors[] = {
        {"111", 60, 3}, {"1101", 4, 4}, {"1100", 8, 4}, {"1011", 16, 4}, {"1010", 32, 4},
        {"10011", 12, 5}, {"01011", 1, 5}, {"01000", 62, 5}, {"001100", 63, 6},
        {"0010111", 5, 7}, {"00011111", 7, 8}, {"00010000", 43, 8}, {"00000100", 58, 8},
        {"000000111", 31, 9}, {"000000010", 39, 9}, {"000000001", 0, 9},
    };
    for (const Vec& v : vectors) {
        std::vector<uint8_t> b = BitsFromString(v.bits);
        b.push_back(0xff);  // trailing data must not disturb the match
        BitCursor c{b.data(), b.size(), 0};
        int cbp = -1;
        CHECK(MpegDecode::DecodeCodedBlockPattern(c, cbp));
        CHECK(cbp == v.cbp);
        CHECK(c.bit_pos == static_cast<uint32_t>(v.len));
    }
    // Cut off ("00" of a longer code) -> false, cursor unchanged. The all-zero 9 bits
    // are not a code either.
    {
        std::vector<uint8_t> b = BitsFromString("00");
        BitCursor c{b.data(), 0, 0};
        int cbp = -1;
        CHECK(!MpegDecode::DecodeCodedBlockPattern(c, cbp));
        CHECK(c.bit_pos == 0 && cbp == -1);
        std::vector<uint8_t> z(2, 0);
        BitCursor zc{z.data(), z.size(), 0};
        CHECK(!MpegDecode::DecodeCodedBlockPattern(zc, cbp));
    }
}

void TestAlternateScan() {
    const uint8_t* alt = MpegDecode::AlternateScan();
    bool seen[64] = {};
    for (int i = 0; i < 64; ++i) {
        CHECK(alt[i] < 64);
        CHECK(!seen[alt[i]]);
        seen[alt[i]] = true;
    }
    // Figure 7-3 anchors: the scan runs down the first column first (0, 8, 16, 24),
    // then 1, 9, 2, 10; it ends at 63.
    CHECK(alt[0] == 0 && alt[1] == 8 && alt[2] == 16 && alt[3] == 24);
    CHECK(alt[4] == 1 && alt[5] == 9 && alt[6] == 2 && alt[7] == 10);
    CHECK(alt[63] == 63);
}

void TestBlockDecodeForms() {
    // DecodeIntraBlock: a DC-only luminance block. idp = 0: predictors start at 128;
    // DC size code "100" (size 0) keeps 128, EOB "10"; F[0] = 128 * 8 = 1024 and the
    // IDCT of a DC-only block is flat at 1024 / 8 = 128.
    const uint8_t* iq = MpegDecode::DefaultIntraQuantMatrix();
    {
        std::vector<uint8_t> b = BitsFromString("100 10");
        BitCursor c{b.data(), b.size(), 0};
        int dc_pred[3] = {128, 128, 128};
        int16_t block[64];
        CHECK(MpegDecode::DecodeIntraBlock(c, block, 0, 2, iq, dc_pred, 0, false, false));
        for (int i = 0; i < 64; ++i) CHECK(block[i] == 128);
        CHECK(dc_pred[0] == 128 && c.bit_pos == 5);
    }
    // DC differential "01 11" = +3 -> predictor 131, flat 131; the chroma predictor is
    // its own ("00 10" for Cb keeps 128).
    {
        std::vector<uint8_t> b = BitsFromString("01 11 10 00 10");
        BitCursor c{b.data(), b.size(), 0};
        int dc_pred[3] = {128, 128, 128};
        int16_t y[64], cb[64];
        CHECK(MpegDecode::DecodeIntraBlock(c, y, 0, 2, iq, dc_pred, 0, false, false));
        CHECK(MpegDecode::DecodeIntraBlock(c, cb, 1, 2, iq, dc_pred, 0, false, false));
        CHECK(y[0] == 131 && y[63] == 131 && cb[0] == 128);
        CHECK(dc_pred[0] == 131 && dc_pred[1] == 128);
    }
    // 9-bit intra DC precision (idp = 1): F[0] = pred * 4; pred 256 -> flat 128 too.
    {
        std::vector<uint8_t> b = BitsFromString("100 10");
        BitCursor c{b.data(), b.size(), 0};
        int dc_pred[3] = {256, 256, 256};
        int16_t block[64];
        CHECK(MpegDecode::DecodeIntraBlock(c, block, 0, 2, iq, dc_pred, 1, false, false));
        CHECK(block[0] == 128 && block[63] == 128);
    }
    // DecodeNonIntraBlock: "1 0" (QF[0] = +1) then EOB with the flat-16 matrix and qs = 62
    // (code 31, linear): F[0] = (3 * 16 * 62) / 32 = 93 (odd sum: no mismatch toggle);
    // IDCT -> 93 / 8 = 11.625 -> 12 everywhere.
    {
        std::vector<uint8_t> b = BitsFromString("1 0 10");
        BitCursor c{b.data(), b.size(), 0};
        uint8_t w[64];
        for (uint8_t& v : w) v = 16;
        int16_t block[64];
        CHECK(MpegDecode::DecodeNonIntraBlock(c, block, 62, w, false));
        for (int i = 0; i < 64; ++i) CHECK(block[i] == 12);
        CHECK(c.bit_pos == 4);
    }
}

void TestYCbCrToRgb32() {
    int16_t y[4][64];
    int16_t cb[64];
    int16_t cr[64];
    uint8_t out[MpegDecode::kMacroblockRgb32Bytes];

    // Zero chroma, mid luma: grey with alpha 0x80.
    for (auto& blk : y) for (int16_t& v : blk) v = 100;
    for (int16_t& v : cb) v = 0;
    for (int16_t& v : cr) v = 0;
    MpegDecode::YCbCrToRgb32(y, cb, cr, out, false);
    for (int i = 0; i < 256; ++i) {
        CHECK(out[i * 4 + 0] == 100 && out[i * 4 + 1] == 100 && out[i * 4 + 2] == 100);
        CHECK(out[i * 4 + 3] == 0x80);
    }
    // SGN adds 128 to each channel before the clamp.
    for (auto& blk : y) for (int16_t& v : blk) v = -28;
    MpegDecode::YCbCrToRgb32(y, cb, cr, out, true);
    for (int i = 0; i < 256; ++i) CHECK(out[i * 4] == 100 && out[i * 4 + 1] == 100 && out[i * 4 + 2] == 100);
    // Chroma applied with the fixed-point BT.601 coefficients (x1024):
    // Cr = 64 -> R += (64 * 1436) >> 10 = 89; G -= (64 * 731) >> 10 = 45; B unchanged.
    for (auto& blk : y) for (int16_t& v : blk) v = 100;
    for (int16_t& v : cr) v = 64;
    MpegDecode::YCbCrToRgb32(y, cb, cr, out, false);
    CHECK(out[0] == 189 && out[1] == 55 && out[2] == 100);
    // Each Y block maps to its quadrant: Y3 (bottom-right) alone at 255 -> pixel (15,15) white-ish.
    for (auto& blk : y) for (int16_t& v : blk) v = 0;
    for (int16_t& v : y[3]) v = 255;
    for (int16_t& v : cr) v = 0;
    MpegDecode::YCbCrToRgb32(y, cb, cr, out, false);
    CHECK(out[(15 * 16 + 15) * 4] == 255);
    CHECK(out[(0 * 16 + 0) * 4] == 0);
    CHECK(out[(8 * 16 + 7) * 4] == 0);   // bottom-left quadrant is Y2
    CHECK(out[(8 * 16 + 8) * 4] == 255); // first pixel of Y3
}

}  // namespace

int main() {
    TestZigzagBijection();
    TestIdctDcOnly();
    TestBitReader();
    TestDcVlcTables();
    TestDecodeDcCoeff();
    TestDecodeAcCoeffs();
    TestQuantiserScale();
    TestDequant();
    TestCodedBlockPattern();
    TestAlternateScan();
    TestBlockDecodeForms();
    TestYCbCrToRgb32();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_mpeg_decode: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_mpeg_decode: PASS (zigzag, idct, bits, dc-vlc, dc-coeff, ac-coeff, qscale, dequant, cbp, "
                "alt-scan, block-forms, ycbcr)\n");
    return 0;
}
