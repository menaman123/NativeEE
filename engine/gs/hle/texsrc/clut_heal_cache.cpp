// gs/hle/texsrc/clut_heal_cache.cpp -- ClutHealCache.
#include "engine/gs/hle/texsrc/clut_heal_cache.h"

#include <cstring>

namespace ps2r {
namespace gs {

bool ClutHealCache::Heal(uint32_t cbp, uint32_t* palette, const VramLayout& layout) {
    if (layout.IsClutHealExempt(cbp)) return false;
    int nonzero = 0, good_alpha = 0;
    for (int i = 0; i < 256; i++) {
        if (palette[i] != 0) nonzero++;
        if ((palette[i] >> 24) >= static_cast<uint32_t>(kGoodAlphaMin)) good_alpha++;
    }
    if (nonzero == 0) {
        auto it = cache_.find(cbp);
        if (it == cache_.end()) return false;
        std::memcpy(palette, it->second.data(), 256 * sizeof(uint32_t));
        return true;
    }
    if (good_alpha >= kGoodEntriesMin) cache_[cbp].assign(palette, palette + 256);
    return false;
}

}  // namespace gs
}  // namespace ps2r
