// gs/hle/targets/scale_policy.cpp -- factor clamp and the native pins.
#include "engine/gs/hle/targets/scale_policy.h"

namespace ps2r {
namespace gs {

ScalePolicy::ScalePolicy(const ScaleSettings& s, const VramLayout* layout)
    : factor_(s.scale < 1 ? 1 : (s.scale > 4 ? 4 : s.scale)), layout_(layout) {}

bool ScalePolicy::IsPinnedNative(uint32_t fbp) const {
    if (!layout_) return false;
    return layout_->IsSceneAccum(fbp) || layout_->IsFogLane(fbp);
}

int ScalePolicy::TargetScale(uint32_t fbp) const {
    return IsPinnedNative(fbp) ? 1 : factor_;
}

}  // namespace gs
}  // namespace ps2r
