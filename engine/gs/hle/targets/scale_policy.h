// gs/hle/targets/scale_policy.h -- ScalePolicy: the ONE resolution factor applied
// at the frame-buffer-identity layer. Logical target sizes stay native; physical
// storage = logical * factor; rasterisation scales through the viewport alone.
// Two pins keep a target native regardless of the factor: the title's scene
// accumulator (its composite into a scaled target provides scale-on-composite)
// and the fog lanes (16-bit strips whose deposits feed CPU decodes).
#pragma once

#include <cstdint>

#include "engine/gs/hle/targets/vram_layout.h"

namespace ps2r {
namespace gs {

struct ScaleSettings {
    int scale = 1;   // integer factor, clamped to [1, 4]
};

class ScalePolicy {
public:
    explicit ScalePolicy(const ScaleSettings& s, const VramLayout* layout);
    int Factor() const { return factor_; }
    bool On() const { return factor_ > 1; }
    // A live change (the Display page's internal scale): clamped to [1, 4]. The
    // target cache re-creates a target whose identity scale differs at its next
    // GetOrCreate (content rescued into the image first), so the change lands
    // at the frame boundary the caller applies it on -- the backend owner's.
    void SetFactor(int factor) { factor_ = factor < 1 ? 1 : (factor > 4 ? 4 : factor); }
    bool IsPinnedNative(uint32_t fbp) const;
    // The scale a target at `fbp` is created with.
    int TargetScale(uint32_t fbp) const;

private:
    int factor_;
    const VramLayout* layout_;
};

}  // namespace gs
}  // namespace ps2r
