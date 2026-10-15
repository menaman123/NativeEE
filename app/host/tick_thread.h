#pragma once
// TickThread: the play-mode field ticker -- the host pacer the TickSource's
// Play policy expects. One host thread sleeps to an ABSOLUTE deadline every
// field period (TickSource::PeriodUs; the base period is one 60 Hz field) and
// publishes the tick to the EE side as the timing design fixes it: a pacer is
// a non-guest thread, so it never runs the tick body itself -- it NOTES a
// pending tick (TickSync::NotePending, fired on the EE at its next fixed
// point: the boundary's TickAdvance slot or a parked kernel timer wait) and
// WAKES the timer waiters (BoundaryProtocol::NotifyTick). No wall value
// crosses into the time domain: only the count of noted ticks does.
//
// Cadence rule (the reference's paced ticker; the clock contract's 60 Hz
// wall edge): the edges are anchor + k * period with the base period EXACTLY
// 1/60 s (nanosecond rational, no rounding drift), so a late wake shortens the
// following sleep instead of pushing the whole train later; when the ticker
// falls more than one period behind (a debugger pause, a system stall) it
// re-anchors to now rather than firing a burst of back-to-back ticks.
//
// Started by the composition root ONLY in play mode (a window exists and the
// boundary is not EE-synchronous); the deterministic policy has no ticker.
// Stop() joins the thread; the destructor stops it.
#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

class BoundaryProtocol;
class TickSource;
class TickSync;

class TickThread {
public:
    TickThread(const TickSource& source, TickSync& ticks, BoundaryProtocol& protocol);
    ~TickThread();
    TickThread(const TickThread&) = delete;
    TickThread& operator=(const TickThread&) = delete;

    void Start();
    void Stop();
    bool Running() const { return thread_.joinable(); }

    // Ticks noted so far (each one NotePending + NotifyTick).
    uint64_t Ticks() const { return ticks_noted_.load(std::memory_order_relaxed); }
    // Times the cadence re-anchored to now after falling > one period behind.
    uint64_t Resyncs() const { return resyncs_.load(std::memory_order_relaxed); }
    // The wall edge (steady_clock ns) tick `tick` (1-based, the k-th noted) was
    // published at, for the last kEdgeRing ticks; false when not (yet) noted or
    // evicted. The clock census attributes a boundary to its first field's edge,
    // so a 1 s window counts the edges answered in it, not the answer latencies.
    static constexpr size_t kEdgeRing = 4096;
    bool EdgeAt(uint64_t tick, int64_t* ns) const;

private:
    void Loop();

    const TickSource& source_;
    TickSync& ticks_;
    BoundaryProtocol& protocol_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> ticks_noted_{0};
    std::atomic<uint64_t> resyncs_{0};
    std::array<std::atomic<int64_t>, kEdgeRing> edge_ns_{};
};
