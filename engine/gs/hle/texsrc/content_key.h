// gs/hle/texsrc/content_key.h -- ContentKey: the address-free key of a decoded
// texture.
//
// Same key => Decode produces byte-identical RGBA, which is what makes serving a
// cached texture on a key hit correct. The key covers exactly the decode's
// inputs: the register tuple that parameterises it (tbw, psm, tw, th, cpsm, csm,
// csa), TEXA, an optional UV-bounded sub-rect (which then also narrows the hashed
// window to the page-row band covering the rect's rows), the bytes of the VRAM
// window the decode can read, the CLUT bytes and every mip level's (tbw, window
// bytes). It deliberately excludes TBP / CBP: every texel address resolves
// through (x, y, tbw, psm) relative to the window start, so the base term cancels
// and folding it in could only split identical content into distinct keys (a title
// that rotates texture uploads through double-buffered addresses would then never
// re-hit). The tuple and texa steps are byte-serial FNV-1a; the content steps are
// xxHash64 mixed in (fast) or FNV-1a (reference) -- both variants are pinned.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gs/hle/ir/tex_read_view.h"

namespace ps2r {
namespace gs {

class ContentKey {
public:
    struct MipInput {
        uint32_t tbw_px;            // level buffer width, pixels
        const uint8_t* window;      // level window bytes
        size_t window_len;
    };
    struct Rect { int x0 = 0, y0 = 0, w = 0, h = 0; };   // w == h == 0: full decode

    // Page-granular byte footprint of the level-0 decode (window the key hashes).
    static void Footprint(const TexReadView& v, uint32_t* start, uint32_t* size);
    // The row band [y0, y1) of the footprint, relative to its start (sub-rect keys).
    static void RowBand(const TexReadView& v, int y0, int y1, uint32_t* rel, uint32_t* size);
    // `window` = the footprint bytes (RowBand slice when `rgn` is a genuine
    // sub-rect); `clut` = the 8 KiB CLUT page bytes (may be empty).
    static uint64_t Compute(const TexReadView& v, const Rect& rgn, const uint8_t* window,
                            size_t window_len, const uint8_t* clut, size_t clut_len,
                            const MipInput* mips, size_t mip_count, bool fast_hash);
    static bool IsSubRect(const TexReadView& v, const Rect& rgn);
    static uint64_t Fnv1aStep(uint64_t h, const void* data, size_t n);
};

}  // namespace gs
}  // namespace ps2r
