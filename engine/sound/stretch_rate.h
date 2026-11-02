#pragma once
// StretchRate: the ratio a play host feeds TimeStretch for each mixer quantum on its way to the audio
// device's pull ring, from two observations per push: the wall interval since the previous push and the
// ring's fill (frames) before the push, against the ring's standing lead (frames).
//
// The device consumes kSampleRate frames of wall time a second; the mixer produces one quantum per
// boundary. The stream needs ratio = device rate / (quantum x boundaries per wall second): the game's
// long-term boundary RATE. A late field followed by a catch-up (a two-field movie picture, one long
// frame) is jitter the lead absorbs, not a tempo change. So:
//   * rate     the TRIMMED mean push interval over the last kRateWindow pushes (two seconds; the longest
//              and the shortest kRateTrim dropped, so a burst of lost fields is not a rate) x 60; 1 until
//              the window holds kRateMinPushes, and 1 within kRateDeadBand of it (the window's own noise).
//              An interval above kMaxIntervalS is a pause or a stall, not a rate: the window and the fill
//              means restart (the ring padded and re-latched meanwhile).
//   * drift    the fill's one-second mean against the lead, a small gain: the device clock's drift and the
//              slow return of the fill after a disturbance.
//   * deficit  the fill's four-push mean against the lead, a dead band of kDeadBand x lead so the pull
//              and hop quantisation of the fill (+-512 / +-480 frames) never moves the ratio, a gain that
//              bridges a rate step before the lead is gone (loop time constant ~10 pushes: a ramp over
//              100-200 ms, never a jump).
//   * snap     within kSnap of 1 the ratio is exactly 1: TimeStretch then passes the audio through
//              (every frame heard once, continuous).
//   * guard    a push that would leave less than kGuardTargetFrames in the ring (the headroom one
//              two-field field needs: the next late field would pad = repeat) is lengthened to leave
//              that much, up to kGuardMaxRatio, whatever the terms above say; inert while the lead holds.
// Pure arithmetic, no clock, no allocation after construction: the host supplies dt and the fill.
#include <cstdint>
#include <vector>

namespace ps2r::sound {

class StretchRate {
public:
    static constexpr int kQuantumFrames = 800;
    static constexpr double kFieldsPerSecond = 60.0;
    static constexpr int kRateWindow = 120;         // pushes (2 s at 60 Hz)
    static constexpr int kRateMinPushes = 30;       // the rate term stays 1 until the window holds half a second
    static constexpr double kMaxIntervalS = 0.1;    // a longer interval is a pause / a stall: the window restarts
    static constexpr double kRateDeadBand = 0.01;   // |rate - 1| within this is 1: the window's own noise (~0.2 % at 2 s)
    static constexpr double kRateTrim = 0.1;        // the longest and the shortest tenth of the window's intervals are dropped
    static constexpr double kDriftTau = 60.0;       // pushes: the drift term's fill mean (1 s)
    static constexpr double kDriftGain = 0.02;      // ratio correction at a drift fill error of one lead
    static constexpr double kDeficitTau = 4.0;      // pushes: the deficit term's fill mean (67 ms)
    static constexpr double kDeficitGain = 0.3;     // ratio correction at a deficit fill error of one lead
    static constexpr double kDeadBand = 0.15;       // the deficit term ignores fill errors within this fraction of the lead
    static constexpr double kSnap = 0.002;          // |ratio - 1| below this: exactly 1
    static constexpr int kGuardTargetFrames = 1800; // a push must leave this much in the ring (37.5 ms: one two-field field + a pull)
    static constexpr double kGuardMaxRatio = 1.5;   // the guard's ceiling (an empty ring)
    static constexpr double kMinRatio = 0.5, kMaxRatio = 2.0;

    StretchRate();

    // The ratio for the quantum about to be pushed. `dt_s` = seconds since the previous push (< 0: the
    // first push), `fill` = frames in the ring before the push, `lead` = the ring's standing lead (frames).
    double Next(double dt_s, int fill, int lead);

    // Census.
    uint64_t Pushes() const { return pushes_; }
    uint64_t Resets() const { return resets_; }
    uint64_t GuardPushes() const { return guard_pushes_; }
    double Ratio() const { return ratio_; }
    double RateRatio() const { return rate_ratio_; }   // the rate term alone (1 until the window fills)

private:
    std::vector<double> intervals_, scratch_;
    int head_ = 0, count_ = 0;
    double fill_drift_ = -1.0, fill_deficit_ = -1.0;   // frames; < 0 = unset
    double ratio_ = 1.0, rate_ratio_ = 1.0;
    uint64_t pushes_ = 0, resets_ = 0, guard_pushes_ = 0;
};

}  // namespace ps2r::sound
