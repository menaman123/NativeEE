// gs/hle/present/present_policy.cpp -- the per-boundary present decision.
#include "engine/gs/hle/present/present_policy.h"

namespace ps2r {
namespace gs {

const char* PresentPolicy::DecisionName(Decision d) {
    switch (d) {
        case Decision::None: return "none";
        case Decision::Live: return "live";
        case Decision::Hold: return "hold";
    }
    return "?";
}

void PresentPolicy::SetFpsCap(uint32_t cap) {
    fps_cap_ = cap >= kDisplayRateHz ? 0u : cap;
    cap_acc_ = 0;
}

bool PresentPolicy::CapAdmits() {
    if (fps_cap_ == 0) return true;
    // An even share of the display fields: cap of every kDisplayRateHz fields,
    // the remainder carried (30 -> every other field, 20 -> every third, ...).
    cap_acc_ += fps_cap_;
    if (cap_acc_ >= kDisplayRateHz) {
        cap_acc_ -= kDisplayRateHz;
        return true;
    }
    return false;
}

PresentPolicy::Decision PresentPolicy::Decide(bool live_available, bool fmv_active) {
    if (fmv_active) ++fmv_boundaries_;
    if (live_available) {
        // The first image is always shown; afterwards the cap takes its share.
        if (!has_image_ || CapAdmits()) {
            has_image_ = true;
            ++lives_;
            return Decision::Live;
        }
        ++cap_skips_;
        ++holds_;
        return Decision::Hold;
    }
    if (has_image_) {
        ++holds_;
        return Decision::Hold;
    }
    ++nones_;
    return Decision::None;
}

void PresentPolicy::ForgetImage() {
    has_image_ = false;
    ++forgotten_;
}

std::string PresentPolicy::Census() const {
    return "live=" + std::to_string(lives_) + ",hold=" + std::to_string(holds_) + ",none=" + std::to_string(nones_) +
           ",cap=" + std::to_string(fps_cap_) + ",cap_skips=" + std::to_string(cap_skips_) +
           ",fmv=" + std::to_string(fmv_boundaries_);
}

}  // namespace gs
}  // namespace ps2r
