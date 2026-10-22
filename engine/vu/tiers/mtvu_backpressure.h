#pragma once
// Producer backpressure for the MTVU pipeline. On hardware the EE cannot run
// ahead of VU1 by more than the VIF1 FIFO; the host bounds the producer two
// ways and parks it (Engage, producer thread only) when either is exceeded:
//   bytes  -- queued + undelivered payload above kPipelineHighBytes, released
//             with hysteresis at the low-water mark (7/8) or by the stall valve
//             (an idle worker with no shrink twice in a row);
//   frames -- declared minus presented frames above the RunaheadGuard cap,
//             released when presents catch up or by the guard's 100 ms valve.
// Every park iteration runs one eager arbiter drain (under deferred
// boundaries the parked producer is the only boundary deliverer) and waits
// at most kPollNs on the pipeline. A shutdown flag releases any park, and so
// does a worker parked on a VIFcode i-bit stall (E431: the EE serves that
// interrupt at its next dispatch point -- AFTER the walk this park sits in).
//
// KICK DROP (the clock contract, DT-14, play mode -- SetDropMode): the game
// thread NEVER parks on the worker. The worker's lag is measured in FRAMES
// (frame marks the producer pushes at every frame sync minus the marks the
// worker has passed); at a frame sync with the lag above kDropLagFrames the
// NEXT frame is a DROPPED frame: every VIF1 segment the producer submits in it
// is tagged, the worker replays its VIF stream (UNPACK / MPG / PATH2 -- VU1
// memory and the GS state stay coherent) but runs no VU1 program (the kicks
// are counted, not executed: VuUnit::SetKickDrop). A frame's draws are missing
// for that frame; the game ran at 60 Hz. The bytes bound stays as a VALVE: a
// frame whose pipeline bytes pass kPipelineHighBytes mid-frame drops the rest
// of the frame too (counted). The Mecha-Bandicoot boss on baf15a38: the two
// VU1 programs the library misses ran on the cached tier at ~75% of real
// time, the bytes park held the game thread ~560 ms every ~25 ticks
// (game_hz 25). The deterministic profile runs no MTVU (unchanged); the
// harness's freerun legs keep the park (drop mode is play mode's).
//
// Spin tier constants for the worker's wait-for-work (the worker loop uses
// them): a bounded spin of kSpinBudgetNs is armed only when the previous
// batch completed within kRecentNs, else the worker parks cold.
//
// Cross-thread contract: Engage / Admit / NoteFrameSync on the producer
// thread only; the counters are read relaxed; statistics are producer-thread
// plain members.
#include <atomic>
#include <cstdint>

class MtvuClock;
class MtvuPipeline;
class RunaheadGuard;

class MtvuBackpressure {
public:
    static constexpr uint64_t kPipelineHighBytes = 256ull << 20;
    static constexpr uint64_t kLowWaterBytes = kPipelineHighBytes - kPipelineHighBytes / 8;
    static constexpr uint64_t kPollNs = 500'000;
    static constexpr int kStallValveCount = 2;
    static constexpr uint64_t kSpinBudgetNs = 40000;
    static constexpr uint64_t kSpinBudgetMinNs = 1000;
    static constexpr uint64_t kSpinBudgetMaxNs = 1000000;
    static constexpr uint64_t kRecentNs = 200000;
    // Drop mode: the worker may be this many frames behind the game at a frame
    // sync; one more and the next frame's kicks are dropped.
    static constexpr uint64_t kDropLagFrames = 2;

    MtvuBackpressure(RunaheadGuard& guard, const MtvuClock& clock);

    // The pipeline the park loop reads and waits on (null = Engage is a no-op).
    void Bind(MtvuPipeline* pipeline);
    // Frame counters: declared at the boundary enqueue, presented at the
    // genuine VSync present; null = depth 0 (frame arm never parks).
    void SetFrameCounters(const std::atomic<uint64_t>* declared, const std::atomic<uint64_t>* presented);
    // While `running` reads false the park loop exits at once; null = never.
    void SetShutdownFlag(const std::atomic<bool>* running);

    // Producer thread: park while a bound is exceeded; immediate otherwise.
    // In drop mode it never parks: it returns whether this submit's kicks are
    // to be DROPPED (the frame is a dropped frame, or the bytes valve tripped).
    bool Engage();

    // Drop mode (play): the park becomes the per-frame kick drop above.
    void SetDropMode(bool on) { drop_mode_ = on; }
    bool DropMode() const { return drop_mode_; }
    // Producer thread, at the frame sync AFTER the frame's mark was pushed:
    // `lag_frames` = marks pushed - marks the worker passed. Decides the next
    // frame. Returns whether the next frame is a dropped frame.
    bool NoteFrameSync(uint64_t lag_frames);
    bool DroppingFrame() const { return dropping_frame_; }

    uint64_t DepthFrames() const;
    uint64_t Engagements() const { return engagements_; }
    uint64_t ParkNs() const { return park_ns_; }   // total ns the producer spent parked in Engage
    uint64_t FrameValveHits() const { return frame_valve_hits_; }
    uint64_t StallValveHits() const { return stall_valve_hits_; }
    // Parks ended because the worker was parked on an i-bit stall (E431).
    uint64_t IrqStallBreaks() const { return irq_stall_breaks_; }
    // Drop census: frames whose kicks were dropped, segments tagged, drop
    // episodes (a run of consecutive dropped frames), frames the bytes valve
    // cut mid-frame, the largest lag seen at a frame sync.
    uint64_t DropFrames() const { return drop_frames_; }
    uint64_t DropSegments() const { return drop_segments_; }
    uint64_t DropEpisodes() const { return drop_episodes_; }
    uint64_t DropValveFrames() const { return drop_valve_frames_; }
    uint64_t LagMaxFrames() const { return lag_max_frames_; }

    // A spin budget clamped to [kSpinBudgetMinNs, kSpinBudgetMaxNs].
    static uint64_t ClampSpinBudgetNs(uint64_t ns);
    // Arm the spin tier: the previous batch completed within kRecentNs.
    static bool SpinArmed(uint64_t prev_work_ns, uint64_t now_ns);
    static void CpuRelax();

private:
    uint64_t Declared() const;
    uint64_t Presented() const;
    bool EngageDrop();

    RunaheadGuard& guard_;
    const MtvuClock& clock_;
    MtvuPipeline* pipeline_ = nullptr;
    const std::atomic<uint64_t>* declared_ = nullptr;
    const std::atomic<uint64_t>* presented_ = nullptr;
    const std::atomic<bool>* running_ = nullptr;
    uint64_t engagements_ = 0;
    uint64_t park_ns_ = 0;
    uint64_t frame_valve_hits_ = 0;
    uint64_t stall_valve_hits_ = 0;
    uint64_t irq_stall_breaks_ = 0;
    bool drop_mode_ = false;
    bool dropping_frame_ = false;
    uint64_t drop_frames_ = 0;
    uint64_t drop_segments_ = 0;
    uint64_t drop_episodes_ = 0;
    uint64_t drop_valve_frames_ = 0;
    uint64_t lag_max_frames_ = 0;
};
