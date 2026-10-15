// engine/ipu/hle/fmv_pacer.cpp -- FmvPacer tick-grid arithmetic (see fmv_pacer.h).
#include "engine/ipu/hle/fmv_pacer.h"

#include <limits>

namespace ps2r::ipu {

void FmvPacer::SetPeriod(int64_t ticks) { ticks_per_pic_ = ticks; }

void FmvPacer::SetAuthoredRateTicks(double fps) {
    if (!(fps > 0.0)) fps = kFallbackFps;   // header lacked a rate: NTSC
    int64_t tpp = static_cast<int64_t>(60.0 / fps + 0.5);
    if (tpp < 1) tpp = 1;
    ticks_per_pic_ = tpp;
}

FmvPacer::Wait FmvPacer::Next(int64_t now_tick) {
    Wait r;
    if (ticks_per_pic_ <= 0) return r;          // pacing disabled
    if (!have_deadline_) {                     // first delivery arms the grid
        next_tick_ = now_tick + ticks_per_pic_;
        have_deadline_ = true;
        return r;                               // no wait: play starts promptly
    }
    int64_t wait = next_tick_ - now_tick;
    if (wait < 0) {
        r.late = true;                          // catch-up serve: instant, grid unchanged ...
        wait = 0;
        if (reanchor_periods_ > 0 && now_tick - next_tick_ >= reanchor_periods_ * ticks_per_pic_) {
            next_tick_ = now_tick;              // ... unless the host fell behind: re-anchor
            ++reanchors_;
        }
    }
    next_tick_ += ticks_per_pic_;
    r.wait_ticks = wait;
    return r;
}

void FmvPacer::Reset() { next_tick_ = 0; have_deadline_ = false; }

bool FmvPacer::Capture(int64_t now_tick, State& out) const {
    if (now_tick < 0 || (have_deadline_ &&
        next_tick_ < std::numeric_limits<int64_t>::min() + now_tick)) return false;
    State state{have_deadline_, have_deadline_ ? next_tick_ - now_tick : 0,
                ticks_per_pic_, reanchor_periods_, reanchors_};
    if (!Validate(state, now_tick)) return false;
    out = state;
    return true;
}

bool FmvPacer::Validate(const State& state, int64_t now_tick) {
    constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
    if (now_tick < 0 || state.reanchor_periods < 0 ||
        (!state.armed && state.ticks_to_next != 0) ||
        (state.period > 0 && state.reanchor_periods > maximum / state.period)) return false;
    if (!state.armed) return state.period <= 0 || now_tick <= maximum - state.period;
    if (state.ticks_to_next > maximum - now_tick) return false;
    const int64_t deadline = now_tick + state.ticks_to_next;
    if (state.period > 0 && deadline > maximum - state.period) return false;
    // Next computes now - deadline for late arrivals. Its nonnegative debt
    // must remain representable even when the restored origin is near zero.
    return state.ticks_to_next != std::numeric_limits<int64_t>::min();
}

bool FmvPacer::Restore(const State& state, int64_t now_tick) {
    if (!Validate(state, now_tick)) return false;
    next_tick_ = state.armed ? now_tick + state.ticks_to_next : 0;
    ticks_per_pic_ = state.period;
    reanchor_periods_ = state.reanchor_periods;
    reanchors_ = state.reanchors;
    have_deadline_ = state.armed;
    return true;
}

}  // namespace ps2r::ipu
