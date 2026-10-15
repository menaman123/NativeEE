#include "engine/ee/timing/budget_clock.h"

#include "engine/ee/cpu/ps2_float.h"

int32_t BudgetClock::ThreshCounts(float f12, uint32_t rate) {
    // rate is loaded with lw (signed word): cvt.s.w sees the SIGNED value.
    const float frate = static_cast<float>(static_cast<int32_t>(rate));
    const float prod = ps2_float_clamp(ps2_float_clamp(f12) * ps2_float_clamp(frate));
    if (prod >= 2147483648.0f) return 0x7FFFFFFF;
    if (prod < -2147483648.0f) return static_cast<int32_t>(0x80000000u);
    return static_cast<int32_t>(prod);
}

uint64_t BudgetClock::PeriodCounts(uint32_t rate, uint32_t fps) {
    const uint32_t lo = fps ? (rate / fps) : 0xFFFFFFFFu;
    return static_cast<uint64_t>(lo);  // dsll32 + dsrl32: ZERO-extend
}

uint64_t BudgetClock::FoldLiteral(uint64_t elapsed, uint64_t period, uint64_t phase, uint32_t cap) {
    const uint64_t lim = period + phase;  // mod 2^64
    if (lim < elapsed) {
        elapsed -= period;
        for (uint32_t i = 0; i < cap && lim < elapsed; ++i) elapsed -= period;
    }
    return elapsed;
}

// While lim >= period every pre-exit iterate stays > lim >= period, so no
// intermediate subtraction wraps and the minimal k >= 1 with
// elapsed - k*period <= lim is k = (diff-1)/period + 1, diff = elapsed - lim.
// Off that domain (period 0 would never terminate in the guest; lim < period
// needs a 64-bit wrap of period+phase) mirror the loop under a hard bound.
uint64_t BudgetClock::FoldClosed(uint64_t elapsed, uint64_t period, uint64_t phase) {
    const uint64_t lim = period + phase;
    if (lim < elapsed) {
        if (period != 0 && lim >= period) {
            const uint64_t diff = elapsed - lim;  // >= 1
            const uint64_t k = (diff - 1) / period + 1;
            elapsed -= k * period;  // mod 2^64
        } else {
            elapsed -= period;
            for (uint32_t i = 0; i < 4096u && lim < elapsed; ++i) elapsed -= period;
        }
    }
    return elapsed;
}

bool BudgetClock::Headroom(float f12, uint32_t rate, uint32_t fps, uint64_t now, uint64_t anchor,
                           uint64_t phase) {
    // thresh is a sign-extended s32 compared as u64 (sltu).
    const uint64_t thresh = static_cast<uint64_t>(static_cast<int64_t>(ThreshCounts(f12, rate)));
    const uint64_t period = PeriodCounts(rate, fps);
    const uint64_t elapsed = FoldClosed(now - anchor, period, phase);
    const uint64_t remaining = period - elapsed;  // u64, wrap kept
    return thresh < remaining;
}

uint64_t BudgetClock::Query(uint64_t anchor, uint32_t base_eff, uint32_t step) {
    if (anchor != last_anchor_) {
        last_anchor_ = anchor;
        reads_this_frame_ = 0;
    }
    ++reads_this_frame_;
    return anchor + base_eff + static_cast<uint64_t>(reads_this_frame_) * step;
}

void BudgetClock::Reset() {
    last_anchor_ = ~0ull;
    reads_this_frame_ = 0;
}
