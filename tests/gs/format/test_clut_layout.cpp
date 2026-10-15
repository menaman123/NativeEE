// tests/gs/format/test_clut_layout.cpp -- ClutLayout property tests: Csm1Index is
// an involution on the 256-entry grid and identity on the 16-entry rectangle;
// the grid matches the GS_SKILL section 2.7.3 figure; the CSM1 CT32 rectangle
// is the 1 KB of blocks 0-3 at CBP; the CT16 IDTEX4 order is the section 8.3.2
// column order; CSM2 is linear; reads return raw bytes.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/psm_layout.h"

using ps2r::gs::ClutLayout;
using ps2r::gs::Psm;
using ps2r::gs::PsmLayout;

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

}  // namespace

int main() {
    // Involution + permutation on 0..255.
    std::vector<int> hits(256, 0);
    for (uint32_t i = 0; i < 256; i++) {
        const uint32_t p = ClutLayout::Csm1Index(i);
        CHECK(p < 256, "Csm1Index(%u) = %u out of range", i, p);
        CHECK(ClutLayout::Csm1Index(p) == i, "Csm1Index not an involution at %u", i);
        if (p < 256) hits[p]++;
    }
    for (int p = 0; p < 256; p++) CHECK(hits[p] == 1, "position %d hit %d times", p, hits[p]);

    // Section 2.7.3 figure, row r of the 16x16 grid: entries (r/2)*32 + (r%2)*8 + 0..7
    // in the left half, the same + 16 in the right half.
    for (uint32_t r = 0; r < 16; r++)
        for (uint32_t c = 0; c < 16; c++) {
            const uint32_t entry = (r / 2) * 32 + (r % 2) * 8 + (c % 8) + (c / 8) * 16;
            const ClutLayout::Position pos = ClutLayout::Csm1Position(entry, ClutLayout::kEntries8);
            CHECK(pos.x == c && pos.y == r, "figure: entry 0x%02x at (%u,%u), got (%u,%u)", entry, c, r, pos.x, pos.y);
        }
    // Row 0 literally: 00..07 | 10..17 ; row 1: 08..0f | 18..1f.
    CHECK(ClutLayout::Csm1Index(0x10) == 8 && ClutLayout::Csm1Index(0x08) == 16 && ClutLayout::Csm1Index(0x18) == 24 &&
          ClutLayout::Csm1Index(0x07) == 7 && ClutLayout::Csm1Index(0xE8) == 0xF0 && ClutLayout::Csm1Index(0xFF) == 0xFF,
          "row 0/1 spot values");

    // 16-entry rectangle: identity in its own 8x2 row-major order, and equal to
    // the 16x16 formula's first sixteen positions.
    for (uint32_t i = 0; i < 16; i++) {
        const ClutLayout::Position p4 = ClutLayout::Csm1Position(i, ClutLayout::kEntries4);
        const ClutLayout::Position p8 = ClutLayout::Csm1Position(i, ClutLayout::kEntries8);
        CHECK(p4.x == (i % 8) && p4.y == (i / 8), "IDTEX4 entry %u at (%u,%u)", i, p4.x, p4.y);
        CHECK(p4.x == p8.x && p4.y == p8.y, "IDTEX4 position differs from the IDTEX8 formula at %u", i);
    }

    // CSM1 CT32, 256 entries: the word addresses are exactly cbp*64 .. cbp*64+255
    // (blocks 0-3 of the page: a 1 KB contiguous CLUT), each hit once.
    for (uint32_t cbp : {0u, 0x3308u, 16383u}) {
        std::vector<int> seen(256, 0);
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t a = ClutLayout::EntryAddress(Psm::CT32, cbp, false, 256, i);
            const uint32_t rel = (a + (1u << 20) - (cbp * 64u) % (1u << 20)) % (1u << 20);
            CHECK(rel < 256, "CT32 CSM1 entry %u outside the 1 KB at cbp %u (word %u)", i, cbp, a);
            if (rel < 256) seen[rel]++;
        }
        for (int k = 0; k < 256; k++) CHECK(seen[k] == 1, "CT32 CSM1 word %d hit %d times", k, seen[k]);
    }
    // Entry i's word within the 1 KB (section 8.3.1 column order applied to the grid):
    // entries 0-7 -> 0,1,4,5,8,9,12,13 ; 8-15 -> 2,3,6,7,10,11,14,15 ; 16-23 -> block 1 (64+).
    const uint32_t kW0[8] = {0, 1, 4, 5, 8, 9, 12, 13}, kW1[8] = {2, 3, 6, 7, 10, 11, 14, 15};
    for (uint32_t i = 0; i < 8; i++) {
        CHECK(ClutLayout::EntryAddress(Psm::CT32, 0, false, 256, i) == kW0[i], "CT32 entry %u word", i);
        CHECK(ClutLayout::EntryAddress(Psm::CT32, 0, false, 256, 8 + i) == kW1[i], "CT32 entry %u word", 8 + i);
        CHECK(ClutLayout::EntryAddress(Psm::CT32, 0, false, 256, 16 + i) == 64 + kW0[i], "CT32 entry %u word", 16 + i);
    }

    // CSM1 CT16, 16 entries: the section 8.3.2 column order (halfword units).
    const uint32_t kT16I4[16] = {0, 2, 8, 10, 16, 18, 24, 26, 4, 6, 12, 14, 20, 22, 28, 30};
    for (uint32_t i = 0; i < 16; i++)
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 16, i) == kT16I4[i], "CT16 IDTEX4 entry %u", i);
    // CSM1 CT16, 256 entries: blocks 0 and 1 of the page (512 B), each halfword once.
    {
        std::vector<int> seen(256, 0);
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t a = ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, i);
            CHECK(a < 256, "CT16 CSM1 entry %u outside 512 B (halfword %u)", i, a);
            if (a < 256) seen[a]++;
        }
        for (int k = 0; k < 256; k++) CHECK(seen[k] == 1, "CT16 CSM1 halfword %d hit %d times", k, seen[k]);
    }
    // CSM1 CT16, 256 entries: the ORDER, pinned explicitly. Entry i sits at grid
    // (gx, gy) = (Csm1Index(i) % 16, Csm1Index(i) / 16) of the 16x16 rectangle, read
    // through the PSMCT16 layout with bw = 1 (section 2.7.3 / 3.4.7): grid rows 0-7
    // are block 0, rows 8-15 block 1 (section 8.3.2 block table, first column);
    // each pair of rows is one 16x2 column (32 halfwords); inside the column the
    // word order is the section 8.3.1 pattern 0,1,4,5,8,9,12,13 (row 0) /
    // 2,3,6,7,10,11,14,15 (row 1) and x >= 8 takes the HIGH halfword of the word
    // x - 8 uses -- the IDTEX4 table above is this formula's x < 8 prefix.
    {
        const uint32_t kWordRow0[8] = {0, 1, 4, 5, 8, 9, 12, 13}, kWordRow1[8] = {2, 3, 6, 7, 10, 11, 14, 15};
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t g = ClutLayout::Csm1Index(i), gx = g % 16, gy = g / 16;
            const uint32_t block = gy / 8, column = (gy % 8) / 2;
            const uint32_t word = (gy % 2) ? kWordRow1[gx % 8] : kWordRow0[gx % 8];
            const uint32_t want = block * 128 + column * 32 + word * 2 + gx / 8;
            CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, i) == want, "CT16 IDTEX8 entry %u halfword", i);
        }
        // Literal spot values of the same order (entry -> halfword):
        //   0..7 -> 0,2,8,10,16,18,24,26 (row 0, low halves)   8..15 -> 4,6,12,14,20,22,28,30 (row 1)
        //   16..23 -> 1,3,9,11,17,19,25,27 (row 0, high halves) 24..31 -> 5,7,13,15,21,23,29,31
        //   32 -> 32 (column 1)   64 -> 64   96 -> 96   128 -> 128 (block 1)   255 -> 255
        const uint32_t kFirst32[32] = {0, 2, 8, 10, 16, 18, 24, 26, 4, 6, 12, 14, 20, 22, 28, 30,
                                       1, 3, 9, 11, 17, 19, 25, 27, 5, 7, 13, 15, 21, 23, 29, 31};
        for (uint32_t i = 0; i < 32; i++)
            CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, i) == kFirst32[i], "CT16 IDTEX8 entry %u literal", i);
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, 32) == 32, "CT16 IDTEX8 entry 32 -> column 1");
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, 64) == 64, "CT16 IDTEX8 entry 64 -> column 2");
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, 96) == 96, "CT16 IDTEX8 entry 96 -> column 3");
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, 128) == 128, "CT16 IDTEX8 entry 128 -> block 1");
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 256, 255) == 255, "CT16 IDTEX8 entry 255 -> last halfword");
        // IDTEX4 (8x2 rectangle) is the first two grid rows of the same layout.
        for (uint32_t i = 0; i < 16; i++)
            CHECK(ClutLayout::EntryAddress(Psm::CT16, 0, false, 16, i) == kFirst32[i], "CT16 IDTEX4 entry %u == IDTEX8 prefix", i);
    }
    // CT16S shares the first two block numbers with CT16 -> same CLUT addresses.
    for (uint32_t i = 0; i < 256; i++)
        CHECK(ClutLayout::EntryAddress(Psm::CT16S, 7, false, 256, i) == ClutLayout::EntryAddress(Psm::CT16, 7, false, 256, i),
              "CT16S CLUT address differs from CT16 at %u", i);

    // CSM2 CT16: linear halfwords at cbp (scope cut, documented).
    for (uint32_t i = 0; i < 256; i++)
        CHECK(ClutLayout::EntryAddress(Psm::CT16, 5, true, 256, i) == 5 * 128 + i, "CSM2 entry %u not linear", i);
    CHECK(ClutLayout::Csm2Index(77) == 77, "Csm2Index identity");

    // Raw reads: the bytes at the computed addresses, little-endian, no alpha fold.
    {
        std::vector<uint8_t> vram(PsmLayout::kVramBytes, 0);
        const uint32_t cbp = 0x3308;
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t a = ClutLayout::EntryAddress(Psm::CT32, cbp, false, 256, i) * 4u;
            vram[a] = uint8_t(i); vram[a + 1] = uint8_t(i ^ 0x5A); vram[a + 2] = uint8_t(255 - i); vram[a + 3] = 0x80;
        }
        for (uint32_t i = 0; i < 256; i++) {
            const uint32_t want = uint32_t(i) | (uint32_t(i ^ 0x5A) << 8) | (uint32_t(255 - i) << 16) | (0x80u << 24);
            CHECK(ClutLayout::ReadClut32(vram.data(), cbp, false, 256, i) == want, "ReadClut32 entry %u", i);
        }
        uint32_t table[256];
        ClutLayout::ReadClut(vram.data(), Psm::CT32, cbp, false, 256, table);
        CHECK(table[0x10] == ClutLayout::ReadClut32(vram.data(), cbp, false, 256, 0x10), "ReadClut table == per entry");
        for (uint32_t i = 0; i < 16; i++) {
            const uint32_t a = ClutLayout::EntryAddress(Psm::CT16, cbp, false, 16, i) * 2u;
            vram[a] = uint8_t(0x11 * i); vram[a + 1] = uint8_t(0x80 | i);
        }
        for (uint32_t i = 0; i < 16; i++)
            CHECK(ClutLayout::ReadClut16(vram.data(), Psm::CT16, cbp, false, 16, i) == ((0x80u | i) << 8 | (0x11u * i)),
                  "ReadClut16 entry %u", i);
    }

    std::printf("test_clut_layout: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
