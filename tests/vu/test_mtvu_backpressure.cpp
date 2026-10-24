// VuMtvuBackpressure: the producer park loop on a fake pipeline and a fake
// clock -- fast path (no engagement below both bounds), byte high water with
// hysteresis release at the 7/8 low-water mark, the idle-worker stall valve
// (two no-shrink iterations), the frame-depth park released by present
// progress, the 100 ms frame valve on frozen depth, the i-bit stall break
// (E431: a stalled worker ends any park at once), depth 0 never parking,
// the lifted cap, the shutdown flag, the eager drain per iteration, balanced
// sync-waiter accounting, and the spin-tier constants/clamp.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>

#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_pipeline.h"
#include "engine/vu/tiers/runahead_guard.h"

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

struct TestMtvuBackpressure {
    struct FakeClock : MtvuClock {
        uint64_t now = 5'000'000'000;
        uint64_t NowNs() const override { return now; }
    };

    // The fake pipeline: bytes, idle flag and seal count are plain state; the two
    // waits advance the fake clock by the timeout and run a scripted step.
    struct FakePipeline : MtvuPipeline {
        FakeClock& clock;
        bool running = true;
        bool may_park = true;
        uint64_t bytes = 0;
        bool idle = false;
        bool stalled = false;
        uint64_t seals = 0;
        int drains = 0;
        int idle_waits = 0;
        int frame_waits = 0;
        uint64_t lag = 0;
        int hold_waits = 0;
        std::function<void()> on_hold_wait;
        int waiters = 0;
        int max_waiters = 0;
        std::function<void()> on_idle_wait;
        std::function<void()> on_frame_wait;

        explicit FakePipeline(FakeClock& c) : clock(c) {}
        bool Running() const override { return running; }
        bool MayPark() const override { return may_park; }
        uint64_t PipelineBytes() const override { return bytes; }
        bool WorkerIdle() const override { return idle; }
        bool IrqStalled() const override { return stalled; }
        uint64_t SealCount() const override { return seals; }
        void EagerDrain() override { ++drains; }
        void WaitIdleOrSeal(uint64_t timeout_ns, uint64_t) override {
            ++idle_waits;
            clock.now += timeout_ns;
            if (on_idle_wait) on_idle_wait();
        }
        void WaitFrameProgress(uint64_t timeout_ns) override {
            ++frame_waits;
            clock.now += timeout_ns;
            if (on_frame_wait) on_frame_wait();
        }
        uint64_t FrameLag() const override { return lag; }
        void WaitHoldProgress(uint64_t timeout_ns) override {
            ++hold_waits;
            clock.now += timeout_ns;
            if (on_hold_wait) on_hold_wait();
        }
        void EnterSyncWait() override {
            ++waiters;
            if (waiters > max_waiters) max_waiters = waiters;
        }
        void LeaveSyncWait() override { --waiters; }
    };

        FakeClock clock;
    std::atomic<uint64_t> declared{0};
    std::atomic<uint64_t> presented{0};
    std::atomic<bool> running{true};
    bool low_band = false;
    RunaheadGuard guard;
    MtvuBackpressure bp;
    FakePipeline pipe;

    explicit TestMtvuBackpressure(int depth = 2)
        : guard(depth, [this] { return low_band; }, clock), bp(guard, clock), pipe(clock) {
        bp.Bind(&pipe);
        bp.SetFrameCounters(&declared, &presented);
        bp.SetShutdownFlag(&running);
    }
};

constexpr uint64_t kHigh = MtvuBackpressure::kPipelineHighBytes;
constexpr uint64_t kLow = MtvuBackpressure::kLowWaterBytes;

}  // namespace

int main() {
    static_assert(kHigh == 256ull << 20, "256 MiB pipeline cap");
    static_assert(kLow == kHigh - kHigh / 8, "low water at 7/8");
    static_assert(MtvuBackpressure::kPollNs == 500'000, "500 us poll");
    static_assert(MtvuBackpressure::kSpinBudgetNs == 40000, "40 us spin budget");
    static_assert(MtvuBackpressure::kRecentNs == 200000, "200 us recency");

    // Unbound: a no-op.
    {
        TestMtvuBackpressure::FakeClock clock;
        RunaheadGuard guard(2, {}, clock);
        MtvuBackpressure bp(guard, clock);
        bp.Engage();
        CHECK(bp.Engagements() == 0 && bp.DepthFrames() == 0, "unbound Engage is a no-op; null counters read depth 0");
    }

    // Fast path: below both bounds, nothing happens.
    {
        TestMtvuBackpressure r;
        r.pipe.bytes = kHigh;  // at the cap is not over
        r.declared = 2;        // depth == cap is not over
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 0 && r.pipe.drains == 0 && r.pipe.idle_waits == 0 && r.pipe.frame_waits == 0,
              "fast path: no engagement at the bounds");
        CHECK(r.bp.DepthFrames() == 2, "depth reads the counters");
    }

    // Worker thread / eager drain / stopped worker: never park even when over.
    {
        TestMtvuBackpressure r;
        r.pipe.bytes = kHigh + 1;
        r.pipe.may_park = false;
        r.bp.Engage();
        r.pipe.may_park = true;
        r.pipe.running = false;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 0 && r.pipe.drains == 0, "worker thread / eager drain / stopped: no park");
    }

    // Byte high water: park until the worker frees bytes down to the low-water mark.
    {
        TestMtvuBackpressure r;
        r.pipe.bytes = kHigh + 4096;
        const uint64_t t0 = r.clock.now;
        // Each wait frees 1 MiB; release only once total <= low water.
        r.pipe.on_idle_wait = [&r] { r.pipe.bytes -= 1u << 20; };
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1, "bytes over: one engagement");
        CHECK(r.pipe.bytes <= kLow, "released at or below low water");
        CHECK(r.pipe.bytes + (1u << 20) > kLow, "not released before low water");
        CHECK(r.pipe.idle_waits == 33, "33 x 1 MiB frees from high+4 KiB to <= 7/8 high");
        CHECK(r.pipe.frame_waits == 0, "byte park waits on the worker, not the frame timer");
        CHECK(r.pipe.drains == r.pipe.idle_waits + 1, "one eager drain per loop iteration");
        CHECK(r.clock.now == t0 + 33 * MtvuBackpressure::kPollNs, "fake time advanced by the waits only");
        CHECK(r.bp.StallValveHits() == 0 && r.bp.FrameValveHits() == 0, "no valve");
        CHECK(r.pipe.waiters == 0 && r.pipe.max_waiters == 1, "sync waiter accounting balanced");
    }

    // Stall valve: idle worker and no shrink twice -> release.
    {
        TestMtvuBackpressure r;
        r.pipe.bytes = kHigh + 1;
        r.pipe.idle = true;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.bp.StallValveHits() == 1, "stall valve released the park");
        CHECK(r.pipe.idle_waits == 1, "second no-shrink iteration releases: one wait");
        CHECK(r.pipe.bytes == kHigh + 1, "bytes unchanged (nothing could free them)");
        // A shrink resets the stall count: idle worker, bytes shrink slowly, no valve.
        TestMtvuBackpressure s;
        s.pipe.bytes = kHigh + 1;
        s.pipe.idle = true;
        int guard_iters = 0;
        s.pipe.on_idle_wait = [&s, &guard_iters] {
            s.pipe.bytes -= (kHigh - kLow) / 4 + 1;
            ++guard_iters;
        };
        s.bp.Engage();
        CHECK(s.bp.StallValveHits() == 0 && s.pipe.bytes <= kLow && guard_iters == 4, "shrinking bytes never trip the stall valve");
    }

    // Frame park released by present progress (case A).
    {
        TestMtvuBackpressure r;
        r.declared = 10;  // depth 10 > cap 2
        int waits_until_progress = 50;  // 25 ms of fake time
        r.pipe.on_frame_wait = [&r, &waits_until_progress] {
            if (--waits_until_progress == 0) r.presented = 9;  // depth 1
        };
        const uint64_t t0 = r.clock.now;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1, "frames over: one engagement");
        CHECK(r.pipe.frame_waits == 50 && r.pipe.idle_waits == 0, "frame park waits on the frame timer");
        CHECK(r.bp.FrameValveHits() == 0, "released by progress, not the valve");
        CHECK(r.clock.now == t0 + 50 * MtvuBackpressure::kPollNs, "parked 25 ms of fake time");
        CHECK(r.pipe.drains == 51, "eager drain per iteration (the parked producer delivers boundaries)");
    }

    // Frozen depth: the 100 ms valve releases (case B).
    {
        TestMtvuBackpressure r;
        r.declared = 50;
        r.presented = 9;  // depth 41, never moves
        const uint64_t t0 = r.clock.now;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.bp.FrameValveHits() == 1, "frozen depth released by the valve");
        CHECK(r.pipe.frame_waits == 200, "200 x 500 us = the 100 ms valve");
        CHECK(r.clock.now == t0 + RunaheadGuard::kFrameValveNs, "released exactly at +100 ms");
        // A second engagement re-arms the valve from its own t0.
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 2 && r.bp.FrameValveHits() == 2 && r.pipe.frame_waits == 400,
              "each engagement carries its own 100 ms valve");
    }

    // E431: a worker parked on an i-bit stall ends the park at once -- frozen depth
    // (the title->attract load: the handler's STC is this thread's, after the walk).
    {
        TestMtvuBackpressure r;
        r.declared = 50;
        r.presented = 9;  // depth 41, frozen
        r.pipe.stalled = true;
        const uint64_t t0 = r.clock.now;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.bp.IrqStallBreaks() == 1, "stalled worker: the frame park ends at once");
        CHECK(r.pipe.frame_waits == 0 && r.pipe.idle_waits == 0 && r.clock.now == t0, "no wait, no fake time");
        CHECK(r.bp.FrameValveHits() == 0 && r.bp.StallValveHits() == 0, "not a valve hit");
        CHECK(r.pipe.drains == 1 && r.pipe.waiters == 0, "one eager drain, waiter accounting balanced");
        // Same for the bytes arm (the stall valve needs an IDLE worker; stalled is not idle).
        r.pipe.stalled = true;
        r.declared = 0;
        r.pipe.bytes = kHigh + 1;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 2 && r.bp.IrqStallBreaks() == 2 && r.pipe.idle_waits == 0, "stalled worker: the bytes park ends at once");
        // A stall that lands DURING a frame park ends it on the next iteration.
        TestMtvuBackpressure s;
        s.declared = 50;
        s.presented = 9;
        int waits_until_stall = 20;
        s.pipe.on_frame_wait = [&s, &waits_until_stall] {
            if (--waits_until_stall == 0) s.pipe.stalled = true;
        };
        s.bp.Engage();
        CHECK(s.bp.Engagements() == 1 && s.bp.IrqStallBreaks() == 1 && s.pipe.frame_waits == 20 && s.bp.FrameValveHits() == 0,
              "a stall landing mid-park ends it (20 x 500 us, not the 100 ms valve)");
        // Not stalled: the valve path is unchanged (the frozen-depth case above).
        CHECK(s.bp.ParkNs() == 20 * MtvuBackpressure::kPollNs, "park ns booked up to the break");
    }

    // The HOLD arm (round 5): hold 1 parks while the worker's lag > 1 (a complete frame queued ahead
    // of it), waits on the hold wait (not the frame timer), releases when the worker passes a mark;
    // lag <= hold never engages; the presenter's depth plays no part; an i-bit stall ends it; the
    // 100 ms valve releases a lag that never moves.
    {
        TestMtvuBackpressure r;
        r.guard.SetHold(1);
        r.declared = 2;
        r.presented = 0;   // depth 2 = in-bound for the cap
        r.pipe.lag = 1;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 0 && r.bp.HoldParks() == 0, "lag 1 (the console's double buffer): no park");
        r.pipe.lag = 2;
        int waits_until_mark = 7;
        r.pipe.on_hold_wait = [&r, &waits_until_mark] {
            if (--waits_until_mark == 0) r.pipe.lag = 1;   // the worker passed a mark
        };
        const uint64_t t0 = r.clock.now;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.bp.HoldParks() == 1, "lag 2: one hold engagement");
        CHECK(r.pipe.hold_waits == 7 && r.pipe.frame_waits == 0 && r.pipe.idle_waits == 0, "the hold waits on the worker's mark, not the frame timer");
        CHECK(r.clock.now == t0 + 7 * MtvuBackpressure::kPollNs && r.bp.ParkNs() == 7 * MtvuBackpressure::kPollNs, "parked 3.5 ms of fake time");
        CHECK(r.bp.HoldValveHits() == 0 && r.bp.FrameValveHits() == 0, "released by progress, no valve");
        CHECK(r.pipe.drains == 8 && r.pipe.waiters == 0, "eager drain per iteration; waiter accounting balanced");
        // Depth far over the cap but lag 1: the CAP parks (frame wait), the hold does not count it.
        r.pipe.on_hold_wait = nullptr;
        r.declared = 10;
        r.presented = 0;
        r.pipe.lag = 1;
        r.pipe.on_frame_wait = [&r] { r.presented = 9; };
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 2 && r.bp.HoldParks() == 1 && r.pipe.frame_waits == 1, "the cap's park is not a hold park");
        // A stalled worker: the hold park ends at once (E431).
        r.declared = 2;
        r.presented = 0;
        r.pipe.lag = 3;
        r.pipe.stalled = true;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 3 && r.bp.HoldParks() == 2 && r.bp.IrqStallBreaks() == 1 && r.pipe.hold_waits == 7,
              "stalled worker: the hold park ends without a wait");
        r.pipe.stalled = false;
        // A lag that never moves: the valve releases at +100 ms.
        const uint64_t t1 = r.clock.now;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 4 && r.bp.HoldValveHits() == 1 && r.pipe.hold_waits == 207 && r.clock.now == t1 + RunaheadGuard::kFrameValveNs,
              "a frozen lag rides the 100 ms valve");
        // Hold off: lag is never over.
        TestMtvuBackpressure n;
        n.declared = 2;
        n.pipe.lag = 5;
        n.bp.Engage();
        CHECK(n.bp.Engagements() == 0 && n.bp.HoldParks() == 0, "no hold: the lag never parks");
    }

    // Depth setting 0: the frame arm is never armed (case C).
    {
        TestMtvuBackpressure r(0);
        r.declared = 100;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 0 && r.pipe.frame_waits == 0, "depth 0: no park at all");
    }

    // The lifted cap (2 -> 3 under low band).
    {
        TestMtvuBackpressure r;
        r.declared = 3;
        r.low_band = true;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 0, "depth 3 under the lifted cap: no park");
        r.low_band = false;
        r.pipe.on_frame_wait = [&r] { r.presented = 1; };
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.pipe.frame_waits == 1, "depth 3 at cap 2: parks until progress");
    }

    // Bytes and frames over together: both must clear; the frame arm keeps waiting
    // on the frame timer once the bytes are released.
    {
        TestMtvuBackpressure r;
        r.pipe.bytes = kHigh + 1;
        r.declared = 5;
        r.pipe.on_idle_wait = [&r] { r.pipe.bytes = 0; };
        int fw = 0;
        r.pipe.on_frame_wait = [&r, &fw] {
            if (++fw == 3) r.presented = 5;
        };
        r.bp.Engage();
        CHECK(r.pipe.idle_waits == 1 && r.pipe.frame_waits == 3, "bytes then frames: waits on each arm in turn");
        CHECK(r.bp.Engagements() == 1, "one engagement for both arms");
    }

    // Shutdown flag: the park exits at once.
    {
        TestMtvuBackpressure r;
        r.declared = 50;
        r.running = false;
        r.bp.Engage();
        CHECK(r.bp.Engagements() == 1 && r.pipe.frame_waits == 0 && r.pipe.drains == 0 && r.pipe.waiters == 0,
              "shutdown releases the park before any wait");
    }

    // Spin tier constants and clamp.
    {
        CHECK(MtvuBackpressure::ClampSpinBudgetNs(0) == MtvuBackpressure::kSpinBudgetMinNs, "clamp low");
        CHECK(MtvuBackpressure::ClampSpinBudgetNs(999) == 1000, "clamp low edge");
        CHECK(MtvuBackpressure::ClampSpinBudgetNs(MtvuBackpressure::kSpinBudgetNs) == 40000, "default within range");
        CHECK(MtvuBackpressure::ClampSpinBudgetNs(1000000) == 1000000, "clamp high edge");
        CHECK(MtvuBackpressure::ClampSpinBudgetNs(5000000) == MtvuBackpressure::kSpinBudgetMaxNs, "clamp high");
        CHECK(!MtvuBackpressure::SpinArmed(0, 123456), "no previous batch: park cold");
        CHECK(MtvuBackpressure::SpinArmed(1000, 1000 + MtvuBackpressure::kRecentNs), "recent batch: spin armed");
        CHECK(!MtvuBackpressure::SpinArmed(1000, 1001 + MtvuBackpressure::kRecentNs), "stale batch: park cold");
        MtvuBackpressure::CpuRelax();
    }

    // KICK DROP (the clock contract, DT-14): drop mode never parks. The frame
    // sync's lag decides the next frame; the bytes bound is a mid-frame valve.
    {
        TestMtvuBackpressure r;
        r.bp.SetDropMode(true);
        static_assert(MtvuBackpressure::kDropLagFrames == 2, "two frames of worker lag are allowed");
        // Over both bounds: no park (no drain, no wait, no engagement), and no drop
        // from the bytes alone while the frame's verdict is Admit... the valve:
        r.pipe.bytes = kHigh + 1;
        r.declared = 10;
        CHECK(r.bp.Engage(), "drop mode, bytes over the cap: the valve drops the rest of the frame");
        CHECK(r.bp.Engagements() == 0 && r.pipe.drains == 0 && r.pipe.idle_waits == 0 && r.pipe.frame_waits == 0,
              "drop mode never parks (no engagement, no drain, no wait)");
        CHECK(r.bp.DropValveFrames() == 1 && r.bp.DropFrames() == 1 && r.bp.DropEpisodes() == 1 && r.bp.DropSegments() == 1,
              "the valve counts a dropped frame, an episode, the segment");
        CHECK(r.bp.Engage() && r.bp.DropSegments() == 2, "the rest of the frame stays dropped");
        // The frame sync with the lag within the bound: the next frame is admitted.
        CHECK(!r.bp.NoteFrameSync(2) && !r.bp.DroppingFrame(), "lag 2 at the frame sync: the next frame runs");
        r.pipe.bytes = kLow;
        CHECK(!r.bp.Engage() && r.bp.DropSegments() == 2, "admitted frame: submits are not dropped");
        // Lag past the bound: the next frame is dropped, every submit tagged.
        CHECK(r.bp.NoteFrameSync(3) && r.bp.DroppingFrame(), "lag 3: the next frame is a dropped frame");
        CHECK(r.bp.Engage() && r.bp.Engage() && r.bp.DropSegments() == 4, "its submits are tagged");
        CHECK(r.bp.DropFrames() == 2 && r.bp.DropEpisodes() == 2, "a second dropped frame, a new episode");
        CHECK(r.bp.NoteFrameSync(4) && r.bp.DropFrames() == 3 && r.bp.DropEpisodes() == 2,
              "a consecutive dropped frame extends the episode");
        CHECK(!r.bp.NoteFrameSync(0) && r.bp.DropFrames() == 3, "the worker caught up: admitted");
        CHECK(r.bp.LagMaxFrames() == 4, "the largest lag seen is published");
        CHECK(r.pipe.waiters == 0 && r.pipe.max_waiters == 0, "no sync waiter was ever registered");
        // Drop mode off: the park is back (bytes over -> an engagement with the stall valve).
        r.bp.SetDropMode(false);
        r.pipe.bytes = kHigh + 1;
        r.pipe.idle = true;
        r.declared = 0;
        CHECK(!r.bp.Engage() && r.bp.Engagements() == 1 && r.bp.StallValveHits() == 1, "park mode restored: the bytes park engages");
        CHECK(!r.bp.NoteFrameSync(9) && !r.bp.DroppingFrame(), "park mode: the frame sync decides nothing (lag still published)");
        CHECK(r.bp.LagMaxFrames() == 9, "lag max tracked in both modes");
    }

    // The steady clock exists and advances monotonically.
    {
        const uint64_t a = MtvuClock::Steady().NowNs();
        const uint64_t b = MtvuClock::Steady().NowNs();
        CHECK(b >= a && a != 0, "steady clock monotone");
    }

    if (g_fail == 0) {
        std::printf("test_mtvu_backpressure: ALL PASS\n");
        return 0;
    }
    std::printf("test_mtvu_backpressure: %d FAILURE(S)\n", g_fail);
    return 1;
}
