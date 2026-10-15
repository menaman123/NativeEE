// gs/format/block_decode.h -- THE texture read: a rect of a GS texture buffer in
// any PSM -> a linear RGBA32 image, through PsmLayout (address), PixelConvert /
// TexaExpand (value) and ClutLayout (palette). GS_SKILL chapter 8 (addresses),
// section 3.4.6 (TEXA), sections 2.7.x (CLUT).
//
// Address side. The walk is page-column ordered: every row of the rect is cut
// into runs that stay inside one page, and inside a page the element offset of
// pixel (lx, ly) comes from a PAGE LUT -- page_w x page_h entries of
// PsmLayout::ElementInPage, GENERATED at first use for the mode (never a typed
// table; there is exactly one address table in the tree, PsmLayout's). A page
// holds 8 KB = 32 blocks of 256 B (section 8.1), so the per-mode LUT is 2 K
// (Bits32) .. 16 K (Bits4) entries of 16 bits. The generic path (DecodeGeneric)
// calls PixelAddress per pixel; the LUT path is byte-identical to it by
// construction (the LUT IS PixelAddress with the page term factored out) and
// tests/gs/format/test_block_decode.cpp pins it. Addresses wrap at 4 MB
// (the PsmLayout rule).
//
// Value side (what an output RGBA32 word means; alpha stays in the PS2 domain,
// 0x80 = 1.0 -- PixelConvert::ScaleAlphaPs2ToGpu is the one GPU scaling site):
//   PSMCT32          the stored word.
//   PSMCT24          RGB + TEXA alpha (TexaExpand::ApplyTexa24, section 3.4.6).
//   PSMCT16/CT16S    5:5:5:1 widened by replication + TEXA alpha
//                    (TexaExpand::ApplyTexa16 -- the manual's texture rule;
//                    EQUIVALENCE_PLAN 7.1 P2-CT16-TEXA records that the original
//                    never applied TEXA here).
//   PSMT8/T4/T8H/T4HL/T4HH   palette[index], the palette prepared by
//                    ReadPalette: CT32 entries raw, CT16 entries through
//                    ApplyTexa16 (the CLUT colour is a 16-bit texel, same rule).
//   PSMZ32/Z24/Z16/Z16S   the Z buffer read back as the colour it aliases: Z32
//                    the word, Z24 the low 24 bits (alpha 0), Z16/Z16S
//                    TexaExpand::Expand16To32 (the TEXA-less frame-readback form,
//                    PixelConvert's convention for a 16-bit FRAME value -- a Z
//                    read is a buffer readback, not texture sampling).
//
// Footprint helpers. DecodeFootprint / DecodeRowBand give the page-granular byte
// window [start, start + size) a decode of the texture (or of its rows
// [y0, y1)) can touch, from the section 8.2 page arithmetic: the last page
// index is (h-1)/page_h * pages_per_row + (w-1)/page_w (both terms monotonic,
// so the maximum is at the far corner); a rect wider than the buffer walks into
// the next row's pages, which the formula covers. Consumers (texture cache
// keys, flush narrowing) are Phase 6.
//
// SIMD. With the CMake option PS2R_SIMD the 16-bit value expansion runs eight
// texels at a time (NEON); SimdEnabled() reports it. DecodeScalar is always
// compiled and Decode == DecodeScalar byte for byte (property test).
//
// Units: Texture::tbp in 64-word blocks (TBP0), Texture::tbw in 64-pixel
// units (TBW, 0 behaves as 1 -- PsmLayout::PagesPerRow), Rect in
// buffer-relative pixels; Palette::cbp in blocks (CBP).
#pragma once

#include <cstdint>

#include "engine/gs/format/psm.h"
#include "engine/gs/format/texa_expand.h"

namespace ps2r {
namespace gs {

class BlockDecode {
public:
    static constexpr uint32_t kVramBytes = 4u * 1024u * 1024u;
    static constexpr uint32_t kPageBytes = 8192;

    struct Texture {
        Psm psm;
        uint32_t tbp;   // 256-byte blocks
        uint32_t tbw;   // 64-pixel units
        uint32_t w, h;  // pixels (level size)
    };

    struct Palette {
        Psm cpsm;       // CT32 / CT16 / CT16S (TEX0.CPSM)
        uint32_t cbp;   // blocks (TEX0.CBP)
        bool csm2;      // TEX0.CSM = 1
    };

    struct Rect { uint32_t x, y, w, h; };

    struct Footprint { uint32_t start, size; };  // bytes; size <= 4 MB

    // Entries a mode's palette has: 256 for T8/T8H, 16 for T4/T4HL/T4HH, 0 otherwise.
    static uint32_t PaletteEntries(Psm psm);

    // The generated page LUT for a mode: page_w * page_h element offsets, row-major.
    static const uint16_t* PageLut(Psm psm);

    // palette[i] = RGBA32 value of CLUT entry i (see the value rules above).
    static void ReadPalette(const uint8_t* vram, const Palette& pal, uint32_t entries,
                            const TexaExpand::Texa& texa, uint32_t* out);

    // rect of tex -> out (rect.w * rect.h RGBA32 words, row-major, pitch rect.w).
    // `palette` is ReadPalette's output for indexed modes and ignored otherwise.
    static void Decode(const uint8_t* vram, const Texture& tex, const Rect& rect,
                       const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out);
    // Whole texture.
    static void Decode(const uint8_t* vram, const Texture& tex,
                       const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out);

    // The scalar inner loop (always available) and the per-pixel PsmLayout
    // reference; both have Decode's contract and are byte-identical to it.
    static void DecodeScalar(const uint8_t* vram, const Texture& tex, const Rect& rect,
                             const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out);
    static void DecodeGeneric(const uint8_t* vram, const Texture& tex, const Rect& rect,
                              const TexaExpand::Texa& texa, const uint32_t* palette, uint32_t* out);
    static bool SimdEnabled();

    // One texel by the value rules (element already fetched); public for tests.
    static uint32_t TexelValue(Psm psm, uint32_t element, const TexaExpand::Texa& texa,
                               const uint32_t* palette);

    // Page-granular windows (section 8.2 arithmetic; see the header comment).
    static Footprint DecodeFootprint(const Texture& tex);
    // Rows [y0, y1) of tex: start is RELATIVE to the buffer base (tbp).
    static Footprint DecodeRowBand(const Texture& tex, uint32_t y0, uint32_t y1);
};

}  // namespace gs
}  // namespace ps2r
