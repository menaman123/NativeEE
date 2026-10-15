// gs/format/mip_chain.h -- where the MIPMAP levels of a texture live (GS_SKILL
// section 3.4.11 MIPMAP; MIPTBP1_1/2 (0x34/0x35) and MIPTBP2_1/2 (0x36/0x37)
// register tables; TEX1 MXL / MTBA fields, section 3.4.3).
//
// The addressing half of mipmapping only. A texture has up to seven levels
// (0..6); level 0 is TEX0 (TBP0, TBW0, TW, TH) and each deeper level halves the
// width and height (section 3.4.11: 2^(m-l) x 2^(n-l), never below 1). Levels
// 1-3 take their base pointer and buffer width from MIPTBP1, levels 4-6 from
// MIPTBP2: each register packs three {TBP 14 bits, TBW 6 bits} pairs at bit
// 20 * (slot) -- TBPn in 64-word blocks (the same unit as TBP0), TBWn in
// 64-texel units, "set to 1 if the width is less than 64" (register note *2:
// a stored 0 is read as 1). MXL (TEX1 bits 4:2, 0..6) is the deepest level the
// GS may sample; Levels() lists levels 0..MXL.
//
// MTBA = 1 (TEX1 bit 9, automatic base pointer calculation for levels 1-3 from
// the format and square size, section 3.4.11) is NOT modelled here: the
// register values are read as written. If a title uses MTBA the Phase-6 texture
// source computes the automatic bases from PsmLayout page sizes; this class
// only reports the flag.
//
// LOD selection (LCM, L, K, MMIN/MMAG; section 3.4.12), trilinear blending and
// every sampling policy belong to gs/hle/texsrc (Phase 6); nothing here decides
// which level is used.
#pragma once

#include <cstdint>

#include "engine/gs/format/psm.h"

namespace ps2r {
namespace gs {

class MipChain {
public:
    static constexpr uint32_t kMaxLevel = 6;    // levels 0..6
    static constexpr uint32_t kLevelCount = kMaxLevel + 1;

    struct Level {
        uint32_t tbp;   // 64-word blocks
        uint32_t tbw;   // 64-texel units (>= 1)
        uint32_t w, h;  // texels
    };

    struct Chain {
        Psm psm;
        uint32_t mxl;                 // deepest level present (0..6)
        Level level[kLevelCount];     // level[0..mxl] valid
        bool mtba;                    // TEX1.MTBA as written (reported, not applied)
    };

    // TEX1 fields (section 3.4.3): MXL bits 4:2, MTBA bit 9.
    static uint32_t Mxl(uint64_t tex1);
    static bool Mtba(uint64_t tex1);

    // MIPTBP pair `slot` (0..2) of a MIPTBP1/MIPTBP2 register word: TBP bits
    // 13:0, TBW bits 19:14 (0 read as 1), at bit 20 * slot.
    static uint32_t PairTbp(uint64_t miptbp, uint32_t slot);
    static uint32_t PairTbw(uint64_t miptbp, uint32_t slot);

    // Level size: max(1, base >> level).
    static uint32_t LevelSize(uint32_t base, uint32_t level);

    // (tbp, tbw) of `level` (1..6) from the two registers; level 0 is TEX0's.
    static uint32_t LevelTbp(uint64_t miptbp1, uint64_t miptbp2, uint32_t level);
    static uint32_t LevelTbw(uint64_t miptbp1, uint64_t miptbp2, uint32_t level);

    // The chain for TEX0 (psm, tbp0, tbw0, tw, th -- log2 sizes), TEX1 (MXL,
    // MTBA) and the two MIPTBP registers. mxl is clamped to 6.
    static Chain Build(Psm psm, uint32_t tbp0, uint32_t tbw0, uint32_t tw, uint32_t th,
                       uint64_t tex1, uint64_t miptbp1, uint64_t miptbp2);

    // Inverse of the field packing (for tests and for re-encoding a chain):
    // the MIPTBP1 / MIPTBP2 words holding levels 1-3 / 4-6 of `chain`.
    static uint64_t PackMiptbp1(const Chain& chain);
    static uint64_t PackMiptbp2(const Chain& chain);
};

}  // namespace gs
}  // namespace ps2r
