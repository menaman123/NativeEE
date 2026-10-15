// gs/format/texa_expand.h -- the GS texel value expansions to 8 bits per channel:
// RGBA16 -> RGBA32 (5-bit channels widened, 1-bit alpha resolved) and RGB24 ->
// RGBA32 (alpha supplied), per GS_SKILL section 3.4.6 Format Conversion and the
// TEXA register (section 3.4.6 / register 0x3b: TA0 bits 7:0, AEM bit 15, TA1
// bits 39:32).
//
// The GS computes at 8 bits per channel. Two rules resolve the missing bits:
//
// 1. RGB widening 5 -> 8. The manual's formula is the shift, out = in5 << 3
//    (zero fill, 31 -> 248). This tree widens by BIT REPLICATION instead,
//    out = (in5 << 3) | (in5 >> 2) (31 -> 255), so that widening is the exact
//    inverse of the 5-bit truncation (Truncate8To5 = >> 3) used when a 16-bit
//    frame is stored: Replicate5To8(Truncate8To5(x)) == x for every
//    representable x, which zero fill does not give. That adjudication is the
//    WALLNOISE record (docs/bugs/BUG-020-wallnoise-ct16-dither.md: CT16/CT16S
//    accumulator decode -> re-render round trips are stable only when the
//    expansion inverts the truncation) and docs/audit/A3_GS_RENDER.md
//    section 2 ("replicate is the settled convention"). Both trees replicate;
//    the departure from the manual's shift is recorded as EQUIVALENCE_PLAN 7.1
//    P2-EXP16-REPL for the Phase-6 frame lane. Zero fill is not written here.
//
// 2. Alpha. RGBA16 carries one alpha bit, RGB24 none; TEXA supplies the byte
//    (section 3.4.6 table):
//        A bit = 0 : AEM = 0 -> TA0 ; AEM = 1 -> 0 if R = G = B = 0, else TA0
//        A bit = 1 : TA1 (AEM does not apply)
//        RGB24     : AEM = 0 -> TA0 ; AEM = 1 -> 0 if R = G = B = 0, else TA0
//    ApplyTexa16 / ApplyTexa24 are that table. Alpha values are in the PS2
//    domain (0x80 = 1.0, see PixelConvert::ScaleAlphaPs2ToGpu).
//
// Expand16To32 is the TEXA-less widening of an RGBA16 value: replicated RGB and
// the A bit as a full byte (0xFF / 0x00). It is the value a 16-bit FRAME pixel
// takes when read back as a colour (the original's render-target readback and
// the FBMSK 16-bit merge use exactly this form), and the CT16 A-bit storage
// convention (PS2_GS_ABIT16 winning arm: an 8-bit alpha >= 0x80 stores the bit)
// is its inverse, Truncate32To16. It is NOT the texture-sampling rule: the
// original's PSMCT16 texture decode used Expand16to32 and never applied TEXA
// (A3 section 2 inconsistency 1); this tree's texture path applies ApplyTexa16,
// the manual's rule -- EQUIVALENCE_PLAN 7.1 P2-CT16-TEXA, Phase 6 arbitrates.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

class TexaExpand {
public:
    // The TEXA register fields (section 3.4.6). Alpha values are PS2-domain bytes.
    struct Texa {
        uint8_t ta0 = 0;    // alpha for A bit = 0 and for RGB24
        uint8_t ta1 = 0;    // alpha for A bit = 1
        bool    aem = false;  // 1: R = G = B = 0 with A bit = 0 (or RGB24) -> alpha 0
    };

    // TEXA register word -> fields: TA0 bits 7:0, AEM bit 15, TA1 bits 39:32.
    static Texa DecodeTexaRegister(uint64_t texa);

    // 5 -> 8 by bit replication and its inverse (the CT16 quantize convention).
    static uint8_t Replicate5To8(uint8_t v5);
    static uint8_t Truncate8To5(uint8_t v8);

    // RGBA16 (5:5:5:1, R in bits 4:0, A in bit 15) -> RGBA32 word (R in the low
    // byte, A in the high byte): replicated RGB, alpha = A bit ? 0xFF : 0x00.
    static uint32_t Expand16To32(uint16_t rgba16);
    // Inverse: 5-bit truncation per channel, A bit = (alpha >= 0x80).
    static uint16_t Truncate32To16(uint32_t rgba32);

    // The section 3.4.6 alpha table, one cell: `rgb_zero` is R = G = B = 0 of the
    // texel (its 5-bit or 8-bit channels -- equivalent), `a_bit` its alpha bit.
    static uint8_t Alpha16(bool a_bit, bool rgb_zero, const Texa& texa);
    static uint8_t Alpha24(bool rgb_zero, const Texa& texa);

    // RGBA16 texel -> RGBA32 word with replicated RGB and the TEXA alpha.
    static uint32_t ApplyTexa16(uint16_t rgba16, const Texa& texa);
    // RGB24 texel (bits 23:0 of the word; bits 31:24 ignored) -> RGBA32 word.
    static uint32_t ApplyTexa24(uint32_t rgb24, const Texa& texa);
};

}  // namespace gs
}  // namespace ps2r
