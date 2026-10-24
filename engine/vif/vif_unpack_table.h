#pragma once
// The table-driven UNPACK lane for VIF1: one whole-command kernel per
// (format, masked, mode) cell, selected from a constant table, for every
// skipping-write shape (CL >= WL). It follows the two bulk lanes in the
// dispatch (they keep the plainest shapes) and takes what they decline: the
// S / V2 / V3-16 / V3-8 / V4-5 formats, every masked command and every MODE
// 1-3 command, with WL > 1 as well as WL == 1.
//
// PS2 rules kept (EE User's Manual 6.3.5, exactly as the scalar reference
// VifUnpack::Execute states them): the k-th written vector lands at
// addr + (k / WL) * CL + (k % WL) (wrapping at 1024 quadwords); the write
// cycle k % WL selects the MASK row and the COL entry (both & 3); S replicates
// its element, V2 writes {x, y, x, y} (W = 0 for V2-32 on an odd payload
// phase), V3 takes W from the following source element without consuming it
// (V3-32: W = 0 when the vector's parity differs from the payload phase), V4-5
// expands the 5:5:5:1 halfword; USN zero-extends 8/16-bit elements, otherwise
// they sign-extend; MODE 1 adds ROW, 2 adds COL[cycle], 3 both; a masked
// field is written / kept / replaced by ROW / replaced by COL per the 2-bit
// MASK action. The lane declines (false) when the payload is shorter than the
// command consumes -- the scalar loop keeps the partial-read semantics -- and
// for CL < WL (the filling write) and undefined formats. NEON bodies under
// PS2R_SIMD, the scalar twin otherwise; VifUnpackScalar proves both
// byte-identical to the reference loop over every cell.
#include <cstddef>
#include <cstdint>

#include "engine/vif/vif_regs.h"

class VifUnpackTable {
public:
    // One command: the decoded VIFcode, the unit's registers, the destination
    // (already TOPS-adjusted) and the VU1 data memory it writes.
    struct Command {
        uint32_t format = 0;          // the VIFcode's low nibble
        bool usn = false;
        bool use_mask = false;        // the VIFcode's m bit
        uint32_t addr_qw = 0;         // first destination quadword (& 0x3FF)
        uint32_t n = 0;               // vectors to write (NUM, 0 -> 256 already applied)
        uint32_t cl = 1;
        uint32_t wl = 1;
        const VifRegs* regs = nullptr;  // mode, mask, row, col
        const uint8_t* payload = nullptr;
        size_t payload_size = 0;
        uint8_t phase = 0;            // the payload's quadword phase (start word & 1)
        uint8_t* dmem = nullptr;      // the 16 KB VU1 data memory
    };

    // Run the command's cell; false = not eligible (nothing written).
    static bool Run(const Command& c);

    // Source bytes the command consumes (without the V3 lookahead); 0 for an undefined format.
    static size_t SourceBytes(uint32_t format, uint32_t n);
};
