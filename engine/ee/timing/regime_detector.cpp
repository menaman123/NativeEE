#include "engine/ee/timing/regime_detector.h"

RegimeDetector::Snapshot RegimeDetector::Capture() const { return {n_, samples_, over_, under_, fast_}; }

bool RegimeDetector::Validate(const Snapshot& state) {
    return state.n >= kMinN && state.n <= kMaxN &&
        state.samples < (state.fast ? kFastWin : kFullWin) &&
        state.over <= state.samples && state.under <= state.samples;
}

bool RegimeDetector::Restore(const Snapshot& state) {
    if (!Validate(state)) return false;
    n_ = state.n; samples_ = state.samples; over_ = state.over; under_ = state.under; fast_ = state.fast;
    return true;
}

void RegimeDetector::Seed() {
    n_ = 1;
    samples_ = over_ = under_ = 0;
    fast_ = true;
}

bool RegimeDetector::OnBoundary(uint64_t arrival_gap, uint64_t final_gap) {
    ++samples_;
    if (final_gap > static_cast<uint64_t>(n_)) ++over_;
    if (arrival_gap + 1 < static_cast<uint64_t>(n_)) ++under_;
    const uint32_t win = fast_ ? kFastWin : kFullWin;
    if (samples_ < win) return false;
    const double fo = static_cast<double>(over_) / static_cast<double>(samples_);
    const double fu = static_cast<double>(under_) / static_cast<double>(samples_);
    const double up_th = fast_ ? kUpFracFast : kUpFrac;
    const double dn_th = fast_ ? kDownFracFast : kDownFrac;
    bool changed = false;
    if (fo > up_th && n_ < kMaxN) {
        ++n_;
        changed = true;
    } else if (fu > dn_th && n_ > kMinN) {
        --n_;
        changed = true;
    }
    samples_ = over_ = under_ = 0;
    fast_ = false;
    return changed;
}
