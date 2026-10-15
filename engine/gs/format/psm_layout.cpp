// gs/format/psm_layout.cpp -- the GS manual's page / block / column tables and the
// six-step address calculation (GS_SKILL chapter 8). Every table below is written
// from the manual figure cited above it; the frozen original's tables are only the
// differential oracle's comparand (tests/oracle/og_gs_format_oracle.cpp).

#include "engine/gs/format/psm_layout.h"

namespace ps2r {
namespace gs {

namespace {

// Section 8.1 table: page / block / column rectangles per geometry class.
//                     psm         name     class            bits idx sh  page     block  column  z      idx    ct32w
constexpr PsmDescriptor kTable[kPsmCount] = {
    { Psm::CT32,  "PSMCT32",  PsmClass::Bits32, 32, 0, 0,  64,  32,  8,  8,  8, 2, false, false, false },
    { Psm::CT24,  "PSMCT24",  PsmClass::Bits32, 32, 0, 0,  64,  32,  8,  8,  8, 2, false, false, false },
    { Psm::CT16,  "PSMCT16",  PsmClass::Bits16, 16, 0, 0,  64,  64, 16,  8, 16, 2, false, false, false },
    { Psm::CT16S, "PSMCT16S", PsmClass::Bits16, 16, 0, 0,  64,  64, 16,  8, 16, 2, false, false, false },
    { Psm::T8,    "PSMT8",    PsmClass::Bits8,   8, 8, 0, 128,  64, 16, 16, 16, 4, false, true,  false },
    { Psm::T4,    "PSMT4",    PsmClass::Bits4,   4, 4, 0, 128, 128, 32, 16, 32, 4, false, true,  false },
    { Psm::T8H,   "PSMT8H",   PsmClass::Bits32, 32, 8, 24, 64,  32,  8,  8,  8, 2, false, true,  true  },
    { Psm::T4HL,  "PSMT4HL",  PsmClass::Bits32, 32, 4, 24, 64,  32,  8,  8,  8, 2, false, true,  true  },
    { Psm::T4HH,  "PSMT4HH",  PsmClass::Bits32, 32, 4, 28, 64,  32,  8,  8,  8, 2, false, true,  true  },
    { Psm::Z32,   "PSMZ32",   PsmClass::Bits32, 32, 0, 0,  64,  32,  8,  8,  8, 2, true,  false, false },
    { Psm::Z24,   "PSMZ24",   PsmClass::Bits32, 32, 0, 0,  64,  32,  8,  8,  8, 2, true,  false, false },
    { Psm::Z16,   "PSMZ16",   PsmClass::Bits16, 16, 0, 0,  64,  64, 16,  8, 16, 2, true,  false, false },
    { Psm::Z16S,  "PSMZ16S",  PsmClass::Bits16, 16, 0, 0,  64,  64, 16,  8, 16, 2, true,  false, false },
};

// ---------------------------------------------------------------------------
// Block tables: block number at (block_y, block_x) inside a page (section 8.3).
// ---------------------------------------------------------------------------

// Section 8.3.1, PSMCT32 / PSMCT24 (and T8H/T4HL/T4HH, which share CT32's
// structure); also section 8.3.3, PSMT8 -- the same 4x8 figure.
constexpr uint8_t kBlock32[4][8] = {
    {  0,  1,  4,  5, 16, 17, 20, 21 },
    {  2,  3,  6,  7, 18, 19, 22, 23 },
    {  8,  9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};

// Section 8.3.1, PSMZ32 / PSMZ24.
constexpr uint8_t kBlockZ32[4][8] = {
    { 24, 25, 28, 29,  8,  9, 12, 13 },
    { 26, 27, 30, 31, 10, 11, 14, 15 },
    { 16, 17, 20, 21,  0,  1,  4,  5 },
    { 18, 19, 22, 23,  2,  3,  6,  7 },
};

// Section 8.3.2, PSMCT16; also section 8.3.4, PSMT4 -- the same 8x4 figure.
constexpr uint8_t kBlock16[8][4] = {
    {  0,  2,  8, 10 },
    {  1,  3,  9, 11 },
    {  4,  6, 12, 14 },
    {  5,  7, 13, 15 },
    { 16, 18, 24, 26 },
    { 17, 19, 25, 27 },
    { 20, 22, 28, 30 },
    { 21, 23, 29, 31 },
};

// Section 8.3.2, PSMZ16.
constexpr uint8_t kBlockZ16[8][4] = {
    { 24, 26, 16, 18 },
    { 25, 27, 17, 19 },
    { 28, 30, 20, 22 },
    { 29, 31, 21, 23 },
    {  8, 10,  0,  2 },
    {  9, 11,  1,  3 },
    { 12, 14,  4,  6 },
    { 13, 15,  5,  7 },
};

// Section 8.3.2, PSMCT16S.
constexpr uint8_t kBlock16S[8][4] = {
    {  0,  2, 16, 18 },
    {  1,  3, 17, 19 },
    {  8, 10, 24, 26 },
    {  9, 11, 25, 27 },
    {  4,  6, 20, 22 },
    {  5,  7, 21, 23 },
    { 12, 14, 28, 30 },
    { 13, 15, 29, 31 },
};

// Section 8.3.2, PSMZ16S.
constexpr uint8_t kBlockZ16S[8][4] = {
    { 24, 26,  8, 10 },
    { 25, 27,  9, 11 },
    { 16, 18,  0,  2 },
    { 17, 19,  1,  3 },
    { 28, 30, 12, 14 },
    { 29, 31, 13, 15 },
    { 20, 22,  4,  6 },
    { 21, 23,  5,  7 },
};

// ---------------------------------------------------------------------------
// Column arrangement (section 8.3): 32-bit word number inside a 16-word column.
// ---------------------------------------------------------------------------

// Section 8.3.1 "Pixel Arrangement Order in a Column": the 8x2 word order that
// every class reuses -- 16-bit modes put two pixels per word (section 8.3.2),
// 8-bit and 4-bit modes four / eight per word with rows 2-3 of their 4-row
// columns rotating the pattern by half (sections 8.3.3 / 8.3.4).
constexpr uint8_t kColumnWord[2][8] = {
    { 0, 1, 4, 5,  8,  9, 12, 13 },
    { 2, 3, 6, 7, 10, 11, 14, 15 },
};

// Sections 8.3.3 / 8.3.4: in the 4-row columns of PSMT8 and PSMT4 the word
// pattern of an 8-texel group is the section-8.3.1 order for rows 0-1 and the
// same order rotated by four words for rows 2-3 (8,9,12,13,0,1,4,5 / ...);
// columns 1 and 3 swap which row pair is rotated. Rows 0-1 occupy the even
// storage units of the word (bytes 0/2 for T8: bits 0-7 and 16-23; nibbles
// 0/2/4/6 for T4) and rows 2-3 the odd units (bytes 1/3; nibbles 1/3/5/7).
inline uint32_t FourRowColumnWord(uint32_t column, uint32_t cy, uint32_t cx_in_group) {
    const bool rotate = ((cy >= 2) != ((column & 1u) != 0));
    return kColumnWord[cy & 1u][(cx_in_group + (rotate ? 4u : 0u)) & 7u];
}

const PsmDescriptor& Desc(Psm psm) { return PsmLayout::Describe(psm); }

}  // namespace

const PsmDescriptor* PsmLayout::Table() { return kTable; }

namespace {

// The 6-bit PSM field -> its descriptor (nullptr for the 51 undefined codes):
// Describe / FromField are on the per-pixel address path of every transfer and
// refresh, so the table lookup is one indexed load, never a scan.
struct PsmIndex {
    const PsmDescriptor* by_field[64];
    constexpr PsmIndex() : by_field{} {
        for (const PsmDescriptor& d : kTable) by_field[static_cast<uint32_t>(d.psm) & 0x3Fu] = &d;
    }
};
constexpr PsmIndex kIndex{};

}  // namespace

const PsmDescriptor& PsmLayout::Describe(Psm psm) {
    const PsmDescriptor* d = kIndex.by_field[static_cast<uint32_t>(psm) & 0x3Fu];
    return d ? *d : kTable[0];  // CT32 for an invalid Psm (the GS reset default)
}

bool PsmLayout::FromField(uint32_t field, Psm* out) {
    const PsmDescriptor* d = kIndex.by_field[field & 0x3Fu];
    if (!d) return false;
    *out = d->psm;
    return true;
}

uint32_t PsmLayout::ElementsPerPage(Psm psm)   { return kPageBytes   * 8u / Desc(psm).storage_bits; }
uint32_t PsmLayout::ElementsPerBlock(Psm psm)  { return kBlockBytes  * 8u / Desc(psm).storage_bits; }
uint32_t PsmLayout::ElementsPerColumn(Psm psm) { return kColumnBytes * 8u / Desc(psm).storage_bits; }

uint32_t PsmLayout::PagesPerRow(Psm psm, uint32_t bw) {
    const uint32_t pages = (bw * 64u) / Desc(psm).page_w;
    return pages == 0 ? 1u : pages;
}

uint32_t PsmLayout::BlockInPage(Psm psm, uint32_t block_x, uint32_t block_y) {
    switch (psm) {
        case Psm::CT32: case Psm::CT24: case Psm::T8H: case Psm::T4HL: case Psm::T4HH:
        case Psm::T8:
            return kBlock32[block_y & 3u][block_x & 7u];
        case Psm::Z32: case Psm::Z24:
            return kBlockZ32[block_y & 3u][block_x & 7u];
        case Psm::CT16: case Psm::T4:
            return kBlock16[block_y & 7u][block_x & 3u];
        case Psm::CT16S:
            return kBlock16S[block_y & 7u][block_x & 3u];
        case Psm::Z16:
            return kBlockZ16[block_y & 7u][block_x & 3u];
        case Psm::Z16S:
            return kBlockZ16S[block_y & 7u][block_x & 3u];
    }
    return 0;
}

uint32_t PsmLayout::ElementInBlock(Psm psm, uint32_t bx, uint32_t by) {
    const PsmDescriptor& d = Desc(psm);
    const uint32_t column = by / d.column_h;
    const uint32_t cy = by % d.column_h;
    switch (d.cls) {
        case PsmClass::Bits32:
            // Section 8.3.1: 8x2 column, one word per pixel.
            return column * 16u + kColumnWord[cy][bx & 7u];
        case PsmClass::Bits16: {
            // Section 8.3.2: 16x2 column; texels 0-7 of a row are the low halves
            // (bits 0-15) of words 0,1,4,5,..., texels 8-15 the high halves.
            const uint32_t half = bx >> 3;
            return column * 32u + kColumnWord[cy][bx & 7u] * 2u + half;
        }
        case PsmClass::Bits8: {
            // Section 8.3.3: 16x4 column; byte = (odd rows pair ? 1 : 0) + 2 * group.
            const uint32_t group = bx >> 3;
            const uint32_t word = FourRowColumnWord(column, cy, bx & 7u);
            const uint32_t byte = (cy >= 2 ? 1u : 0u) + 2u * group;
            return column * 64u + word * 4u + byte;
        }
        case PsmClass::Bits4: {
            // Section 8.3.4: 32x4 column; nibble = (odd rows pair ? 1 : 0) + 2 * group.
            const uint32_t group = bx >> 3;
            const uint32_t word = FourRowColumnWord(column, cy, bx & 7u);
            const uint32_t nibble = (cy >= 2 ? 1u : 0u) + 2u * group;
            return column * 128u + word * 8u + nibble;
        }
    }
    return 0;
}

uint32_t PsmLayout::ElementInPage(Psm psm, uint32_t lx, uint32_t ly) {
    const PsmDescriptor& d = Desc(psm);
    const uint32_t block = BlockInPage(psm, lx / d.block_w, ly / d.block_h);
    return block * ElementsPerBlock(psm) + ElementInBlock(psm, lx % d.block_w, ly % d.block_h);
}

uint32_t PsmLayout::PixelAddress(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw) {
    // Section 8.4 steps 1-6, in element units, then the 4 MB wrap.
    const PsmDescriptor& d = Desc(psm);
    const uint64_t page = static_cast<uint64_t>(y / d.page_h) * PagesPerRow(psm, bw) + (x / d.page_w);
    const uint64_t per_block = ElementsPerBlock(psm);
    const uint64_t per_page  = ElementsPerPage(psm);
    const uint64_t element = static_cast<uint64_t>(bp) * per_block + page * per_page +
                             ElementInPage(psm, x % d.page_w, y % d.page_h);
    const uint64_t vram_elements = static_cast<uint64_t>(kVramBytes) * 8u / d.storage_bits;
    return static_cast<uint32_t>(element % vram_elements);
}

void PsmLayout::RowAddresser::Begin(Psm m, uint32_t row, uint32_t base, uint32_t width) {
    const PsmDescriptor& d = Desc(m);
    psm = m;
    y = row;
    bp = base;
    bw = width;
    page_w = d.page_w;
    block_w = d.block_w;
    const uint32_t ly = row % d.page_h;
    block_y = ly / d.block_h;
    by = ly % d.block_h;
    per_block = ElementsPerBlock(m);
    per_page = ElementsPerPage(m);
    const uint64_t page_row = static_cast<uint64_t>(row / d.page_h) * PagesPerRow(m, width);
    row_base = static_cast<uint64_t>(base) * per_block + page_row * per_page;
    vram_elements = static_cast<uint64_t>(kVramBytes) * 8u / d.storage_bits;
    storage_bits = d.storage_bits;
    auto log2u = [](uint64_t v) { uint32_t n = 0; while (v > 1) { v >>= 1; ++n; } return n; };
    page_w_shift = log2u(page_w);
    block_w_shift = log2u(block_w);
    per_page_shift = log2u(per_page);
    per_block_shift = log2u(per_block);
    vram_mask = vram_elements - 1;
    byte_shift = storage_bits == 32 ? 2 : storage_bits == 16 ? 1 : storage_bits == 8 ? 0 : -1;
}

uint32_t PsmLayout::RowAddresser::ByteAddress(uint32_t x) const {
    // The same terms PixelAddress sums, with the row's constants hoisted:
    // page = page_row * pages_per_row + x / page_w; element in page = block *
    // per_block + element in block. Every divisor is a power of two (Begin), so
    // the quotients are shifts, the remainders masks and the 4 MB wrap an AND;
    // Coretest_psm_layout walks every mode against PixelByteAddress.
    const uint32_t lx = x & (page_w - 1u);
    const uint64_t page_x = x >> page_w_shift;
    const uint64_t block = BlockInPage(psm, lx >> block_w_shift, block_y);
    const uint64_t element = row_base + (page_x << per_page_shift) + (block << per_block_shift) +
                             ElementInBlock(psm, lx & (block_w - 1u), by);
    const uint64_t wrapped = element & vram_mask;
    return static_cast<uint32_t>(byte_shift >= 0 ? (wrapped << byte_shift) : (wrapped >> 1));
}

uint32_t PsmLayout::PixelByteAddress(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw) {
    const uint64_t element = PixelAddress(psm, x, y, bp, bw);
    return static_cast<uint32_t>(element * Desc(psm).storage_bits / 8u);
}

uint32_t PsmLayout::PixelBitInByte(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw) {
    if (Desc(psm).cls != PsmClass::Bits4) return 0;
    return (PixelAddress(psm, x, y, bp, bw) & 1u) * 4u;
}

}  // namespace gs
}  // namespace ps2r
