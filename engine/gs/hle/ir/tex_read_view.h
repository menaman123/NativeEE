// gs/hle/ir/tex_read_view.h -- the texture-read register set a draw was kicked
// with: TEX0 (base, width, format, size, TFX/TCC, CLUT base/format/mode/offset/
// load), TEX1 (filters, mip levels, LOD source), CLAMP, TEXA and MIPTBP1/2 -- the
// complete input the texture-source resolver needs (GS User's Manual 3.4.4 TEX0,
// 3.4.5 TEX1, 3.4.6 TEXA, 3.4.7 CLAMP, 3.4.11 MIPTBP).
//
// Addresses are BYTE addresses (the register's 64-word / 256-byte units already
// multiplied out); tbw is in pixels; tw/th are log2 sizes as TEX0 carries them.
#pragma once

#include <cstdint>

namespace ps2r {
namespace gs {

struct TexReadView {
    // TEX0
    uint32_t tbp = 0;      // texture base, bytes
    uint32_t tbw = 0;      // buffer width, pixels
    uint32_t psm = 0;      // texture PSM
    uint32_t tw = 0;       // log2 width
    uint32_t th = 0;       // log2 height
    uint32_t tcc = 0;      // 0 = RGB, 1 = RGBA
    uint32_t tfx = 0;      // 0 MODULATE, 1 DECAL, 2 HIGHLIGHT, 3 HIGHLIGHT2
    uint32_t cbp = 0;      // CLUT base, bytes
    uint32_t cpsm = 0;     // CLUT PSM
    uint32_t csm = 0;      // CLUT storage mode
    uint32_t csa = 0;      // CLUT entry offset (16-entry units)
    uint32_t cld = 0;      // CLUT load control
    bool fst = false;      // the UV-vs-STQ register the kick read
    // TEX1
    bool mmag = false;     // 0 nearest, 1 linear
    uint8_t mmin = 0;      // minification filter 0..5
    uint8_t mxl = 0;       // mip levels beyond 0
    bool lcm = false;      // 0 = LOD from Q, 1 = fixed K
    uint8_t l = 0;         // LOD shift
    int16_t k = 0;         // LOD bias, s7.4
    // CLAMP
    uint8_t wms = 0, wmt = 0;
    uint16_t minu = 0, maxu = 0, minv = 0, maxv = 0;
    // TEXA
    uint8_t texa_ta0 = 0;
    uint8_t texa_ta1 = 0x80;
    bool texa_aem = false;
    // MIPTBP1 / MIPTBP2 (levels 1..3 / 4..6): base bytes and width pixels
    uint32_t mip_tbp[6] = {0, 0, 0, 0, 0, 0};
    uint32_t mip_tbw[6] = {0, 0, 0, 0, 0, 0};
};

}  // namespace gs
}  // namespace ps2r
