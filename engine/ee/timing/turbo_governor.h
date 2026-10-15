#pragma once
// TurboGovernor: decides the TickSource mode (Realtime / Freerun / Turbo K)
// for PLAY. It is a host pacing policy: its output only shapes the period the
// pacer sleeps and the bypass flag; no value here is a clock.
//
// Manual scalar (Config engine.turbo): 0 = unset (realtime, K=1); 1 = freerun
// (ticks released as fast as the EE consumes them); 2..64 = fixed K.
// Load turbo (Config cdvd.load_turbo, SETTING, default OFF): while the guest is
// in a load phase -- disc/stream activity noted continuously for kEngageMs
// with gaps below kGapMs, and no player input for kInputQuietMs -- the ticker
// runs in its FREERUN form (consumption-driven ticks: the winning arm of OLD
// turbo.h AutoK == 0; finite K floods the EE with K x the per-tick ISR duties
// and pins the load loop at 60, so it was never the shipped load form); it
// releases kReleaseMs after activity stops or on input.
// Under the deterministic profile the governor is never consulted.
#include <cstdint>

#include "engine/ee/timing/tick_source.h"

class TurboGovernor {
public:
    static constexpr uint32_t kEngageMs = 1500;
    static constexpr uint32_t kGapMs = 150;
    static constexpr uint32_t kInputQuietMs = 2000;
    static constexpr uint32_t kReleaseMs = 250;

    // engine.turbo: 0 realtime, 1 freerun, 2..64 K.
    void SetManual(uint32_t turbo_setting);
    void SetLoadTurbo(bool enabled) { load_turbo_ = enabled; }

    // Host pacer inputs (milliseconds on the pacer's own clock; never a value
    // any clock derives from).
    void NoteLoadActivity(uint64_t now_ms);
    void NoteInput(uint64_t now_ms);
    void Update(uint64_t now_ms);

    TickSource::Mode Mode() const;
    uint32_t K() const;
    bool LoadTurboEngaged() const { return engaged_; }
    void Apply(TickSource& source) const;

private:
    uint32_t manual_ = 0;
    bool load_turbo_ = false;
    bool engaged_ = false;
    uint64_t activity_start_ms_ = 0;
    uint64_t last_activity_ms_ = 0;
    uint64_t last_input_ms_ = 0;
    bool have_activity_ = false;
};
