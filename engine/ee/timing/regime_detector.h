#pragma once
// RegimeDetector: decides how many NTSC fields one guest frame spans (the
// REGIME, 1..5) from SUSTAINED evidence across boundaries, never per-frame
// jitter. The emulated time domain advances exactly REGIME ticks per frame
// boundary, so the game reads the same per-frame delta a PS2 running at that
// sustained rate would produce (60 -> 30 -> 20 Hz field-locked slip).
//
// Inputs per boundary (both in RAW ticks of the tick source):
//   arrival_gap -- ticks elapsed when the EE reached the tick wait (its work
//                  for the frame was done);
//   final_gap   -- ticks elapsed when the boundary settled.
// UP evidence:   final_gap > n           (the frame needed more than n fields)
// DOWN evidence: arrival_gap + 1 < n     (the work fit inside n-1 ticks: the EE
//                arrived having crossed at most n-2 tick edges). arrival_gap
//                == n-1 only proves work < n ticks -- true for ANY loop at
//                quantum n -- and must not count, or a steady 30 fps loop at
//                n=2 flaps 1<->2.
// Hysteresis: the first window after Seed() is short (15 boundaries: cold
// start attack) with stricter fractions (up > 0.50, down > 0.95); every later
// window is 120 boundaries with up > 0.30, down > 0.90. Strict '>' throughout.
#include <cstdint>

class RegimeDetector {
public:
    static constexpr int kMinN = 1;
    static constexpr int kMaxN = 5;
    static constexpr uint32_t kFullWin = 120;
    static constexpr uint32_t kFastWin = 15;
    static constexpr double kUpFrac = 0.30;
    static constexpr double kDownFrac = 0.90;
    static constexpr double kUpFracFast = 0.50;
    static constexpr double kDownFracFast = 0.95;

    void Seed();
    // Returns true when the regime CHANGED at this boundary.
    bool OnBoundary(uint64_t arrival_gap, uint64_t final_gap);

    int n() const { return n_; }
    uint32_t samples() const { return samples_; }
    bool fast_window() const { return fast_; }
    struct Snapshot {
        int32_t n = 1;
        uint32_t samples = 0, over = 0, under = 0;
        bool fast = true;
    };
    Snapshot Capture() const;
    static bool Validate(const Snapshot&);
    bool Restore(const Snapshot&);

private:
    int n_ = 1;
    uint32_t samples_ = 0;
    uint32_t over_ = 0;   // final_gap > n
    uint32_t under_ = 0;  // arrival_gap + 1 < n
    bool fast_ = true;
};
