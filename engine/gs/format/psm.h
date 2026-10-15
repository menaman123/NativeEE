// gs/format/psm.h -- the GS pixel storage modes and their geometry descriptor.
//
// `Psm` holds the 6-bit PSM field values as the GS registers carry them
// (TEX0.PSM, FRAME.PSM, ZBUF.PSM, BITBLTBUF.SPSM/DPSM, TEX0.CPSM); the 13 values
// below are every mode the GS defines. `PsmDescriptor` is the per-mode geometry
// from the GS manual (GS_SKILL section 8.1 table): the page / block / column
// rectangle in pixels and the storage-element width. The tables themselves and
// the address arithmetic live in PsmLayout (psm_layout.h); this header is data
// only so that every format-layer class shares one vocabulary.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

enum class Psm : uint8_t {
    CT32  = 0x00,  // RGBA32, 8:8:8:8
    CT24  = 0x01,  // RGB24 in a 32-bit word (upper byte unused; TEXA supplies alpha)
    CT16  = 0x02,  // RGBA16, 5:5:5:1
    CT16S = 0x0A,  // RGBA16 with the "S" block arrangement (section 8.3.2)
    T8    = 0x13,  // 8-bit CLUT index, one texel per byte
    T4    = 0x14,  // 4-bit CLUT index, one texel per nibble
    T8H   = 0x1B,  // 8-bit index in bits 31-24 of a CT32 word
    T4HL  = 0x24,  // 4-bit index in bits 27-24 of a CT32 word
    T4HH  = 0x2C,  // 4-bit index in bits 31-28 of a CT32 word
    Z32   = 0x30,  // 32-bit Z
    Z24   = 0x31,  // 24-bit Z in a 32-bit word
    Z16   = 0x32,  // 16-bit Z
    Z16S  = 0x3A,  // 16-bit Z with the "S" block arrangement
};

constexpr int kPsmCount = 13;

// The four geometry classes of section 8.1: modes in the same class share page,
// block and column rectangles and (per section 8.3) the same physical layout of a
// given block/column number -- only the block ORDER within the page differs
// between the colour, Z and "S" members of a class.
enum class PsmClass : uint8_t {
    Bits32,  // CT32 CT24 Z32 Z24 T8H T4HL T4HH : page 64x32, block 8x8, column 8x2
    Bits16,  // CT16 CT16S Z16 Z16S              : page 64x64, block 16x8, column 16x2
    Bits8,   // T8                               : page 128x64, block 16x16, column 16x4
    Bits4,   // T4                               : page 128x128, block 32x16, column 32x4
};

struct PsmDescriptor {
    Psm psm;
    const char* name;
    PsmClass cls;
    uint8_t  storage_bits;   // bits one storage element occupies: 32 / 16 / 8 / 4
    uint8_t  index_bits;     // CLUT index width for indexed modes (8 / 4), else 0
    uint8_t  index_shift;    // bit position of the index inside the storage element
    uint16_t page_w, page_h;
    uint8_t  block_w, block_h;
    uint8_t  column_w, column_h;
    bool     is_z;           // Z-buffer mode (its own block order; never a colour alias)
    bool     is_indexed;     // T8 T4 T8H T4HL T4HH
    bool     shares_ct32_word;  // T8H/T4HL/T4HH: the element is a CT32 word holding the index
};

}  // namespace gs
}  // namespace ps2r
