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
// The HOLD (round 5, engine.mtvu_depth_hold): a second, independent arm read
// from the WORKER's progress, not the presenter's. Its depth is the worker's
// lag in frames (frame marks the producer pushed at its frame syncs minus the
// marks the worker has passed): lag 1 = the worker is inside the frame the EE
// just finished (the double-buffered console's own depth), lag 2 = a COMPLETE
// frame is queued ahead of the worker while it is still in the one before.
// With hold = 1 the producer parks while lag > 1 -- only while the worker has
// at least one whole frame of work queued, so the park never starves it and
// the throughput stays the worker's own; the EE's lead over the worker (the
// frames a button's picture waits in the queue) is bounded at one frame. The
// first design (2026-10-23 a.m.) read the hold off declared - presented: that
// serialised the EE behind the worker AND the consumer (a three-stage pipeline
// at depth one) and cost GL 28 vs 49 pictures/s under load; the lift / re-arm
// rules that tried to patch it are gone. Unlike the cap, the hold needs no
// lift: when the worker is the bound the EE runs at its pace one frame ahead;
// when the EE is the bound lag never passes 1 and the hold never parks. The
// same 100 ms valve releases a hold park whose lag never moves. 0 = no hold.
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
    // The cap in force: the setting, lifted 2 -> 3 while low_band() holds.
    uint64_t EffMaxFrames() const;
    // declared - presented, clamped at 0 (a declare/present interleave can
    // transiently order presented > declared).
    static uint64_t DepthFrames(uint64_t declared, uint64_t presented);
    // The park condition: the cap is armed and the depth exceeds it.
    bool Over(uint64_t declared, uint64_t presented) const;
    // The frame arm of an engagement opened at `engaged_at_ns`: parks while
    // Over(); once kFrameValveNs elapsed the valve releases it instead.
    RunaheadFramePark FramePark(uint64_t declared, uint64_t presented, uint64_t engaged_at_ns) const;

    // The hold (0 = none); producer thread, before the worker runs.
    void SetHold(uint64_t hold_frames) { hold_frames_ = hold_frames; }
    uint64_t HoldFrames() const { return hold_frames_; }
    // The hold's park condition: armed and the worker's lag (marks pushed - marks
    // passed) exceeds it -- more than `hold` frames queued ahead of the worker.
    bool HoldOver(uint64_t worker_lag_frames) const;
    // The hold arm of an engagement opened at `engaged_at_ns`: parks while
    // HoldOver(); once kFrameValveNs elapsed the valve releases it instead.
    RunaheadFramePark HoldPark(uint64_t worker_lag_frames, uint64_t engaged_at_ns) const;

    uint64_t NowNs() const;

private:
    uint64_t depth_frames_;
    std::function<bool()> low_band_;
    const MtvuClock& clock_;
    uint64_t hold_frames_ = 0;
};
