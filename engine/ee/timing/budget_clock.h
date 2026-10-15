#pragma once
// BudgetClock: the frame-budget headroom predicate the game's per-frame
// maintenance slice asks ("is there still f12 seconds of this field left?"),
// with the guest function's arithmetic reproduced exactly, evaluated in the
// emu domain. MECHANISM ONLY: the object-field offsets, the anchor value and
// the tuning constants (base, base_low, step, lowdraw) are title data
// (TitleProfile.quirks.budget) passed in by the caller.
//
// Guest arithmetic (registers renamed):
//   rate    = lw [obj+rate]          // 576000 = BUSCLK/256 per second
//   fps     = lw [obj+fps]           // 60
//   thresh  = cvt.w.s(ps2_mul(f12, cvt.s.w(rate)))   saturating trunc-to-zero
//   period  = zext32(divu rate / fps)                 (9600 counts)
//   elapsed = now - anchor           // u64 dsubu, wrap kept
//   if (period + phase < elapsed) { elapsed -= period;
//       while (period + phase < elapsed) elapsed -= period; }
//   remaining = period - elapsed     // u64: the (period, period+phase] sliver
//                                    // wraps huge = headroom TRUE (the game's
//                                    // own 78-count boundary guard)
//   return (u64)thresh < remaining;  // sltu
//
// The synthetic emu clock (mode 1): now = anchor + base_eff + reads x step,
// where reads counts the queries since the anchor last changed (the frame
// top). Every frame's pump therefore sees the same deterministic grant
// schedule; base_eff comes from the PhaseClassifier (full vs low-draw frame).
#include <cstdint>

class BudgetClock {
public:
    // thresh = cvt.w.s(ps2_mul(f12, cvt.s.w(rate))) with the PS2 single
    // multiply (operand + result clamp) and the EE's saturating truncation.
    static int32_t ThreshCounts(float f12, uint32_t rate);
    // period = zext32(divu rate / fps); divu by 0 leaves LO = -1 -> 0xFFFFFFFF.
    static uint64_t PeriodCounts(uint32_t rate, uint32_t fps);
    // The guest's literal fold loop, bounded by `cap` inner iterations.
    static uint64_t FoldLiteral(uint64_t elapsed, uint64_t period, uint64_t phase, uint32_t cap = (1u << 20));
    // Closed form, equal to FoldLiteral wherever the guest can reach.
    static uint64_t FoldClosed(uint64_t elapsed, uint64_t period, uint64_t phase);
    // The full predicate (the guest function's v0).
    static bool Headroom(float f12, uint32_t rate, uint32_t fps, uint64_t now, uint64_t anchor, uint64_t phase);

    // Synthetic per-frame clock: call at each query with the game's current
    // anchor and the frame's effective base; returns `now` for Headroom().
    uint64_t Query(uint64_t anchor, uint32_t base_eff, uint32_t step);
    bool AnchorChanged(uint64_t anchor) const { return anchor != last_anchor_; }
    uint32_t ReadsThisFrame() const { return reads_this_frame_; }
    void Reset();

private:
    uint64_t last_anchor_ = ~0ull;
    uint32_t reads_this_frame_ = 0;
};
