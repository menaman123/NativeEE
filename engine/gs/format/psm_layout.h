// gs/format/psm_layout.h -- THE GS local-memory address math (GS_SKILL chapter 8).
//
// Every other format-layer class (rect swizzle, CLUT, block decode, mip chain)
// and every Phase-6 consumer derives its addressing from this one class; no other
// page/block/column table exists in the tree.
//
// Units (stated once, used everywhere):
//   bp  -- buffer base in 64-word (256-byte) BLOCK units, the TBP0/DBP/SBP/CBP
//          register convention (section 8.1). FBP/ZBP are in 2048-word page units
//          and convert as fbp * kBlocksPerPage. A base on a block boundary inside
//          a page (section 8.5) is plain addition: block i of the buffer is block
//          bp + i of memory, and the figure's labels are (bp + i) mod 32 -- the
//          "wrap" of section 8.5 is that labelling, the data continues into the
//          next page.
//   bw  -- buffer width in 64-pixel units, the TBW/FBW/DBW register convention.
//          pages_per_row = max(1, bw*64 / page_w): a width narrower than one page
//          still addresses one page per row (section 8.2).
//   x,y -- buffer-relative pixel coordinates (section 8.2).
//   element -- the storage unit of the mode: 32-bit word (Bits32 class, incl. the
//          T8H/T4HL/T4HH index-in-word modes), 16-bit halfword, byte (T8) or
//          nibble (T4). PixelAddress returns an element index; PixelByteAddress
//          is element * storage_bits / 8 (T4: the byte holding the nibble).
//   4 MB rule -- GS local memory is 4 MB and its address arithmetic wraps: the
//          element address is reduced modulo (4 MB expressed in elements), so a
//          buffer that runs past the top of memory continues at address 0. There
//          is no clamp and no error path.
#pragma once

#include <cstdint>

#include "engine/gs/format/psm.h"

namespace ps2r {
namespace gs {

class PsmLayout {
public:
    static constexpr uint32_t kVramBytes      = 4u * 1024u * 1024u;
    static constexpr uint32_t kPageBytes      = 8192;
    static constexpr uint32_t kBlockBytes     = 256;
    static constexpr uint32_t kColumnBytes    = 64;
    static constexpr uint32_t kBlocksPerPage  = 32;
    static constexpr uint32_t kColumnsPerBlock = 4;
    static constexpr uint32_t kPageCount      = kVramBytes / kPageBytes;   // 512
    static constexpr uint32_t kBlockCount     = kVramBytes / kBlockBytes;  // 16384

    // Geometry for a mode (section 8.1 table).
    static const PsmDescriptor& Describe(Psm psm);

    // The 13 modes in table order (for enumeration in tests and tables).
    static const PsmDescriptor* Table();  // kPsmCount entries

    // 6-bit register field -> mode. Returns false for the 51 undefined encodings.
    static bool FromField(uint32_t field, Psm* out);

    // Elements per page / block / column for the mode (page = 8 KB etc.).
    static uint32_t ElementsPerPage(Psm psm);
    static uint32_t ElementsPerBlock(Psm psm);
    static uint32_t ElementsPerColumn(Psm psm);

    // Section 8.2: pages per buffer row for a width in 64-pixel units (>= 1).
    static uint32_t PagesPerRow(Psm psm, uint32_t bw);

    // Section 8.3 block table: block number (0..31) of the block at grid position
    // (block_x, block_y) inside a page.
    static uint32_t BlockInPage(Psm psm, uint32_t block_x, uint32_t block_y);

    // Section 8.3 column arrangement: element offset (0 .. ElementsPerBlock-1)
    // of the pixel at (bx, by) inside a block; column = by / column_h.
    static uint32_t ElementInBlock(Psm psm, uint32_t bx, uint32_t by);

    // Element offset inside a page of the pixel at page-relative (lx, ly):
    // BlockInPage * ElementsPerBlock + ElementInBlock.
    static uint32_t ElementInPage(Psm psm, uint32_t lx, uint32_t ly);

    // Section 8.4 steps 1-6 for the buffer (bp, bw): absolute element address,
    // wrapped to the 4 MB (see the unit notes above).
    static uint32_t PixelAddress(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw);

    // Byte address of the element holding pixel (x, y) (wrapped to the 4 MB).
    static uint32_t PixelByteAddress(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw);

    // Bit offset of the pixel inside its byte (0 or 4 for T4; 0 otherwise).
    static uint32_t PixelBitInByte(Psm psm, uint32_t x, uint32_t y, uint32_t bp, uint32_t bw);

    // The per-row form of PixelByteAddress for a walk along x at one y: the
    // row's page row, block row and column terms are computed once (Begin),
    // each x pays the page column, the block-in-page lookup and the element in
    // the block. ByteAddress(x) == PixelByteAddress(psm, x, y, bp, bw) for every
    // x (the unit test walks every mode over whole pages).
    struct RowAddresser {
        void Begin(Psm psm, uint32_t y, uint32_t bp, uint32_t bw);
        uint32_t ByteAddress(uint32_t x) const;
        // The element address (== PixelAddress(psm, x, y, bp, bw)); for T4 its low
        // bit is the nibble inside the byte ByteAddress names.
        uint32_t ElementAddress(uint32_t x) const;

        Psm psm = Psm::CT32;
        uint32_t y = 0, bp = 0, bw = 0;   // the row this addresser was begun for
        uint32_t page_w = 1, block_w = 1;
        uint32_t block_y = 0, by = 0;
        uint64_t row_base = 0;            // bp * per_block + page_row * pages_per_row * per_page
        uint64_t per_page = 1, per_block = 1, vram_elements = 1;
        uint32_t storage_bits = 32;
        // The same geometry as shifts and masks (every GS mode's page_w, block_w,
        // elements-per-page/block and the 4 MB element count are powers of two;
        // Begin derives them, ByteAddress uses them): the per-pixel address on the
        // IMAGE upload path has no division.
        uint32_t page_w_shift = 0, block_w_shift = 0, per_page_shift = 0, per_block_shift = 0;
        uint64_t vram_mask = 0;           // vram_elements - 1
        int byte_shift = 2;               // element -> byte: 32-bit +2, 16-bit +1, 8-bit 0, 4-bit -1
    };
};

}  // namespace gs
}  // namespace ps2r
