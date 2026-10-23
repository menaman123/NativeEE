#include "engine/vu/tiers/runahead_guard.h"

#include <algorithm>

RunaheadGuard::RunaheadGuard(int depth_frames, std::function<bool()> low_band, const MtvuClock& clock)
    : depth_frames_(depth_frames < 0 ? 0u : static_cast<uint64_t>(depth_frames)),
      low_band_(std::move(low_band)),
      clock_(clock) {}

uint64_t RunaheadGuard::EffMaxFrames() const {
    if (depth_frames_ == kLiftBaseDepth && low_band_ && low_band_()) return kLiftedDepth;
    return depth_frames_;
}

uint64_t RunaheadGuard::EffMaxFrames(uint64_t declared) const {
    const uint64_t setting = EffMaxFrames();
    if (hold_frames_ != 0 && hold_frames_ < setting && !HoldLifted(declared)) return hold_frames_;
    return setting;
}

bool RunaheadGuard::NoteHoldPark(uint64_t declared, uint64_t engaged_at_ns) {
    if (hold_frames_ == 0 || HoldLifted(declared)) return false;
    if (clock_.NowNs() < engaged_at_ns + kHoldLiftNs) return false;
    hold_lift_until_ = declared + kHoldLiftBoundaries;
    ++hold_lifts_;
    return true;
}

uint64_t RunaheadGuard::DepthFrames(uint64_t declared, uint64_t presented) {
    const int64_t d = static_cast<int64_t>(declared) - static_cast<int64_t>(presented);
    return d < 0 ? 0u : static_cast<uint64_t>(d);
}

bool RunaheadGuard::Over(uint64_t declared, uint64_t presented) const {
    const uint64_t maxf = EffMaxFrames(declared);
    return maxf != 0 && DepthFrames(declared, presented) > maxf;
}

RunaheadFramePark RunaheadGuard::FramePark(uint64_t declared, uint64_t presented,
                                           uint64_t engaged_at_ns) const {
    RunaheadFramePark r;
    if (!Over(declared, presented)) return r;
    if (clock_.NowNs() >= engaged_at_ns + kFrameValveNs) {
        r.valve_hit = true;
        return r;
    }
    r.park = true;
    return r;
}

uint64_t RunaheadGuard::NowNs() const { return clock_.NowNs(); }

RunaheadGuard::Tripwire::Result RunaheadGuard::Tripwire::Feed(uint64_t depth_frames, uint64_t cap_frames) {
    Result r;
    r.over = depth_frames > cap_frames + 1;
    if (r.over) {
        if (over_run != UINT32_MAX) ++over_run;
        peak_depth = std::max(peak_depth, depth_frames);
        if (!latched && over_run > kPersistPresents) {
            latched = true;
            if (reports < kMaxReports) {
                ++reports;
                r.fire = true;
                r.peak = peak_depth;
                r.run = over_run;
            }
        }
    } else {
        // Recovery re-arms a later, distinct episode.
        over_run = 0;
        peak_depth = 0;
        latched = false;
    }
    return r;
}
