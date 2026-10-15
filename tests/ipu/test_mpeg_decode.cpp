// tests/ipu/test_mpeg_decode.cpp -- the MpegDecode arithmetic (PHASE7_PLAN
// section 4.2): a DC-only block through InverseDct is flat at round(dc / 8);
// the zigzag scan is a permutation of 0..63; the DC size VLC tables decode
// their spec codes (ISO/IEC 13818-2 Tables B-12 / B-13) and the differential
// sign rule; the bit reader is MSB-first; AC run/level escape and EOB; the
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
    // Differential bits missing: size code "1 1111 1110" (size 9) matched but only
    // 7 bits follow -> 0, cursor left after the size code.
    {
        std::vector<uint8_t> b = BitsFromString("111111110 0000000");
        CHECK(b.size() == 2);
        BitCursor c{b.data(), b.size(), 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(c.bit_pos == 9);
    }
    // Empty stream: no code matches, nothing consumed.
    {
        BitCursor c{nullptr, 0, 0};
        CHECK(MpegDecode::DecodeDcCoeff(c, 0) == 0);
        CHECK(c.bit_pos == 0);
    }
}

void TestDecodeAcCoeffs() {
    const uint8_t* zz = MpegDecode::Zigzag();
    // "1 0" -> +1 at scan 1; "1 1" -> -1 at scan 2; EOB "10".
    {
        std::vector<uint8_t> b = BitsFromString("10 11 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        MpegDecode::DecodeAcCoeffs(c, block);
        // "10" is also the EOB code: the first pair is read as EOB, so nothing lands.
        CHECK(block[zz[1]] == 0);
        CHECK(c.bit_pos == 2);
    }
    // "11" -> -1 at scan 1, then EOB.
    {
        std::vector<uint8_t> b = BitsFromString("11 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        MpegDecode::DecodeAcCoeffs(c, block);
        CHECK(block[zz[1]] == -1);
        CHECK(c.bit_pos == 4);
        int nonzero = 0;
        for (int i = 0; i < 64; ++i) nonzero += block[i] != 0;
        CHECK(nonzero == 1);
    }
    // Escape: "000001" run=000101 (5) level=000001111111 (127) -> scan 1 + 5 = 6; then EOB.
    {
        std::vector<uint8_t> b = BitsFromString("000001 000101 000001111111 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        MpegDecode::DecodeAcCoeffs(c, block);
        CHECK(block[zz[6]] == 127);
        CHECK(c.bit_pos == 6 + 6 + 12 + 2);
    }
    // Escape with a negative 12-bit level: 1111 1111 1110 = 4094 -> -2.
    {
        std::vector<uint8_t> b = BitsFromString("000001 000000 111111111110 10");
        int16_t block[64] = {};
        BitCursor c{b.data(), b.size(), 0};
        MpegDecode::DecodeAcCoeffs(c, block);
        CHECK(block[zz[1]] == -2);
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
    TestYCbCrToRgb32();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_mpeg_decode: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_mpeg_decode: PASS (zigzag, idct, bits, dc-vlc, dc-coeff, ac-coeff, ycbcr)\n");
    return 0;
}
