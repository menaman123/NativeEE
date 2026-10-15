// tests/core/test_psm_layout.cpp -- PsmLayout unit tests (PHASE2_PLAN section 4 item 5,
// 2A slice): descriptor table, page/block/column bijection per PSM, arithmetic
// identities, unit conversions, and the 4 MB wrap rule.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/gs/format/psm_layout.h"

using ps2r::gs::kPsmCount;
using ps2r::gs::Psm;
using ps2r::gs::PsmClass;
using ps2r::gs::PsmDescriptor;
using ps2r::gs::PsmLayout;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail++; } } while (0)

static void TestDescriptors() {
    CHECK(kPsmCount == 13);
    for (int i = 0; i < kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        // Section 8.1: page = 8 KB, block = 256 B, column = 64 B for every mode.
        CHECK(uint32_t(d.page_w) * d.page_h * d.storage_bits == PsmLayout::kPageBytes * 8);
        CHECK(uint32_t(d.block_w) * d.block_h * d.storage_bits == PsmLayout::kBlockBytes * 8);
        CHECK(uint32_t(d.column_w) * d.column_h * d.storage_bits == PsmLayout::kColumnBytes * 8);
        CHECK(d.page_w / d.block_w * (d.page_h / d.block_h) == PsmLayout::kBlocksPerPage);
        CHECK(d.block_h / d.column_h == PsmLayout::kColumnsPerBlock && d.block_w == d.column_w);
        Psm back;
        CHECK(PsmLayout::FromField(uint32_t(d.psm), &back) && back == d.psm);
        CHECK(&PsmLayout::Describe(d.psm) == &d);
    }
    Psm dummy;
    int defined = 0;
    for (uint32_t f = 0; f < 64; f++) defined += PsmLayout::FromField(f, &dummy) ? 1 : 0;
    CHECK(defined == 13);
    CHECK(PsmLayout::Describe(Psm::T4).page_w == 128 && PsmLayout::Describe(Psm::T4).page_h == 128);
    CHECK(PsmLayout::Describe(Psm::T8).page_w == 128 && PsmLayout::Describe(Psm::T8).page_h == 64);
    CHECK(PsmLayout::Describe(Psm::CT16S).page_h == 64 && PsmLayout::Describe(Psm::Z32).is_z);
    CHECK(PsmLayout::Describe(Psm::T8H).shares_ct32_word && PsmLayout::Describe(Psm::T8H).index_shift == 24);
    CHECK(PsmLayout::Describe(Psm::T4HH).index_shift == 28 && PsmLayout::Describe(Psm::T4HL).index_shift == 24);
}

// Every (x, y) of a page maps to a distinct element and all elements are covered;
// block tables are permutations of 0..31; a block's elements are contiguous.
static void TestBijection() {
    for (int i = 0; i < kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        const uint32_t per_page = PsmLayout::ElementsPerPage(d.psm);
        std::vector<uint8_t> seen(per_page, 0);
        for (uint32_t y = 0; y < d.page_h; y++)
            for (uint32_t x = 0; x < d.page_w; x++) {
                uint32_t e = PsmLayout::ElementInPage(d.psm, x, y);
                CHECK(e < per_page);
                if (e < per_page) seen[e]++;
            }
        uint32_t covered = 0;
        for (uint8_t s : seen) covered += (s == 1);
        CHECK(covered == per_page);

        uint32_t block_mask = 0;
        for (uint32_t by = 0; by < d.page_h / d.block_h; by++)
            for (uint32_t bx = 0; bx < d.page_w / d.block_w; bx++)
                block_mask |= 1u << PsmLayout::BlockInPage(d.psm, bx, by);
        CHECK(block_mask == 0xFFFFFFFFu);

        const uint32_t per_block = PsmLayout::ElementsPerBlock(d.psm);
        const uint32_t per_col = PsmLayout::ElementsPerColumn(d.psm);
        for (uint32_t by = 0; by < d.block_h; by++)
            for (uint32_t bx = 0; bx < d.block_w; bx++) {
                uint32_t e = PsmLayout::ElementInBlock(d.psm, bx, by);
                CHECK(e < per_block);
                CHECK(e / per_col == by / d.column_h);  // element lies in its column
            }
    }
}

// Section 8.3.1 spot values (the figure) and the class-sharing rule of 8.3.
static void TestArithmetic() {
    // 8x2 column word order.
    const uint32_t row0[8] = {0, 1, 4, 5, 8, 9, 12, 13}, row1[8] = {2, 3, 6, 7, 10, 11, 14, 15};
    for (uint32_t x = 0; x < 8; x++) {
        CHECK(PsmLayout::ElementInBlock(Psm::CT32, x, 0) == row0[x]);
        CHECK(PsmLayout::ElementInBlock(Psm::CT32, x, 1) == row1[x]);
        CHECK(PsmLayout::ElementInBlock(Psm::CT32, x, 7) == 48 + row1[x]);
    }
    // 16-bit: texel 8 of row 0 is the high half of word 0 -> halfword 1.
    CHECK(PsmLayout::ElementInBlock(Psm::CT16, 8, 0) == 1);
    CHECK(PsmLayout::ElementInBlock(Psm::CT16, 1, 0) == 2);
    // T8 section 8.3.3: row 2 of column 0 starts at word 8 byte 1 = byte 33; column 1 row 0 = byte 96.
    CHECK(PsmLayout::ElementInBlock(Psm::T8, 0, 2) == 33);
    CHECK(PsmLayout::ElementInBlock(Psm::T8, 0, 4) == 96);
    CHECK(PsmLayout::ElementInBlock(Psm::T8, 0, 6) == 65);
    // T4 section 8.3.4: nibble units; row 0 texel 8 -> word 0 nibble 2; row 2 texel 0 -> word 8 nibble 1.
    CHECK(PsmLayout::ElementInBlock(Psm::T4, 8, 0) == 2);
    CHECK(PsmLayout::ElementInBlock(Psm::T4, 0, 2) == 65);
    CHECK(PsmLayout::ElementInBlock(Psm::T4, 1, 0) == 8);
    // Block tables: colour vs Z vs S members differ; T8 shares CT32's, T4 shares CT16's.
    CHECK(PsmLayout::BlockInPage(Psm::CT32, 0, 0) == 0 && PsmLayout::BlockInPage(Psm::Z32, 0, 0) == 24);
    CHECK(PsmLayout::BlockInPage(Psm::CT16, 2, 0) == 8 && PsmLayout::BlockInPage(Psm::CT16S, 2, 0) == 16);
    CHECK(PsmLayout::BlockInPage(Psm::Z16, 0, 0) == 24 && PsmLayout::BlockInPage(Psm::Z16S, 2, 2) == 0);
    for (uint32_t by = 0; by < 4; by++)
        for (uint32_t bx = 0; bx < 8; bx++)
            CHECK(PsmLayout::BlockInPage(Psm::T8, bx, by) == PsmLayout::BlockInPage(Psm::CT32, bx, by));
    for (uint32_t by = 0; by < 8; by++)
        for (uint32_t bx = 0; bx < 4; bx++)
            CHECK(PsmLayout::BlockInPage(Psm::T4, bx, by) == PsmLayout::BlockInPage(Psm::CT16, bx, by));
    // Section 8.2: page arithmetic.
    CHECK(PsmLayout::PagesPerRow(Psm::CT32, 10) == 10);
    CHECK(PsmLayout::PagesPerRow(Psm::T8, 2) == 1 && PsmLayout::PagesPerRow(Psm::T8, 1) == 1);
    CHECK(PsmLayout::PagesPerRow(Psm::T4, 4) == 2);
    CHECK(PsmLayout::PixelAddress(Psm::CT32, 64, 0, 0, 10) == 2048);       // page 1
    CHECK(PsmLayout::PixelAddress(Psm::CT32, 0, 32, 0, 10) == 10 * 2048);  // page row 1
    CHECK(PsmLayout::PixelAddress(Psm::CT32, 8, 0, 0, 1) == 64);           // block 1
}

static void TestUnits() {
    // bp is in 256-byte blocks: block 1 shifts every mode by 256 bytes (relative to
    // bp 0 -- pixel (0,0) is block 24 of the page for the Z modes, block 0 otherwise).
    for (int i = 0; i < kPsmCount; i++) {
        Psm p = PsmLayout::Table()[i].psm;
        const uint32_t base = PsmLayout::PixelByteAddress(p, 0, 0, 0, 1);
        CHECK(base == (PsmLayout::Table()[i].is_z ? 24u * 256u : 0u));
        CHECK(PsmLayout::PixelByteAddress(p, 0, 0, 1, 1) == base + 256);
        CHECK(PsmLayout::PixelByteAddress(p, 0, 0, 32, 1) == base + 8192);  // one page = 32 blocks
        CHECK(PsmLayout::PixelBitInByte(p, 0, 0, 0, 1) == 0);
    }
    // Element units per class.
    CHECK(PsmLayout::ElementsPerPage(Psm::CT32) == 2048 && PsmLayout::ElementsPerPage(Psm::CT16) == 4096);
    CHECK(PsmLayout::ElementsPerPage(Psm::T8) == 8192 && PsmLayout::ElementsPerPage(Psm::T4) == 16384);
    CHECK(PsmLayout::ElementsPerBlock(Psm::T4) == 512 && PsmLayout::ElementsPerColumn(Psm::T8) == 64);
    // Byte address = element * bits / 8; T4 odd nibble -> bit 4.
    CHECK(PsmLayout::PixelByteAddress(Psm::CT32, 1, 0, 0, 1) == 4);
    CHECK(PsmLayout::PixelByteAddress(Psm::CT16, 8, 0, 0, 1) == 2);
    CHECK(PsmLayout::PixelByteAddress(Psm::T4, 0, 2, 0, 1) == 32 && PsmLayout::PixelBitInByte(Psm::T4, 0, 2, 0, 1) == 4);
    CHECK(PsmLayout::PixelBitInByte(Psm::T4, 8, 0, 0, 1) == 0 && PsmLayout::PixelByteAddress(Psm::T4, 8, 0, 0, 1) == 1);
}

// 4 MB rule: the address wraps to 0 past the top of local memory, for every class.
static void TestWrap() {
    for (int i = 0; i < kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        const uint32_t top = PsmLayout::kBlockCount;  // one block past the end == block 0
        CHECK(PsmLayout::PixelByteAddress(d.psm, 0, 0, top, 1) == PsmLayout::PixelByteAddress(d.psm, 0, 0, 0, 1));
        CHECK(PsmLayout::PixelByteAddress(d.psm, 0, 0, top + 1, 1) == PsmLayout::PixelByteAddress(d.psm, 0, 0, 1, 1));
        CHECK(PsmLayout::PixelByteAddress(d.psm, 0, 0, top - 24, 1) == (d.is_z ? 0u : (PsmLayout::kVramBytes - 24u * 256u)));
        // Last page + one page row of pixels wraps to page 0.
        const uint32_t last_page_bp = (PsmLayout::kPageCount - 1) * PsmLayout::kBlocksPerPage;
        CHECK(PsmLayout::PixelByteAddress(d.psm, 0, d.page_h, last_page_bp, 1) ==
              PsmLayout::PixelByteAddress(d.psm, 0, 0, 0, 1));
        CHECK(PsmLayout::PixelByteAddress(d.psm, d.page_w - 1, d.page_h - 1, last_page_bp, 1) < PsmLayout::kVramBytes);
    }
}

// RowAddresser::ByteAddress(x) is PixelByteAddress(psm, x, y, bp, bw) for every
// mode, over two page rows and two page columns, at bp 0 / mid / the wrap, bw 1 and 2.
static void TestRowAddresser() {
    const PsmDescriptor* t = PsmLayout::Table();
    const uint32_t last_page_bp = (PsmLayout::kPageCount - 1) * PsmLayout::kBlocksPerPage;
    const uint32_t bps[3] = {0, 1234, last_page_bp};
    uint64_t compared = 0, equal = 0;
    for (int i = 0; i < kPsmCount; i++) {
        const PsmDescriptor& d = t[i];
        for (uint32_t bw = 1; bw <= 2; ++bw) {
            for (uint32_t bp : bps) {
                for (uint32_t y = 0; y < 2u * d.page_h; ++y) {
                    PsmLayout::RowAddresser row;
                    row.Begin(d.psm, y, bp, bw);
                    for (uint32_t x = 0; x < 2u * d.page_w; ++x) {
                        ++compared;
                        if (row.ByteAddress(x) == PsmLayout::PixelByteAddress(d.psm, x, y, bp, bw)) ++equal;
                    }
                }
            }
        }
    }
    CHECK(compared > 0 && equal == compared);
    std::printf("row addresser: %llu addresses compared, %llu equal\n", (unsigned long long)compared,
                (unsigned long long)equal);
}

int main() {
    TestDescriptors();
    TestBijection();
    TestArithmetic();
    TestUnits();
    TestWrap();
    TestRowAddresser();
    if (g_fail) { std::printf("test_psm_layout: FAIL (%d)\n", g_fail); return 1; }
    std::printf("test_psm_layout: PASS\n");
    return 0;
}
