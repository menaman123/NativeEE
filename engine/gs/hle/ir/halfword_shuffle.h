// gs/hle/ir/halfword_shuffle.h -- HalfwordShuffle: the "texture shuffle" sprite column
// pass as a per-32-bit-pixel half-word copy.
//
// A 16-bit FRAME over a buffer the title draws as 32 bpp, textured from the SAME
// layout read as 16 bpp (its own buffer or a sibling of equal FBW), drawn as full-
// height sprite columns 8 pixels wide whose texture column is the destination column
// shifted by exactly 8: within a GS block the CT32 pixel (r, x) and the CT16 pixels
// (r, x) / (r, x + 8) are the same bytes (the low / high half-word; GS User's Manual
// 8.3, the CT32 and CT16 block and column layouts), and the 16-bit page / block
// tables permute whole 8-row bands across the 64 columns of a page, so a column
// pass covering the whole 16-bit extent of one parity touches every 32-bit pixel
// once: dest half := (texel-function(src other half) & ~FBMSK16) | (dest half &
// FBMSK16). The texel function is the CT16 -> RGBA expansion by TEXA (5-bit fields
// << 3, A from TA0 / TA1 / AEM) followed by DECAL (or MODULATE by 0x80: identity)
// and the CT16 frame write (>> 3 per field, A bit = alpha >= 0x80).
//
// Detect() is pure over the DrawIr: it accepts exactly that shape -- sprites, FST,
// equal FBW, DECAL or identity MODULATE with TCC = 1 (TCC = 0 would write the vertex
// alpha), no blend term (ABE off or A = B with D = Cs), an alpha test that passes or
// fails every pixel into the frame buffer, no DATE, no per-pixel Z test, no SCANMSK,
// FBA = 0 (FBA forces the written A bit), DTHE = 0 (dithering is not a copy), a
// scissor holding the extent, 8-wide 16-aligned columns of one parity covering FBW,
// every column spanning the same [0, h16) with h16 a multiple of 64, u = x -/+ 8 and
// v = y -- and reports the operation. The caller decides whether the source bytes are
// available (its own target's copy or a sibling target) and executes it as one
// full-extent pass over the 32-bit image.
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class HalfwordShuffle {
public:
    struct Op {
        bool dest_hi = false;      // the destination half-word: high (x % 16 >= 8) or low
        uint16_t mask16 = 0;       // FRAME.FBMSK folded onto the 5551 half-word (1 = keep dest)
        int cols = 0;              // 32-bit pixels per row covered (= FBW)
        int rows32 = 0;            // 32-bit rows covered (= the 16-bit height / 2)
        bool self = false;         // the texture base is the frame base (same FBW)
        uint8_t ta0 = 0, ta1 = 0;  // TEXA expansion of the source half-word's A bit
        bool aem = false;
    };

    static bool Detect(const DrawIr& ir, Op* op);

    // FRAME.FBMSK (RGBA8 bit layout) folded onto the CT16 5551 layout: a field is
    // kept when its high bits are; alpha keeps bit 15 from bit 31.
    static uint16_t Fbmsk16From32(uint32_t m);
    static bool Is16BitColor(uint8_t psm) { return psm == 0x02 || psm == 0x0A; }
};

}  // namespace gs
}  // namespace ps2r
