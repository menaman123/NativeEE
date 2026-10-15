// gs/format/swizzle.h -- THE rect walker between linear pixel rows and GS local
// memory, for every PSM, over PsmLayout::PixelAddress (GS_SKILL chapter 8).
//
// Both directions and every role are the same two primitives:
//   VramToLinear -- the texture decoder's read side (a texture rect -> a linear
//                   image of indices / pixels), local->host transfers;
//   LinearToVram -- the transfer engine's write side (HWREG image upload: each
//                   arriving row segment is a 1-row rect) and the render-target
//                   encode/sync (an FBO readback rect -> the frame/Z buffer).
// No other code in the tree computes a swizzled address for a rect.
//
// Linear side: one PIXEL per position, LinearBitsPerPixel(psm) wide, rows
// `pitch` bytes apart, packed little-endian; 4-bit pixels fill the low nibble
// first (the GS IMAGE stream order). The pixel holds exactly the mode's bits:
// 32 for CT32/Z32, the low 24 for CT24/Z24 (upper byte 0), 16 for the 16-bit
// class, the 8- or 4-bit index for T8/T4/T8H/T4HL/T4HH.
//
// VRAM side: a 4 MB view of GS local memory; addresses wrap at 4 MB (the
// PsmLayout rule). Writes touch only the bits ElementWriteMask(psm) selects
// (CT24/Z24 leave the upper byte, T8H/T4HL/T4HH only their index field -- the
// co-page use GS_SKILL section 2.6.3 describes), further reduced by the caller's
// `mask` (e.g. the writable bits left by FBMSK); masked writes read-modify-write.
//
// Units: Buffer::bp in 64-word blocks, Buffer::bw in 64-pixel units, Rect in
// buffer-relative pixels -- the PsmLayout conventions.
//
// The fast path walks each row in block-aligned runs with the per-mode
// element table hoisted (no per-pixel division); the Generic variants are the
// per-pixel PsmLayout reference. They are byte-identical by construction and
// tests/gs/format/test_swizzle.cpp pins it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gs/format/psm.h"

namespace ps2r {
namespace gs {

class Swizzle {
public:
    static constexpr uint32_t kVramBytes = 4u * 1024u * 1024u;
    static constexpr uint32_t kAllBits = 0xFFFFFFFFu;

    struct Buffer {
        Psm psm;
        uint32_t bp;  // 256-byte blocks (TBP0 / DBP / SBP / CBP convention)
        uint32_t bw;  // 64-pixel units (TBW / DBW / SBW / FBW)
    };

    struct Rect {
        uint32_t x, y;  // buffer-relative top-left pixel
        uint32_t w, h;  // size in pixels
    };

    // Bits one linear pixel occupies: the index width for indexed modes, else
    // the storage width (32 / 16).
    static uint32_t LinearBitsPerPixel(Psm psm);

    // Minimum linear row pitch in bytes for `w` pixels (nibbles rounded up).
    static size_t MinPitch(Psm psm, uint32_t w);

    // Bits of a storage element that belong to the mode's pixel (section 2.3.1 /
    // 2.6.3): 0x00FFFFFF for CT24/Z24, the index field for T8H/T4HL/T4HH, all
    // element bits otherwise.
    static uint32_t ElementWriteMask(Psm psm);

    // Linear rows -> VRAM (transfer write side, RT encode side).
    static void LinearToVram(uint8_t* vram, const Buffer& buf, const Rect& rect,
                             const uint8_t* src, size_t src_pitch, uint32_t mask = kAllBits);

    // VRAM -> linear rows (decoder read side, local->host).
    static void VramToLinear(const uint8_t* vram, const Buffer& buf, const Rect& rect,
                             uint8_t* dst, size_t dst_pitch);

    // Reference implementations: one PsmLayout::PixelAddress per pixel. Same
    // contract as the two above; kept public for the identity property test.
    static void LinearToVramGeneric(uint8_t* vram, const Buffer& buf, const Rect& rect,
                                    const uint8_t* src, size_t src_pitch, uint32_t mask = kAllBits);
    static void VramToLinearGeneric(const uint8_t* vram, const Buffer& buf, const Rect& rect,
                                    uint8_t* dst, size_t dst_pitch);
};

}  // namespace gs
}  // namespace ps2r
