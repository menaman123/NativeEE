// gs/hle/texsrc/mip_sampler.cpp -- MipSampler.
#include "engine/gs/hle/texsrc/mip_sampler.h"

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/mip_chain.h"
#include "engine/gs/format/psm_layout.h"

namespace ps2r {
namespace gs {

int16_t MipSampler::SignExtendK(uint16_t raw_k) {
    int16_t k = static_cast<int16_t>(raw_k & 0xFFF);
    if (k & 0x800) k = static_cast<int16_t>(k - 0x1000);
    return k;
}

MipSampler::Chain MipSampler::Capture(const TexReadView& v) {
    Chain c;
    if (v.mmin < 2 || v.mxl == 0) return c;
    Psm psm = Psm::CT32;
    if (!PsmLayout::FromField(v.psm, &psm)) return c;
    const uint32_t base_w = 1u << v.tw, base_h = 1u << v.th;
    const uint32_t n = v.mxl > kMaxLevels ? kMaxLevels : v.mxl;
    for (uint32_t i = 1; i <= n; i++) {
        Level& lv = c.level[i - 1];
        lv.tbp_bytes = v.mip_tbp[i - 1];
        lv.tbw_px = v.mip_tbw[i - 1] == 0 ? 64 : v.mip_tbw[i - 1];
        lv.w = MipChain::LevelSize(base_w, i);
        lv.h = MipChain::LevelSize(base_h, i);
        BlockDecode::Texture t;
        t.psm = psm;
        t.tbp = lv.tbp_bytes / PsmLayout::kBlockBytes;
        t.tbw = lv.tbw_px / 64;
        t.w = lv.w;
        t.h = lv.h;
        const BlockDecode::Footprint fp = BlockDecode::DecodeFootprint(t);
        if (fp.size == 0 || fp.size > kMaxLevelWindowBytes ||
            static_cast<uint64_t>(fp.start) + fp.size > BlockDecode::kVramBytes)
            break;
        lv.fp_start = fp.start;
        lv.fp_size = fp.size;
        c.levels = static_cast<uint8_t>(i);
    }
    if (c.levels == 0) return c;
    c.lcm = v.lcm;
    c.l = v.l;
    c.k = v.k;
    return c;
}

}  // namespace gs
}  // namespace ps2r
