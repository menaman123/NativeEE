#pragma once
// The runahead bound. Real hardware has no runahead: the EE's VIF1 DMA stalls
// when the 64-quadword VIF1 FIFO is full until VU1 consumes. The host
// equivalent bounds the producer in DISPLAY FRAMES: depth = frames DECLARED
// (boundary enqueues) - frames PRESENTED (genuine VSync presents; injected
// CRTC presents advance neither counter); the producer parks while depth
// exceeds the cap. A cap of exactly 2 lifts to 3 while the present pacer
// reports its lowest cadence band (the injected `low_band`); 0 disables the
// frame park. The 100 ms valve releases a frame park whose depth never moves
// (a frozen consumer must not hang the producer).
//
// The HOLD cap (round 5, engine.mtvu_depth_hold): the cap in force while the
// worker keeps up is `hold` (1: the EE may lead the worker by one frame, the
// double-buffered console's own depth), so a button's picture is not two
// frames behind the EE that drew it; a frame park on the hold cap that lasts
// longer than kHoldLiftNs says the worker is NOT keeping up and lifts the cap
// to the setting (2, 3 under low band) for kHoldLiftBoundaries declared frames
// -- the catch-up allowance the round-4 chain had -- after which the hold
// returns. 0 = no hold (the setting is the cap, the round-4 behaviour).
//
// Tripwire: a pure detector fed once per genuine present by the graphics
// thread; `over` means depth > cap + 1 (the +1 absorbs the legal transients:
// declare/present interleave, the pacer slot, the valve fall-through); an
// episode alarms after more than kPersistPresents consecutive over-presents,
// once per episode, at most kMaxReports per run; recovery re-arms.
#include <cstdint>
#include <functional>

#include "engine/vu/tiers/mtvu_clock.h"

// The frame arm of one backpressure engagement: park, or the valve released it.
struct RunaheadFramePark {
    bool park = false;
    bool valve_hit = false;
};

class RunaheadGuard {
public:
    static constexpr uint64_t kLiftBaseDepth = 2;
    static constexpr uint64_t kLiftedDepth = 3;
    static constexpr uint64_t kFrameValveNs = 100'000'000;
    static constexpr uint64_t kHoldLiftNs = 4'000'000;        // a hold park longer than this lifts the cap
    static constexpr uint64_t kHoldLiftBoundaries = 120;      // ... for this many declared frames (2 s at 60)

    struct Tripwire {
        static constexpr uint32_t kPersistPresents = 30;  // > 30 over-presents (~1 s at 30 fps) before an alarm
        static constexpr uint32_t kMaxReports = 5;        // reported episodes per run
        uint32_t over_run = 0;
        uint64_t peak_depth = 0;
        uint32_t reports = 0;
        bool latched = false;

        struct Result {
            bool fire = false;
            bool over = false;
            uint64_t peak = 0;
            uint32_t run = 0;
        };
        Result Feed(uint64_t depth_frames, uint64_t cap_frames);
    };

    RunaheadGuard(int depth_frames, std::function<bool()> low_band = {},
                  const MtvuClock& clock = MtvuClock::Steady());

    // The configured cap (negative clamps to 0).
    uint64_t DepthFramesSetting() const { return depth_frames_; }
    // The hold cap (0 = none); producer thread, before the worker runs.
    void SetHold(uint64_t hold_frames) { hold_frames_ = hold_frames; }
    uint64_t HoldFrames() const { return hold_frames_; }
    // The cap in force: the setting, lifted 2 -> 3 while low_band() holds; the hold
    // cap (when set and below it) unless a hold park lifted it (`declared` is the
    // producer's frame clock; HoldLifted(declared) reads the lift).
    uint64_t EffMaxFrames() const;
    uint64_t EffMaxFrames(uint64_t declared) const;
    bool HoldLifted(uint64_t declared) const { return hold_frames_ != 0 && declared < hold_lift_until_; }
    // Producer thread, from a frame park opened at `engaged_at_ns` under the hold cap:
    // true (once per lift) when the park has lasted kHoldLiftNs -- the cap is lifted to
    // the setting for kHoldLiftBoundaries frames from `declared`.
    bool NoteHoldPark(uint64_t declared, uint64_t engaged_at_ns);
    uint64_t HoldLifts() const { return hold_lifts_; }
    // declared - presented, clamped at 0 (a declare/present interleave can
    // transiently order presented > declared).
    static uint64_t DepthFrames(uint64_t declared, uint64_t presented);
    // The park condition: the cap is armed and the depth exceeds it.
    bool Over(uint64_t declared, uint64_t presented) const;
    // The frame arm of an engagement opened at `engaged_at_ns`: parks while
    // Over(); once kFrameValveNs elapsed the valve releases it instead.
    RunaheadFramePark FramePark(uint64_t declared, uint64_t presented, uint64_t engaged_at_ns) const;
    uint64_t NowNs() const;

private:
    uint64_t depth_frames_;
    std::function<bool()> low_band_;
    const MtvuClock& clock_;
    uint64_t hold_frames_ = 0;
    uint64_t hold_lift_until_ = 0;   // declared frame the lift ends at (producer thread)
    uint64_t hold_lifts_ = 0;
};
