// gs/format/clut_layout.h -- where CLUT entry i lives in GS local memory
// (GS_SKILL sections 2.7.1, 2.7.3, 2.7.4, 3.4.7) and the raw reads over a VRAM view.
//
// CSM1 (TEX0.CSM = 0): the CLUT is a small IMAGE stored at CBP in the CPSM
// pixel format with buffer width 1 (64 pixels), starting at the buffer's upper
// left (section 3.4.7): a 16x16 rectangle for 256 entries (IDTEX8), an 8x2
// rectangle for 16 entries (IDTEX4). Its texel (x, y) is therefore addressed by
// PsmLayout::PixelAddress(cpsm, x, y, cbp, 1). Within the 16x16 rectangle the
// entries are NOT row-major (section 2.7.3 figure): each group of 32 entries
// fills two grid rows with the rows of eight reordered 0, 2, 1, 3 --
//   row 0: entries 00-07 | 10-17      row 1: entries 08-0f | 18-1f
// so entry i sits at grid position Csm1Index(i): within its 32-group, entries
// 8-15 move to positions 16-23 and 16-23 to 8-15. Csm1Index is an involution.
// The 8x2 rectangle of a 16-entry CLUT is the first two rows of eight of the
// same figure (entry i at (i % 8, i / 8)) -- the same formula's first 16 values.
// This is the ONE CSM1 reorder in the tree (the original had three copies and a
// row-order variant, all equal; the oracle proves NEW against each).
//
// CSM2 (TEX0.CSM = 1): one row of PSMCT16 entries (section 2.7.3: width 256 or
// 16, height 1; CPSM must be CT16, section 2.7.4) at the TEXCLUT position
// (CBW, COU, COV) inside the CBP buffer. Scope cut kept from the original
// (A3 section 2 inconsistency 3): NEW reads CSM2 as a LINEAR halfword row at
// CBP (COU = COV = 0, no CT16 page layout) -- the original's behaviour, so the
// two trees agree; the hardware rule (row through the CT16 layout at the
// TEXCLUT offset) is recorded in engine/gs/format/README.md for Phase 6.
// A CT32 CLUT under CSM2 is undefined by the manual; it is read as the CSM1
// rectangle without the index reorder (again the original's behaviour).
//
// CSA (TEX0 bits 56-60) is an offset in the GS's 1 KB CLUT TEMPORARY buffer in
// 16-entry units (section 3.4.7): it selects where a loaded CLUT lands and where
// lookups read, letting several 16-entry palettes coexist. It is NOT part of the
// local-memory address, so the readers here take no CSA; Phase 6's CLUT cache
// models the buffer (CLD load control, TEXFLUSH) above this class.
//
// Reads return RAW PS2 values (CT32: RGBA8 word with 0x80 = opaque; CT16/CT16S:
// the 5:5:5:1 halfword). No alpha prefold (PS2_GS_CLUTRAW winning arm); 16 -> 32
// expansion and TEXA belong to TexaExpand.
#pragma once

#include <cstdint>

#include "engine/gs/format/psm.h"

namespace ps2r {
namespace gs {

class ClutLayout {
public:
    static constexpr uint32_t kEntries8 = 256;  // IDTEX8 (PSMT8 / PSMT8H)
    static constexpr uint32_t kEntries4 = 16;   // IDTEX4 (PSMT4 / PSMT4HL / PSMT4HH)

    struct Position { uint32_t x, y; };

    // Section 2.7.3: grid position (row-major, 16 wide) of entry i in the CSM1
    // 16x16 rectangle. Involution on 0..255.
    static uint32_t Csm1Index(uint32_t entry);

    // CSM2: sequential.
    static uint32_t Csm2Index(uint32_t entry);

    // CSM1 rectangle position of `entry` for a CLUT of `entries` (16 or 256):
    // 16x16 grid via Csm1Index, 8x2 grid (i % 8, i / 8) for 16 entries.
    static Position Csm1Position(uint32_t entry, uint32_t entries);

    // Element address (cpsm storage units: word for CT32, halfword for
    // CT16/CT16S) of `entry` in the CLUT at cbp (blocks). csm2 selects CSM2.
    static uint32_t EntryAddress(Psm cpsm, uint32_t cbp, bool csm2, uint32_t entries, uint32_t entry);

    // Raw entry reads over a 4 MB VRAM view (see the header comment).
    static uint32_t ReadClut32(const uint8_t* vram, uint32_t cbp, bool csm2, uint32_t entries, uint32_t entry);
    static uint16_t ReadClut16(const uint8_t* vram, Psm cpsm, uint32_t cbp, bool csm2, uint32_t entries, uint32_t entry);

    // Whole palette: `out[entry]` = raw value (32-bit word or zero-extended
    // 16-bit halfword) for every entry of the CLUT at cbp.
    static void ReadClut(const uint8_t* vram, Psm cpsm, uint32_t cbp, bool csm2, uint32_t entries, uint32_t* out);
};

}  // namespace gs
}  // namespace ps2r
