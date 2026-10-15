#pragma once
// TickSource: the ONE frame/field tick generator. Every raw tick that reaches
// the EmuDomain comes from here, and nothing here carries a wall-clock value:
// the generator publishes a COUNT and a PERIOD POLICY; the host pacer (play)
// or the boundary (deterministic) decides WHEN to call Tick().
//
// Two policies (Config time.tick_sync / time.tick_deterministic):
//  * Deterministic -- no thread. Ticks are demanded by the EE at boundary
//    step 2 (BoundarySequencer / TickSync in 3D2): one call to Tick() per
//    demanded field. Wall time cannot influence the count.
//  * Play -- the host pacer sleeps PeriodUs() between ticks and NOTES a tick
//    (TickSync::NotePending); the guest fires it at its next fixed point --
//    the wall-locked boundary (BoundarySequencer::BindPacer: the boundary
//    parks for the pacer's tick instead of firing its own) or a parked kernel
//    timer wait. The pacer's clock never crosses this interface: only the
//    resulting count does; with a pacer bound every noted tick is one game
//    field (EmuDomain pacer release) -- the one a boundary or timer waiter
//    WAITED for and the ones a late frame found noted alike (the clock
//    contract: the title's T0 sees the elapsed wall fields).
//
// Modes shape the period + release policy (turbo.h / warp.h rules):
//   Realtime  period = 16667 us (one 60 Hz field, 1/60 s rounded to the us;
//             the emulated console is a perfect 60 Hz PS2, not 59.94);
//   Turbo(K)  period = 16667 / K us, K in [2, 64]: the pacer fires K fields
//             per wall field; the EmuDomain quantum still governs how many
//             of them the game sees per boundary;
//   Freerun   release at once, no period (consumption-driven ticks); this is
//             also the engaged form of load turbo (TurboGovernor);
//   Warp      release at once until the target frame (harness.warp_to_frame).
// Bypass rule (OLD ps2_vblank.cpp BypassNow = freerun || warp || load-turbo
// freerun form): ONLY the release-at-once modes make the EmuDomain ride raw
// ticks 1:1. Finite-K turbo is NOT a bypass -- its extra raw ticks stay under
// the quantum, so the game still observes REGIME fields per boundary and
// only the host pace changes.
#include <cstdint>

class TickSource {
public:
    enum class Policy { Deterministic, Play };
    enum class Mode { Realtime, Freerun, Turbo, Warp };

    static constexpr uint32_t kBasePeriodUs = 16667;
    static constexpr uint32_t kTurboKMin = 2;
    static constexpr uint32_t kTurboKMax = 64;
    static uint32_t PeriodUsForK(uint32_t k);

    void SetPolicy(Policy p) { policy_ = p; }
    Policy policy() const { return policy_; }
    void SetMode(Mode m, uint32_t k = 1);
    Mode mode() const { return mode_; }
    uint32_t K() const { return k_; }

    // One field tick fired; returns the count AFTER the tick.
    uint64_t Tick();
    uint64_t RawCount() const { return raw_count_; }

    // Period the play pacer sleeps before the next Tick(); 0 = release at once.
    uint32_t PeriodUs() const;
    // The EmuDomain rides raw ticks 1:1 while this is set: release-at-once
    // modes only (Freerun, Warp). Finite-K Turbo stays under the quantum.
    bool Bypass() const { return mode_ == Mode::Freerun || mode_ == Mode::Warp; }

    void ResetForRestore(uint64_t raw_count) { raw_count_ = raw_count; }

private:
    Policy policy_ = Policy::Deterministic;
    Mode mode_ = Mode::Realtime;
    uint32_t k_ = 1;
    uint64_t raw_count_ = 0;
};
