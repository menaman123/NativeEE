// gs/format/pixel_convert.h -- the value side of the GS pixel storage modes:
// how a storage element of each PSM packs its pixel (GS_SKILL section 2.3.1 colour
// formats, 2.3.2 Z formats, 2.6.3 index-in-word formats), the alpha domain (PS2
// 0x80 = 1.0 versus the GPU's 0xFF), and the per-mode element widths, all read
// from the one PsmLayout descriptor table. Pure functions; no address math here
// (PsmLayout / Swizzle) and no TEXA (TexaExpand).
//
// Word layout (section 2.3.1): R in bits 7:0, G 15:8, B 23:16, A 31:24. Colour
// helpers move between an RGBA32 word in that layout and the storage element:
//   PSMCT32   element = the word                              (alpha 0x80 = 1.0)
//   PSMCT24   element bits 23:0 = RGB; bits 31:24 are NOT the pixel (the byte is
//             left to whoever shares the word -- section 2.6.3 T8H/T4HL/T4HH --
//             and Swizzle writes CT24 through the 0x00FFFFFF element mask).
//             UnpackColor returns alpha 0; TEXA supplies it (TexaExpand::ApplyTexa24).
//             The IMAGE stream (section 3.9 host->local) packs RGB24 pixels at 24
//             bits each: StreamBitsPerPixel(CT24) = 24 while the stored element is
//             32 bits. The original unpacked the CT24 stream at 32 bits per pixel
//             and stored 0x80 in the unused byte (EQUIVALENCE_PLAN 7.1 P2-CT24-HWREG-A);
//             neither is written here.
//   PSMCT16 / PSMCT16S  element = 5:5:5:1 halfword (R 4:0, G 9:5, B 14:10, A 15).
//             UnpackColor = TexaExpand::Expand16To32 (replicated RGB, A bit as
//             0xFF/0x00); PackColor = TexaExpand::Truncate32To16 (the 5-bit
//             truncation convention, PS2_GS_CT16_QUANT winning arm; A bit =
//             alpha >= 0x80, the PS2_GS_ABIT16 winning arm). Dither is Phase 6.
//
// Z (section 2.3.2) is a distinct type here: Depth. PSMZ32 keeps 32 bits, PSMZ24
// bits 23:0 (31:24 not the pixel, same sharing rule as CT24), PSMZ16/Z16S 16 bits.
// A Z element is never a colour and the colour helpers reject Z modes (and vice
// versa) by contract -- the original's "PSMCT helpers on Z bytes silently corrupt
// co-paged data" hazard is closed by the API, not by a comment. The original's
// colour-authored Z routing (PS2_GS_ZSRC winning arm, s9_shadow.h ZsrcRoute) is a
// serve-side predicate for Phase 6; the value it needs is UnpackZ of the CT32
// word's bytes read as Z32, which is what DepthFromColorWord expresses.
//
// Indexed modes (sections 2.3.1, 2.6.3): the index sits at the descriptor's
// index_shift inside the storage element (T8: byte; T4: nibble; T8H: bits 31:24
// of a CT32 word; T4HL: 27:24; T4HH: 31:28). PackIndex is a read-modify-write of
// the element so the co-paged colour bits survive.
//
// Alpha domain: the GS alpha byte is 0..0x80 for 0.0..1.0 (0x81..0xFF are >1.0,
// section 2.3.1 / 3.6 alpha blending). ScaleAlphaPs2ToGpu maps it to the GPU's
// 0..0xFF exactly as the original did (a >= 0x80 -> 0xFF, else a * 2) and
// ScaleAlphaGpuToPs2 is the original's inverse (0xFF -> 0x80, else a / 2); the
// pair is the identity on 0..0x80.
#pragma once

#include <cstdint>

#include "engine/gs/format/psm.h"

namespace ps2r {
namespace gs {

class PixelConvert {
public:
    // A Z-buffer value (section 2.3.2). Distinct from a colour word by type.
    struct Depth { uint32_t value; };

    // ---- element widths (from the PsmLayout descriptor table) ----
    static uint32_t StorageBitsPerPixel(Psm psm);      // 32 / 16 / 8 / 4
    static uint32_t BytesPerPixel(Psm psm);            // storage bits / 8; modes of 8 bits or wider only
    static uint32_t StreamBitsPerPixel(Psm psm);       // IMAGE-stream width: CT24/Z24 = 24, else storage bits
    static bool IsColor(Psm psm);                      // CT32 CT24 CT16 CT16S
    static bool IsZ(Psm psm);                          // Z32 Z24 Z16 Z16S
    static bool IsIndexed(Psm psm);                    // T8 T4 T8H T4HL T4HH

    // ---- colour (RGBA32 word <-> storage element; IsColor(psm) required) ----
    static uint32_t UnpackColor(Psm psm, uint32_t element);
    static uint32_t PackColor(Psm psm, uint32_t rgba32);
    // The bits of the element that ARE the pixel (CT24: 0x00FFFFFF, CT16: 0xFFFF, ...).
    static uint32_t ElementPixelMask(Psm psm);

    // ---- Z (Depth <-> storage element; IsZ(psm) required) ----
    static Depth UnpackZ(Psm psm, uint32_t element);
    static uint32_t PackZ(Psm psm, Depth depth);
    // A CT32 word's 32 bits read as the Z32 value they alias (colour-authored Z).
    static Depth DepthFromColorWord(uint32_t rgba32);

    // ---- CLUT index (IsIndexed(psm) required) ----
    static uint32_t UnpackIndex(Psm psm, uint32_t element);
    static uint32_t PackIndex(Psm psm, uint32_t element, uint32_t index);

    // ---- alpha domain ----
    static uint8_t ScaleAlphaPs2ToGpu(uint8_t ps2_alpha);
    static uint8_t ScaleAlphaGpuToPs2(uint8_t gpu_alpha);

    // RGBA32 word field access (section 2.3.1 layout).
    static uint8_t  R(uint32_t rgba32) { return uint8_t(rgba32 & 0xFF); }
    static uint8_t  G(uint32_t rgba32) { return uint8_t((rgba32 >> 8) & 0xFF); }
    static uint8_t  B(uint32_t rgba32) { return uint8_t((rgba32 >> 16) & 0xFF); }
    static uint8_t  A(uint32_t rgba32) { return uint8_t((rgba32 >> 24) & 0xFF); }
    static uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }
};

}  // namespace gs
}  // namespace ps2r
