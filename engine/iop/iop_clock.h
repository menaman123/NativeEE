// engine/iop/iop_clock.h -- the ONE time source of the cooperative IOP kernel
// (DelayThread deadlines, deferred-completion dues, periodic timer beats).
// Two policies (Config iop.dispatch):
//   Frame    -- a virtual microsecond counter derived from EE frame boundaries
//               (kFrameUs per delivered FIELD -- one per boundary under the
//               deterministic profile, N for a late play-mode boundary that
//               delivered N fields: IopModuleTable::PumpAtBoundary(fields))
//               plus the explicit forced advances the dispatcher issues when
//               every runnable thread is time-parked. Guest-visible IOP
//               scheduling becomes a pure function of the EE frame stream: the
//               deterministic profile's proof path.
//   Realtime -- microseconds on steady_clock (the play policy; documented here,
//               never used by the deterministic profile, never hash-gated).
// GetSystemTime/USec2SysClock do NOT route here: their per-call counters are
// guest-visible and live in the thbase module.
#pragma once

#include <chrono>
#include <cstdint>

namespace ps2r::iop {

class IopClock {
public:
    enum class Policy { Realtime, Frame };

    static constexpr uint64_t kFrameUs = 16667;   // one NTSC frame quantum

    void   SetPolicy(Policy p) { policy_ = p; }
    Policy GetPolicy() const { return policy_; }

    // Now, in the kernel's scheduling domain (microseconds).
    uint64_t NowUs() const;

    // Frame: an EE frame boundary arrived -- the virtual base advances by one
    // frame quantum (never backwards past a forced advance). No-op otherwise.
    void OnFrameBoundary();
    // Frame: forced advance to an absolute deadline (all threads time-parked:
    // jump to the earliest due). Never moves time backwards. No-op otherwise.
    // With the frame base following (SetFrameBaseFollowsSkips, the default), an
    // advance beyond what the NEXT boundary can absorb (past (frames+2)*kFrameUs)
    // carries the frame base to the frame before the one containing the new
    // time: the next boundary lands in that frame and runs its remainder, the
    // one after runs a full quantum -- no boundary pump is ever a no-op. An
    // overshoot within the next frame is left to that boundary (its shortened
    // quantum keeps IOP time continuous), as before. Without following (the
    // pre-FIX-2 clock) the base stays behind and every boundary pump is a no-op
    // until the boundary count catches up: a 1 s skip = 65 dark boundaries while
    // the SPU plays on (the 989snd bank load that killed the hub music).
    void AdvanceToUs(uint64_t t);
    void SetFrameBaseFollowsSkips(bool on) { follow_skips_ = on; }
    bool FrameBaseFollowsSkips() const { return follow_skips_; }
    // Census: forced advances beyond the next boundary's ceiling, the frames
    // they skipped (base moves, or -- not following -- the dark boundaries they
    // cost), and the longest single skip past that ceiling in microseconds.
    uint64_t Skips() const { return skips_; }
    uint64_t SkippedFrames() const { return skipped_frames_; }
    uint64_t MaxSkipUs() const { return max_skip_us_; }
    // Frame: a savestate restore -- the restored image's vsync frame count IS
    // this clock's frame count: the frame base becomes `frames` and time moves
    // (forward-only, as every advance) to that base, so the NEXT boundary
    // advances to (frames + 1) quanta and a due armed at the restored now is
    // reachable; dues of the pre-load world (below the new base) expire at once.
    // Rebasing only the microsecond base would leave the frame count behind and
    // freeze time until the boundaries caught up (~frames of them). No-op otherwise.
    void RebaseToFrame(uint64_t frames);
    // Frame: the bound for intra-frame forced advances at a boundary pump --
    // one frame quantum past the current frame base (the next boundary's tick).
    uint64_t FrameCeilingUs() const;
    // Frame: how far the virtual clock stands past the current frame base --
    // the IOP's instant inside the frame (0 at the base; at or beyond kFrameUs
    // when the frame's IOP time is consumed and the clock ran ahead).
    uint64_t OffsetInFrameUs() const;
    // Frame: boundaries seen so far.
    uint64_t Frames() const { return frames_; }

    // Realtime helper: the steady time_point of a kernel-domain due.
    std::chrono::steady_clock::time_point DueToSteady(uint64_t due_us) const;

private:
    uint64_t SteadyNowUs() const;

    Policy   policy_ = Policy::Frame;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    uint64_t virtual_us_ = 0;
    uint64_t frames_ = 0;
    bool     follow_skips_ = true;
    uint64_t skips_ = 0, skipped_frames_ = 0, max_skip_us_ = 0;
};

}  // namespace ps2r::iop
