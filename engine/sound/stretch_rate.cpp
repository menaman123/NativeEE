// StretchRate implementation; the terms are described in the header.
#include "engine/sound/stretch_rate.h"

#include <algorithm>
#include <cmath>

namespace ps2r::sound {

StretchRate::StretchRate() : intervals_(static_cast<size_t>(kRateWindow), 0.0) { scratch_.reserve(static_cast<size_t>(kRateWindow)); }

double StretchRate::Next(double dt_s, int fill, int lead) {
    ++pushes_;
    if (dt_s > kMaxIntervalS) {
        head_ = count_ = 0;
        fill_drift_ = fill_deficit_ = -1.0;
        ++resets_;
    } else if (dt_s >= 0.0) {
        if (count_ < kRateWindow) ++count_;
        intervals_[static_cast<size_t>(head_)] = dt_s;
        head_ = (head_ + 1) % kRateWindow;
    }
    if (fill_drift_ < 0.0) fill_drift_ = fill_deficit_ = fill;
    else {
        fill_drift_ += (fill - fill_drift_) / kDriftTau;
        fill_deficit_ += (fill - fill_deficit_) / kDeficitTau;
    }
    // The rate: the trimmed mean of the window's intervals (the longest and the shortest kRateTrim each
    // dropped): a burst of lost fields -- a cluster of two-field pictures at a movie's end, a level load's
    // long frames -- is a transient the fill terms bridge, not a rate the next two seconds should be
    // stretched by; a game that is slow on every frame moves the whole window.
    rate_ratio_ = 1.0;
    if (count_ >= kRateMinPushes) {
        scratch_.assign(intervals_.begin(), intervals_.begin() + count_);
        std::sort(scratch_.begin(), scratch_.end());
        const int trim = static_cast<int>(count_ * kRateTrim);
        double sum = 0.0;
        for (int i = trim; i < count_ - trim; ++i) sum += scratch_[static_cast<size_t>(i)];
        rate_ratio_ = (sum / (count_ - 2 * trim)) * kFieldsPerSecond;
        if (std::abs(rate_ratio_ - 1.0) < kRateDeadBand) rate_ratio_ = 1.0;
    }
    const double lead_f = std::max(1.0, static_cast<double>(lead));
    const double drift = std::clamp((lead_f - fill_drift_) / lead_f, -1.0, 1.0);
    double deficit = std::clamp((lead_f - fill_deficit_) / lead_f, -1.0, 1.0);
    if (std::abs(deficit) <= kDeadBand) deficit = 0.0;
    else deficit -= deficit > 0.0 ? kDeadBand : -kDeadBand;
    double ratio = rate_ratio_ * (1.0 + kDriftGain * drift + kDeficitGain * deficit);
    if (std::abs(ratio - 1.0) < kSnap) ratio = 1.0;
    // The guard: this push must leave kGuardTargetFrames in the ring -- the headroom one two-field field
    // (33 ms = 1600 frames) plus a device pull needs -- else the next late field pads (a repeat).
    if (fill + kQuantumFrames * ratio < kGuardTargetFrames) {
        ratio = std::min(kGuardMaxRatio, static_cast<double>(kGuardTargetFrames - fill) / kQuantumFrames);
        ++guard_pushes_;
    }
    ratio_ = std::clamp(ratio, kMinRatio, kMaxRatio);
    return ratio_;
}

}  // namespace ps2r::sound
