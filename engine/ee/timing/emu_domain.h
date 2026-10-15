#pragma once
// EmuDomain: THE emulated time domain -- the one monotonic tick counter every
// game-observable EE clock derives from (DerivedClocks). Its inputs are the
// RAW tick sequence of the TickSource and the frame-boundary sequence; NO wall
// value enters: the domain is a pure function of (raw ticks, arrivals,
// boundaries, re-anchors). Under the deterministic profile raw ticks are
// themselves demanded at boundaries, so the whole clock tree is a function of
// the guest's own execution.
//
// Rules (PS2 field-locked frame model):
//  * quantum: a frame is granted exactly REGIME emu ticks per boundary -- the
//    hardware amount for a REGIME-field NTSC frame -- however many raw ticks
//    the host burned. Carry: while a frame is in flight each raw tick also
//    fires one emu tick until the quantum is exhausted; the boundary tops the
//    counter up to base + REGIME (never regressing it).
//  * lead governor: emu time may lead raw time by at most kLeadSlackTicks (1);
//    lag (host stalls absorbed) accumulates freely and is NEVER repaid as a
//    burst -- the timers are relative clocks, nothing game-visible compares
//    them to the raw generator.
//  * idle release: kIdleReleaseTicks (6 = 100 ms at 60 Hz) raw ticks without a
//    boundary means the guest left its SyncV loop (load / FMV): the counter
//    rides raw ticks 1:1 (no jump in either direction). The next boundary
//    re-anchors and reseeds the detector.
//  * bypass (freerun / warp / load-turbo): same 1:1 ride, the caller decides.
//  * pacer release (Play policy, a host pacer bound -- the clock contract):
//    a tick a guest waiter fires (TickSync Deferred) is the pacer's wall
//    vblank and rides 1:1 -- one field per wall vblank, whether the guest
//    WAITED for it (a pace park of N ticks costs N fields) or found it noted
//    after a LATE frame (the elapsed fields reach the title's T0, so its step
//    grows: rule 3). The boundary settle does no quantum top-up under the
//    release (rule 1: never a field the wall did not produce). Off under the
//    deterministic policy (OnDeferredTick == OnRawTick; the quantum rules
//    above are unchanged).
//  * ReAnchor (boot / savestate restore): reseeds the quantum bookkeeping and
//    the detector; the counter stays continuous (timer baselines are rebased
//    by their owner at the same instant).
//  * regime pin: Config engine.time_regime_pin >= 1 fixes the quantum (the
//    deterministic profile pins 1: the game observes a perfect 60 Hz PS2).
#include <cstdint>

#include "engine/ee/timing/regime_detector.h"

class EmuDomain {
public:
    static constexpr int64_t kLeadSlackTicks = 1;
    static constexpr uint64_t kIdleReleaseTicks = 6;  // 100 ms of 60 Hz ticks

    // Pure settle arithmetic: the emu advance for a frame given its quantum,
    // the emu ticks already carried (cur - base), the raw ticks it spanned and
    // the lead before it. adv >= carried, adv <= max(quantum, carried),
    // lead_after = lead_before + adv - final_gap <= kLeadSlackTicks.
    static uint64_t BoundaryAdvance(uint64_t quantum, uint64_t carried, uint64_t final_gap,
                                    int64_t lead_before, int64_t* lead_after);

    // -1 = adaptive detector; 1..5 = pinned quantum.
    void SetRegimePin(int n);
    int RegimePin() const { return pin_; }
    // Freerun / warp / load-turbo: the counter rides raw ticks 1:1 while set.
    void SetBypass(bool on) { bypass_ = on; }
    bool Bypass() const { return bypass_; }

    // One raw tick fired (raw_count = the generator's count AFTER the tick).
    void OnRawTick(uint64_t raw_count);
    // A tick a guest WAITER fired (TickSync Deferred: a pacer-noted tick fired
    // at a kernel timer wait or as the wall-locked boundary's field). With the
    // pacer release on (SetPacerRelease: the Play policy with a host pacer
    // bound) the tick is the idle-release form -- the counter rides it 1:1, no
    // quantum absorption -- so a pace park of N pacer ticks costs the game N
    // fields, as on the console; with the release off it is OnRawTick. Under the
    // wall-locked boundary every noted tick -- a late frame's second field
    // included -- arrives here 1:1 (BoundarySequencer::FirePending); nothing is
    // routed to OnRawTick in that regime.
    void OnDeferredTick(uint64_t raw_count);
    void SetPacerRelease(bool on) { pacer_release_ = on; }
    bool PacerRelease() const { return pacer_release_; }
    // Raw ticks OnRawTick absorbed under the exhausted quantum (diagnostic).
    uint64_t Absorbed() const { return absorbed_; }
    // The EE reached the tick wait at raw_count (the detector's DOWN input).
    void NoteArrival(uint64_t raw_count);
    // The frame boundary settled at raw_count.
    void OnBoundary(uint64_t raw_count);
    // Boot / restore: reseed bookkeeping; counter continuous.
    void ReAnchor(uint64_t raw_count);

    // The ONE game-visible tick value.
    uint64_t DomainTick() const { return emu_count_; }
    uint32_t Regime() const { return regime_; }
    int64_t Lead() const { return lead_; }
    uint64_t BoundaryCount() const { return boundaries_; }
    uint64_t LastBoundaryTick() const { return emu_base_; }
    bool InLoop(uint64_t raw_count) const;
    const RegimeDetector& Detector() const { return det_; }
    // Existing logical clock state, captured by the game owner. Restoring
    // preserves the settled tick and adaptive window; ReAnchor alone does not.
    struct Snapshot {
        uint64_t tick = 0, base = 0, boundary_raw = 0, arrival_raw = 0, boundaries = 0;
        int64_t lead = 0;
        uint32_t regime = 1;
        int32_t pin = -1;
        bool have_boundary = false, bypass = false, pacer_release = false;
        RegimeDetector::Snapshot detector;
    };
    Snapshot Capture() const;
    static bool Validate(const Snapshot&, uint64_t raw_tick);
    bool Restore(const Snapshot&, uint64_t raw_tick);

private:
    void Reseed(uint64_t raw_count, bool in_loop);

    uint64_t emu_count_ = 0;       // the game-visible tick
    uint64_t emu_base_ = 0;        // emu count at the last boundary
    uint64_t boundary_raw_ = 0;    // raw count at the last boundary
    uint64_t arrival_raw_ = 0;     // raw count at tick-wait entry
    uint64_t boundaries_ = 0;      // boundaries seen since ReAnchor
    bool have_boundary_ = false;   // a boundary has settled since ReAnchor
    int64_t lead_ = 0;
    uint32_t regime_ = 1;
    int pin_ = -1;
    bool bypass_ = false;
    bool pacer_release_ = false;
    uint64_t absorbed_ = 0;
    RegimeDetector det_;
};
