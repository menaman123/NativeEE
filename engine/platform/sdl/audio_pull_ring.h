// platform/sdl/audio_pull_ring.h -- the single-producer / single-consumer PCM
// ring between the tick-clocked mixer and the wall-clocked audio device.
//
// Cross-thread contract (the determinism boundary, PHASE7_PLAN section 3.1):
//   * Push() is called by ONE producer (the EE boundary thread) with the 800
//     stereo frames the mixer produced for one vsync (48000 Hz / 60).
//   * Pull() is called by ONE consumer (the SDL device callback) for whatever
//     chunk the device asks for (512 frames per callback at the sink's spec).
//   * Neither side ever blocks or waits on the other. A full ring drops its
//     OLDEST whole quanta (audible skip; `overruns` and `dropped` counted); an
//     empty ring zero-fills the remainder of the pull (audible gap; `underruns`
//     counted). Nothing -- no counter, no clock, no back-pressure -- is ever
//     read back by the producer to steer the engine.
//   * Pad-with-tail (SetPadWithTail, the clock contract rule 4; the play host
//     turns it on): the device never hears a gap, a skip or a pitch change
//     because the game thread was late. A pull the ring cannot fill repeats
//     the PREVIOUS QUANTUM's tail (the last kQuantumFrames pushed, cyclically,
//     phase-continuous across pulls) instead of zeros -- counted `padded`
//     frames, never an underrun -- and marks a RE-LATCH: the next Push first
//     re-pushes the previous quantum once (`relatches`; the stream's lead is
//     rebuilt by one quantum, as the device would re-read a held buffer),
//     then the new audio. The lead is bounded the other way: a Push that
//     leaves more than TrimFrames() in the ring (a late game thread catching
//     up in a burst) discards the OLDEST frames down to the standing lead
//     (`trimmed`: the late audio the pad already stood in for), so latency
//     never grows across stalls and the overflow drop never triggers.
//     Prime() pushes the standing lead of silence once so an ordinary late
//     tick is absorbed without any pad.
//   * Adaptive lead: the standing lead starts at kLeadQuanta and GROWS by
//     one quantum each time a pad had to stand in (the producer's cadence is
//     longer than the lead -- a game thread running below 60 Hz delivers its
//     quanta in bursts every 60-90 ms), up to kMaxLeadQuanta; the trim bound
//     follows it (lead + kTrimSlackQuanta). A lead that covers the cadence makes
//     pads, re-latches and trims stop: the device hears every produced frame
//     once, continuously, at a latency of one cadence. After kDecayPushes
//     pushes without a pad the lead target steps back down by one (no frame is
//     cut for it: only the trim bound moves, so a 60 Hz producer that hitched
//     once returns to the short lead at its next burst). LeadQuanta() /
//     TrimFrames() read the live values.
//   * Positions are monotonic 64-bit frame counts published with release /
//     acquire atomics. The write position is owned by the producer; the read
//     position advances by compare-exchange from BOTH sides (the consumer
//     after a pull, the producer when it discards the oldest quanta), so a
//     pull that raced a drop detects it and re-reads instead of returning
//     torn audio. Lock-free and allocation-free after construction.
#pragma once

#include <atomic>
#include <cstdint>

namespace ps2r::platform::sdl {

class AudioPullRing {
public:
    static constexpr int kChannels       = 2;
    static constexpr int kQuantumFrames  = 800;                            // one vsync at 48 kHz
    static constexpr int kCapacityQuanta = 12;                             // 200 ms of audio
    static constexpr int kCapacityFrames = kQuantumFrames * kCapacityQuanta;

    AudioPullRing();
    AudioPullRing(const AudioPullRing&) = delete;
    AudioPullRing& operator=(const AudioPullRing&) = delete;

    // Producer side. `interleaved` holds `frames` stereo frames (L,R,L,R...).
    // If fewer than `frames` are free, whole quanta are discarded from the
    // read end first (oldest audio goes) until the new frames fit. A push
    // larger than the capacity keeps only its newest kCapacityFrames.
    void Push(const float* interleaved, int frames);

    // Consumer side. Fills `out` with `frames` stereo frames; whatever the
    // ring cannot supply is written as zeros and counted as one underrun.
    void Pull(float* out, int frames);

    // Rule 4 (see above). Set before the device starts; default off (zero-fill).
    static constexpr int kLeadQuanta = 3;                                  // the initial / minimum lead (50 ms)
    static constexpr int kMaxLeadQuanta = kCapacityQuanta - 2;             // the lead never fills the ring (167 ms)
    static constexpr int kTrimSlackQuanta = 2;                             // trim a push that leaves more than lead + this
    static constexpr int kDecayPushes = 600;                               // pushes without a pad before the lead steps down
    void SetPadWithTail(bool on) { pad_with_tail_.store(on, std::memory_order_release); }
    bool PadWithTail() const { return pad_with_tail_.load(std::memory_order_acquire); }
    // Producer side, once before the stream (and after a restore's discard):
    // the standing lead of silence (counted as pushed).
    void Prime();
    // The live standing lead (quanta) and the trim bound (frames) it implies.
    int LeadQuanta() const { return lead_quanta_.load(std::memory_order_relaxed); }
    int TrimFrames() const { return (LeadQuanta() + kTrimSlackQuanta) * kQuantumFrames; }

    // Consumer-side tap: called from Pull() on the device thread with the
    // FINAL frames handed to the device (ring audio, pad or silence alike) -- the
    // stream a listener hears, for a host's WAV capture. Set before the device
    // starts, cleared after it stops; nothing it does flows back into the ring.
    using PullTap = void (*)(void* ctx, const float* interleaved, int frames);
    void SetPullTap(PullTap fn, void* ctx);

    // Frames readable at the moment of the call (a valid bound for either side).
    int Available() const;
    // Lifecycle owner only, after producer join AND device callback stop.
    // Drops obsolete output without rewinding process counters/positions (pad
    // mode: the debt and the tail are dropped with it -- silence, not a pad,
    // until the stream pushes again; Prime() restores the lead).
    uint64_t DiscardStopped();

    // Monotonic counters. pushed/pulled/dropped are in frames; the two
    // *runs counters count events.
    uint64_t pushed() const;
    uint64_t pulled() const;
    uint64_t dropped() const;
    uint64_t underruns() const;
    uint64_t overruns() const;
    uint64_t padded() const;      // frames filled from the previous tail at a pull (pad mode)
    uint64_t relatches() const;   // previous quanta re-pushed to rebuild the lead (pad mode)
    uint64_t trimmed() const;     // late frames discarded above TrimFrames() (pad mode)

private:
    void CopyIn(const float* src, int frames, uint64_t write_pos);
    void CopyOut(float* dst, int frames, uint64_t read_pos) const;
    void PadFromTail(float* dst, int frames, uint64_t write_pos);
    void PullInto(float* out, int frames);   // Pull() without the tap
    void PushRaw(const float* interleaved, int frames);
    void TrimToLead(uint64_t write_pos);

    float                 samples_[kCapacityFrames * kChannels];
    std::atomic<uint64_t> read_{0};
    std::atomic<uint64_t> write_{0};
    std::atomic<uint64_t> pushed_{0};
    std::atomic<uint64_t> pulled_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<bool>     pad_with_tail_{false};
    std::atomic<bool>     tail_valid_{false};   // a quantum has been pushed since the stream (re)started
    std::atomic<bool>     relatch_{false};      // consumer -> producer: a pad happened, re-push the previous quantum
    std::atomic<uint64_t> padded_{0};
    std::atomic<uint64_t> relatches_{0};
    std::atomic<uint64_t> trimmed_{0};
    std::atomic<int>      lead_quanta_{kLeadQuanta};   // producer-owned value, read by anyone
    int                   pushes_since_pad_ = 0;      // producer-owned: the lead's decay clock
    PullTap               tap_fn_ = nullptr;   // consumer-side capture tap (set before the device runs)
    void*                 tap_ctx_ = nullptr;
    uint64_t              pad_phase_ = 0;  // consumer-owned: position inside the repeated tail
    float                 last_quantum_[kQuantumFrames * kChannels] = {};   // producer-owned: the previous push
    int                   last_quantum_frames_ = 0;
};

}  // namespace ps2r::platform::sdl
