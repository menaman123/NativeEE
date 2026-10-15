// gs/hle/texsrc/mip_sampler.h -- MipSampler: which MIPMAP levels a read authors
// and the LOD parameters the sampler applies (GS User's Manual 3.4.3 TEX1, 3.4.11
// MIPTBP, 3.4.12 LOD).
//
// A chain is captured only when the read can mip-sample at all: MMIN >= 2 (the
// 0/1 filters never select a level) and MXL > 0; MTBA = 1 (automatic bases) is
// not modelled and falls back to level 0. Levels 1..MXL take (TBP, TBW) from the
// MIPTBP registers (a stored TBW 0 reads as 1 unit) and halve the size per level
// (never below 1); a level whose page footprint is empty, exceeds the per-level
// cap, or runs past the 4 MiB ends the chain there. K is the s7.4 LOD bias
// (12 bits, sign-extended), L the LOD shift, LCM the fixed-LOD select.
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/tex_read_view.h"

namespace ps2r {
namespace gs {

class MipSampler {
public:
    static constexpr uint32_t kMaxLevels = 6;
    static constexpr uint32_t kMaxLevelWindowBytes = 1u << 20;
    struct Level {
        uint32_t tbp_bytes = 0;
        uint32_t tbw_px = 0;
        uint32_t w = 0, h = 0;
        uint32_t fp_start = 0, fp_size = 0;   // page-granular VRAM window
    };
    struct Chain {
        uint8_t levels = 0;      // authored levels past 0 (0 = no chain)
        bool lcm = false;
        uint8_t l = 0;
        int16_t k = 0;
        Level level[kMaxLevels];
    };
    static Chain Capture(const TexReadView& v);
    static int16_t SignExtendK(uint16_t raw_k);
};

}  // namespace gs
}  // namespace ps2r
