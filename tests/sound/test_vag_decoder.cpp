// tests/sound/test_vag_decoder.cpp -- VagDecoder vectors (PHASE4_PLAN section
// 4.1: the original tests/test_vag_decoder.cpp, ported): synthetic ADPCM blocks
// against the documented scheme (F0 = {0,60,115,98,122}, F1 = {0,0,-52,-55,-60},
// low nibble first, s16 clamp), the history across blocks, the malformed-field
// policy, the flag passthrough.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/sound/vag_decoder.h"

static int g_failures = 0;
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

using History = VagDecoder::History;

// An independent transcription of the documented scheme.
static void RefDecode(const uint8_t blk[16], int32_t& h1, int32_t& h2, int16_t out[28]) {
    static const int32_t F0[5] = {0, 60, 115, 98, 122};
    static const int32_t F1[5] = {0, 0, -52, -55, -60};
    uint32_t shift = blk[0] & 0xF;
    uint32_t filt = (blk[0] >> 4) & 0xF;
    if (shift > 12) shift = 12;
    if (filt > 4) filt = 0;
    for (int i = 0; i < 28; i++) {
        uint32_t nib = (i & 1) ? (blk[2 + i / 2] >> 4) : (blk[2 + i / 2] & 0xF);
        const int32_t signed_nibble = nib < 8 ? static_cast<int32_t>(nib) : static_cast<int32_t>(nib) - 16;
        int32_t s = signed_nibble * (4096 / static_cast<int32_t>(uint32_t{1} << shift));
        s += (h1 * F0[filt] + h2 * F1[filt]) >> 6;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
        h2 = h1;
        h1 = out[i];
    }
}

static void TestZeroBlockSilence() {
    uint8_t blk[16] = {};
    blk[1] = VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat;
    History h;
    int16_t out[28];
    const uint8_t flags = VagDecoder::DecodeBlock(blk, h, out);
    CHECK(flags == (VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat));
    for (int i = 0; i < 28; i++) CHECK(out[i] == 0);
    CHECK(h.h1 == 0 && h.h2 == 0);
}

static void TestFilter0Shift0Direct() {
    uint8_t blk[16] = {};
    blk[0] = 0x00;   // shift 0, filter 0
    blk[2] = 0x71;   // samples: +1, +7
    blk[3] = 0x8F;   // samples: -1 (0xF), -8 (0x8)
    History h;
    int16_t out[28];
    VagDecoder::DecodeBlock(blk, h, out);
    CHECK(out[0] == 1 << 12);    //  4096
    CHECK(out[1] == 7 << 12);    //  28672
    CHECK(out[2] == -4096);    // -1 scaled by 12 bits
    CHECK(out[3] == -32768);   // -8 scaled by 12 bits
    for (int i = 4; i < 28; i++) CHECK(out[i] == 0);
}

static void TestShiftScaling() {
    uint8_t blk[16] = {};
    blk[0] = 0x04;   // shift 4, filter 0
    blk[2] = 0x07;   // sample +7
    History h;
    int16_t out[28];
    VagDecoder::DecodeBlock(blk, h, out);
    CHECK(out[0] == (7 << 12) >> 4);   // 1792
}

static void TestFiltersAgainstReference() {
    for (int filt = 0; filt <= 4; filt++) {
        uint8_t blk[16];
        blk[0] = static_cast<uint8_t>((filt << 4) | 3);   // shift 3
        blk[1] = 0;
        uint32_t seed = 0x12345u + static_cast<uint32_t>(filt);
        for (int i = 2; i < 16; i++) {
            seed = seed * 1103515245u + 12345u;
            blk[i] = static_cast<uint8_t>(seed >> 16);
        }
        History h;
        h.h1 = 1000;
        h.h2 = -2000;
        int32_t rh1 = 1000, rh2 = -2000;
        int16_t got[28], want[28];
        VagDecoder::DecodeBlock(blk, h, got);
        RefDecode(blk, rh1, rh2, want);
        for (int i = 0; i < 28; i++) CHECK(got[i] == want[i]);
        CHECK(h.h1 == rh1 && h.h2 == rh2);
    }
}

static void TestHistoryAcrossBlocks() {
    uint8_t b1[16], b2[16];
    b1[0] = 0x22; b1[1] = 0;   // filter 2, shift 2
    b2[0] = 0x31; b2[1] = 0;   // filter 3, shift 1
    uint32_t seed = 999;
    for (int i = 2; i < 16; i++) {
        seed = seed * 1103515245u + 12345u; b1[i] = static_cast<uint8_t>(seed >> 13);
        seed = seed * 1103515245u + 12345u; b2[i] = static_cast<uint8_t>(seed >> 13);
    }
    History h;
    int16_t got[28];
    int32_t rh1 = 0, rh2 = 0;
    int16_t want[28];
    VagDecoder::DecodeBlock(b1, h, got);
    RefDecode(b1, rh1, rh2, want);
    VagDecoder::DecodeBlock(b2, h, got);
    RefDecode(b2, rh1, rh2, want);
    for (int i = 0; i < 28; i++) CHECK(got[i] == want[i]);
    CHECK(h.h1 == rh1 && h.h2 == rh2);
}

static void TestClampPositive() {
    uint8_t blk[16];
    blk[0] = 0x10;   // filter 1, shift 0
    blk[1] = 0;
    std::memset(blk + 2, 0x77, 14);   // all +7 nibbles
    History h;
    h.h1 = 32767;
    h.h2 = 32767;
    int16_t out[28];
    VagDecoder::DecodeBlock(blk, h, out);
    CHECK(out[0] == 32767);
    for (int i = 0; i < 28; i++) CHECK(out[i] <= 32767);
}

static void TestClampNegative() {
    uint8_t blk[16];
    blk[0] = 0x10;
    blk[1] = 0;
    std::memset(blk + 2, 0x88, 14);   // all -8 nibbles
    History h;
    h.h1 = -32768;
    h.h2 = -32768;
    int16_t out[28];
    VagDecoder::DecodeBlock(blk, h, out);
    CHECK(out[0] == -32768);
    for (int i = 0; i < 28; i++) CHECK(out[i] >= -32768);
}

static void TestMalformedFieldPolicy() {
    uint8_t bad[16] = {}, good[16] = {};
    bad[0] = 0xFF;    // filter 15, shift 15
    good[0] = 0x0C;   // filter 0, shift 12
    bad[2] = good[2] = 0x77;
    History hb, hg;
    int16_t ob[28], og[28];
    VagDecoder::DecodeBlock(bad, hb, ob);
    VagDecoder::DecodeBlock(good, hg, og);
    for (int i = 0; i < 28; i++) CHECK(ob[i] == og[i]);
}

static void TestFlagPassthrough() {
    uint8_t blk[16] = {};
    History h;
    int16_t out[28];
    blk[1] = VagDecoder::kFlagLoopStart;
    CHECK(VagDecoder::DecodeBlock(blk, h, out) == VagDecoder::kFlagLoopStart);
    blk[1] = VagDecoder::kFlagLoopEnd;
    CHECK(VagDecoder::DecodeBlock(blk, h, out) == VagDecoder::kFlagLoopEnd);
    blk[1] = VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat | VagDecoder::kFlagLoopStart;
    CHECK(VagDecoder::DecodeBlock(blk, h, out) ==
          (VagDecoder::kFlagLoopEnd | VagDecoder::kFlagLoopRepeat | VagDecoder::kFlagLoopStart));
}

static void TestNibbleOrder() {
    uint8_t blk[16] = {};
    blk[0] = 0x00;
    blk[2] = 0x21;   // low = 1 first, high = 2 second
    History h;
    int16_t out[28];
    VagDecoder::DecodeBlock(blk, h, out);
    CHECK(out[0] == 1 << 12);
    CHECK(out[1] == 2 << 12);
}

static void TestAllSignedNibblesAndShifts() {
    // Exhaust all 4-bit signed codes, including -8/-1/+7, at every legal
    // shift and both positions. These are integer mathematical expectations,
    // independent of RefDecode and free of signed shift operations.
    for (uint32_t shift = 0; shift <= 12; ++shift) {
        const int32_t scale = 4096 / static_cast<int32_t>(uint32_t{1} << shift);
        for (uint32_t nibble = 0; nibble < 16; ++nibble) {
            uint8_t block[16] = {};
            block[0] = static_cast<uint8_t>(shift);
            block[2] = static_cast<uint8_t>(nibble | (nibble << 4));
            History history;
            int16_t output[28];
            VagDecoder::DecodeBlock(block, history, output);
            const int32_t expected = (nibble < 8 ? static_cast<int32_t>(nibble) :
                                     static_cast<int32_t>(nibble) - 16) * scale;
            CHECK(output[0] == expected);
            CHECK(output[1] == expected);
            for (int i = 2; i < 28; ++i) CHECK(output[i] == 0);
            CHECK(history.h1 == 0 && history.h2 == 0);
        }
    }
}

int main() {
    TestZeroBlockSilence();
    TestFilter0Shift0Direct();
    TestShiftScaling();
    TestFiltersAgainstReference();   // 5 filter vectors
    TestHistoryAcrossBlocks();
    TestClampPositive();
    TestClampNegative();
    TestMalformedFieldPolicy();
    TestFlagPassthrough();
    TestNibbleOrder();
    TestAllSignedNibblesAndShifts();
    if (g_failures) {
        std::printf("test_vag_decoder: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("test_vag_decoder: all vectors passed (11 cases; 208 signed-nibble/shift pairs)\n");
    return 0;
}
