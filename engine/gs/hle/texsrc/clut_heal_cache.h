// gs/hle/texsrc/clut_heal_cache.h -- ClutHealCache: substitutes the last good
// 256-entry palette seen at a CLUT base when the current one decodes to all zero.
//
// This is a WORKAROUND, not a GS rule: a real GS samples whatever the CLUT
// buffer holds. It exists because the title's texture uploads race the CLUT
// loads in the emulated stream; it is quarantined here until the root fix lands
// (HOOK_RECLASSIFICATION ladder) and moves to the title's workarounds at
// integration. Mechanism: for a CLUT base the layout does NOT exempt (the
// text-atlas CLUTs are exempt -- their all-zero palettes are legitimate), a
// palette whose 256 entries are all zero is replaced by the cached palette for
// that base, and a palette with >= 200 entries of PS2 alpha >= 100 becomes the
// cached one. The cache never primes from a zero mirror, so it is a no-op in
// replay.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/gs/hle/targets/vram_layout.h"

namespace ps2r {
namespace gs {

class ClutHealCache {
public:
    static constexpr int kGoodAlphaMin = 100;     // PS2-domain alpha threshold
    static constexpr int kGoodEntriesMin = 200;   // entries needed to become the cached palette
    // Apply the rule to `palette` (256 RGBA32 words, PS2 alpha) for CLUT base `cbp`.
    // Returns true when the palette was replaced.
    bool Heal(uint32_t cbp, uint32_t* palette, const VramLayout& layout);
    bool HasCached(uint32_t cbp) const { return cache_.count(cbp) != 0; }
    void Clear() { cache_.clear(); }

private:
    std::unordered_map<uint32_t, std::vector<uint32_t>> cache_;
};

}  // namespace gs
}  // namespace ps2r
