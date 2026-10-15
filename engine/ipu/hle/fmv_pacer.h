// engine/ipu/hle/fmv_pacer.h -- FmvPacer: delivers one picture per authored
// display period on the vblank tick domain. On hardware sceMpegGetPicture returns
// pictures as the IPU decodes them at the disc/decoder rate and the driver shows
// one per round(60 / authored fps) vblanks; the HLE decodes at host speed, so the
// pacer books each serve against an absolute tick grid and tells the caller how
// many ticks to wait. Pure int64 arithmetic: the caller reads the tick counter
// and performs the wait (no clock, no thread here).
#pragma once

#include <cstdint>

namespace ps2r::ipu {

class FmvPacer {
public:
    struct Wait {
        int64_t wait_ticks = 0;  // ticks the caller waits before handing over the picture
        bool late = false;       // the deadline had already passed on arrival
    };
    struct State {
        bool armed = false;
        int64_t ticks_to_next = 0; // relative to capture, may be late/negative
        int64_t period = 0, reanchor_periods = 0;
        uint64_t reanchors = 0;
    };

    // The rate assumed when the sequence header carries none (or the decoder
    // has not reported one): NTSC film-to-video 29.97 pictures per second.
    static constexpr double kFallbackFps = 29.97;

    // Picture period in ticks; <= 0 disables pacing (Next returns wait 0).
    void SetPeriod(int64_t ticks);
    // Period from the authored frame rate: round(60 / fps), at least 1 tick
    // (2 at the NTSC 29.97 rate). A rate <= 0 (unknown) uses kFallbackFps.
    void SetAuthoredRateTicks(double fps);
    int64_t Period() const { return ticks_per_pic_; }

    // Books one delivery at `now_tick`. The first delivery arms the grid at
    // now + period and is served without waiting. A late arrival (deadline
    // already passed) is served instantly and flagged; the grid is NOT moved:
    // pictures the disc already delivered sit in the bitstream ring and the
    // IPU decodes them at memory speed, so a backlog drains with instant serves
    // until the grid catches up with `now`. Every call advances the deadline by
    // one period.
    //
    // Bounded catch-up (opt-in; 0 = off, the reference contract above): a grid
    // `periods` or more periods behind `now` has no hardware analogue (the disc
    // never delivers that far ahead of the display) -- it means the HOST fell
    // behind real time. Such a serve is still instant and flagged late, but the
    // grid re-anchors at `now`, so the next pictures pace at the authored rate
    // again (slow motion: every picture shown for its period) instead of
    // draining hundreds of pictures as a blur. The host enables it in play mode
    // only; deterministic legs keep the reference contract.
    void SetReanchorPeriods(int64_t periods) { reanchor_periods_ = periods > 0 ? periods : 0; }
    int64_t ReanchorPeriods() const { return reanchor_periods_; }
    uint64_t Reanchors() const { return reanchors_; }
    Wait Next(int64_t now_tick);

    // Disarms (the next delivery re-arms on its own origin); the period is kept.
    void Reset();
    bool Armed() const { return ticks_per_pic_ > 0 && have_deadline_; }
    int64_t NextTick() const { return next_tick_; }
    // Pure software clock migration. Preserve deadline distance across a
    // different domain origin; an armed restored deadline may equal zero.
    // Invalid/overflowing state leaves the live clock and output unchanged.
    bool Capture(int64_t now_tick, State& out) const;
    static bool Validate(const State&, int64_t now_tick);
    bool Restore(const State&, int64_t now_tick);

    // The libmpeg driver primes its pipeline with two GetPicture calls before
    // the first display; those serves are instant.
    static bool ShouldPace(uint32_t frames_served) { return frames_served >= 2; }

private:
    int64_t next_tick_ = 0;      // absolute deadline; presence is explicit for restore at tick zero
    int64_t ticks_per_pic_ = 0;  // <= 0 disables pacing
    int64_t reanchor_periods_ = 0;  // 0 = reference contract (never re-anchor)
    uint64_t reanchors_ = 0;        // bounded catch-up re-anchors (host behind real time)
    bool have_deadline_ = false;
};

}  // namespace ps2r::ipu
