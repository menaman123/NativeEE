// gs/format/mip_chain.cpp -- MIPTBP1/MIPTBP2 field unpacking and level sizing
// (GS_SKILL section 3.4.11, the MIPTBP register tables, TEX1 MXL/MTBA).

#include "engine/gs/format/mip_chain.h"

namespace ps2r {
namespace gs {

uint32_t MipChain::Mxl(uint64_t tex1) { return uint32_t((tex1 >> 2) & 0x7u); }

bool MipChain::Mtba(uint64_t tex1) { return ((tex1 >> 9) & 1u) != 0; }

uint32_t MipChain::PairTbp(uint64_t miptbp, uint32_t slot) {
    return uint32_t((miptbp >> (20u * slot)) & 0x3FFFu);
}

uint32_t MipChain::PairTbw(uint64_t miptbp, uint32_t slot) {
    const uint32_t tbw = uint32_t((miptbp >> (20u * slot + 14u)) & 0x3Fu);
    return tbw == 0 ? 1u : tbw;   // register note *2: less than 64 texels -> 1
}

uint32_t MipChain::LevelSize(uint32_t base, uint32_t level) {
    const uint32_t s = level >= 32u ? 0u : (base >> level);
    return s == 0 ? 1u : s;
}

uint32_t MipChain::LevelTbp(uint64_t miptbp1, uint64_t miptbp2, uint32_t level) {
    return level <= 3u ? PairTbp(miptbp1, level - 1u) : PairTbp(miptbp2, level - 4u);
}

uint32_t MipChain::LevelTbw(uint64_t miptbp1, uint64_t miptbp2, uint32_t level) {
    return level <= 3u ? PairTbw(miptbp1, level - 1u) : PairTbw(miptbp2, level - 4u);
}

MipChain::Chain MipChain::Build(Psm psm, uint32_t tbp0, uint32_t tbw0, uint32_t tw, uint32_t th,
                                uint64_t tex1, uint64_t miptbp1, uint64_t miptbp2) {
    Chain c{};
    c.psm = psm;
    c.mxl = Mxl(tex1) > kMaxLevel ? kMaxLevel : Mxl(tex1);
    c.mtba = Mtba(tex1);
    const uint32_t w0 = 1u << (tw & 15u), h0 = 1u << (th & 15u);
    c.level[0] = Level{tbp0, tbw0 == 0 ? 1u : tbw0, w0, h0};
    for (uint32_t l = 1; l <= c.mxl; l++) {
        c.level[l] = Level{LevelTbp(miptbp1, miptbp2, l), LevelTbw(miptbp1, miptbp2, l),
                           LevelSize(w0, l), LevelSize(h0, l)};
    }
    return c;
}

namespace {
uint64_t PackPair(uint32_t tbp, uint32_t tbw, uint32_t slot) {
    return (uint64_t(tbp & 0x3FFFu) | (uint64_t(tbw & 0x3Fu) << 14)) << (20u * slot);
}
}  // namespace

uint64_t MipChain::PackMiptbp1(const Chain& chain) {
    uint64_t r = 0;
    for (uint32_t l = 1; l <= 3u && l <= chain.mxl; l++) r |= PackPair(chain.level[l].tbp, chain.level[l].tbw, l - 1u);
    return r;
}

uint64_t MipChain::PackMiptbp2(const Chain& chain) {
    uint64_t r = 0;
    for (uint32_t l = 4; l <= 6u && l <= chain.mxl; l++) r |= PackPair(chain.level[l].tbp, chain.level[l].tbw, l - 4u);
    return r;
}

}  // namespace gs
}  // namespace ps2r
