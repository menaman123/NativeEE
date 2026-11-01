// tests/iop/test_iop_clock.cpp -- IopClock under the FRAME policy (PHASE4_PLAN
// section 4.1 "also landing", ctest IopClockFrame): OnFrameBoundary advances the
// virtual base by exactly one frame quantum (16,667 us); AdvanceToUs never moves
// time backwards; FrameCeilingUs is one quantum past the current frame base (the
// bound of a boundary pump's intra-frame forced advances); a boundary after a
// forced advance beyond the ceiling does not move time backwards; and the kernel
// heartbeat count over N boundary pumps for the three periods the title's hard
// timer can produce. The pump's ceiling is INCLUSIVE (the OLD DrainQuantum rule:
// break only when the next due is PAST the ceiling), so a due that falls exactly
// on the next boundary's tick is serviced one pump early: N pumps from t=0 cover
// the dues in (0, (N+1)*16667], i.e. floor((N+1)*16667/period) beats -- one more
// than N / HeartbeatTicksFor(period) for the 60 Hz heartbeat. Realtime is only
// checked to be monotonic (documented, never hash-gated).
#include <cstdint>
#include <cstdio>

#include "engine/iop/iop_kernel.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;

static int g_failures = 0;
#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) std::printf("PASS: %s\n", name);               \
        else { std::printf("FAIL: %s (line %d)\n", name, __LINE__); g_failures++; } \
    } while (0)

// The DerivedClocks rule for clock 7: heartbeat ticks per frame = max(1, round(usec/16667)).
static uint64_t HeartbeatTicksFor(uint64_t usec) {
    const uint64_t r = (usec + IopClock::kFrameUs / 2) / IopClock::kFrameUs;
    return r ? r : 1;
}

// One boundary pump: advance the frame base, then dispatch with forced advances
// bounded (inclusively) by the frame ceiling -- the module table's DrainQuantum,
// including its guard against a due re-armed at the ceiling by a fired handler.
static void PumpBoundary(IopKernel& k) {
    k.Clock().OnFrameBoundary();
    const uint64_t ceiling = k.Clock().FrameCeilingUs();
    for (int i = 0; i < 4096; i++) {
        auto r = k.Dispatch();
        if (!r.next_due_us || *r.next_due_us > ceiling) break;
        k.Clock().AdvanceToUs(*r.next_due_us);
        if (!r.ran_something && i > 0 && *r.next_due_us >= ceiling) break;
    }
}

int main() {
    ps2r::platform::host::RegisterSelf();
    {
        IopClock c;
        c.SetPolicy(IopClock::Policy::Frame);
        CHECK(c.NowUs() == 0 && c.Frames() == 0, "frame clock starts at 0");
        CHECK(c.FrameCeilingUs() == IopClock::kFrameUs, "ceiling before any boundary = one quantum");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == 16667 && c.Frames() == 1, "one boundary = +16,667 us");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == 33334, "two boundaries = 33,334 us");
        CHECK(c.FrameCeilingUs() == 50001, "ceiling = one quantum past the current base");
        c.AdvanceToUs(40000);
        CHECK(c.NowUs() == 40000, "forced advance moves time forward");
        c.AdvanceToUs(35000);
        CHECK(c.NowUs() == 40000, "forced advance never moves time backwards");
        // FIX-2 (music): an overshoot the next boundary absorbs is not a skip (unchanged rule) ...
        c.AdvanceToUs(60000);   // beyond the ceiling 50001, within the next boundary's 66668
        CHECK(c.NowUs() == 60000 && c.Frames() == 2 && c.Skips() == 0, "an overshoot within the next frame leaves the base (no skip)");
        c.OnFrameBoundary();    // base -> 50001 < 60000: time stays put; this pump's ceiling 66668 covers the rest of the frame
        CHECK(c.NowUs() == 60000 && c.Frames() == 3, "a boundary never moves time backwards past a forced advance");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == 66668, "the next boundary base overtakes the forced advance");
        // ... but an advance beyond the next boundary's ceiling carries the base to the frame before the
        // one containing the time, so the next boundary lands in that frame and runs its remainder.
        // frames 4: base 66668, ceiling 83335, next ceiling 100002. 110000 lies in frame 6 (100002, 116669].
        c.AdvanceToUs(110000);
        CHECK(c.NowUs() == 110000 && c.Frames() == 5 && c.FrameCeilingUs() == 100002,
              "a skip past the next ceiling moves the base to the frame before the time's frame");
        CHECK(c.Skips() == 1 && c.SkippedFrames() == 1 && c.MaxSkipUs() == 110000 - 100002, "the skip census: one skip, one frame, its depth past the next ceiling");
        c.OnFrameBoundary();    // base -> 100002 < 110000: time stays; the pump's ceiling 116669 covers the frame's rest
        CHECK(c.NowUs() == 110000 && c.Frames() == 6 && c.FrameCeilingUs() == 116669, "the boundary after a skip lands in the time's frame (no dark boundary)");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == 116669 && c.Frames() == 7, "then full quanta again");
        c.AdvanceToUs(133336);   // exactly the next ceiling (frames 7: ceiling 133336? no: ceiling = 8*16667 = 133336, next = 150003)
        CHECK(c.Skips() == 1 && c.Frames() == 7, "an advance at the ceiling is not a skip");
        c.AdvanceToUs(150003);   // exactly the next boundary's ceiling: inclusive, no base move
        CHECK(c.Skips() == 1 && c.Frames() == 7 && c.NowUs() == 150003, "an advance exactly to the next ceiling is not a skip");
        c.AdvanceToUs(150003 + 65 * 16667 + 1);   // the 989snd bank load's ~1.08 s drive
        // 1233359 lies in frame 74 ((74*k, 75*k]): the base goes to 73 (66 frames on from 7), the next boundary lands in 74.
        CHECK(c.Frames() == 73 && c.SkippedFrames() == 1 + 66 && (c.Frames() + 2) * IopClock::kFrameUs >= c.NowUs(),
              "a 65-frame skip moves the base to the frame before the time's: the next boundary's ceiling covers the time");
    }
    {
        // The pre-fix clock (iop.frame_base_follows_skips=off): the base stays behind a skip and the
        // boundaries that follow are no-ops until the count catches up -- the census still counts the skip.
        IopClock c;
        c.SetPolicy(IopClock::Policy::Frame);
        c.SetFrameBaseFollowsSkips(false);
        c.OnFrameBoundary();
        c.OnFrameBoundary();
        c.AdvanceToUs(110000);   // past the next boundary's ceiling 66668: the caller's bound, not the clock's
        CHECK(c.Frames() == 2 && c.Skips() == 1 && c.SkippedFrames() == 3, "follow=off: the base stays, the skip is counted (3 frames)");
        c.OnFrameBoundary();    // base -> 50001 < 110000: time stays put
        c.OnFrameBoundary();    // base -> 66668: a DARK boundary (its ceiling 83335 is below the time: nothing could run)
        c.OnFrameBoundary();    // base -> 83335: dark again
        CHECK(c.NowUs() == 110000 && c.Frames() == 5 && c.FrameCeilingUs() == 100002, "follow=off: three boundaries later the ceiling is still below the time");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == 110000 && c.Frames() == 6 && c.FrameCeilingUs() == 116669, "follow=off: the fourth boundary's ceiling finally covers the time");
    }
    {
        IopClock c;
        c.SetPolicy(IopClock::Policy::Realtime);
        const uint64_t a = c.NowUs();
        c.OnFrameBoundary();
        c.AdvanceToUs(1u << 30);
        const uint64_t b = c.NowUs();
        CHECK(b >= a && b < (1u << 30), "realtime ignores frame/forced advances and is monotonic");
    }
    // Heartbeat beats over N boundaries = N / HeartbeatTicksFor(period).
    const uint64_t periods[] = {16667, 33333, 100000};
    for (uint64_t period : periods) {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        int beats = 0;
        k.StartPeriodicTimerUs(period, [&]() -> bool { beats++; return true; });
        const int frames = 60;
        for (int f = 0; f < frames; f++) PumpBoundary(k);
        // Inclusive ceiling: N pumps cover the dues in (0, (N+1)*kFrameUs].
        const int expected = (int)(((uint64_t)(frames + 1) * IopClock::kFrameUs) / period);
        const int per_frame_rule = (int)(frames / HeartbeatTicksFor(period));
        char name[128];
        std::snprintf(name, sizeof(name),
                      "heartbeat period %llu us: %d beats over %d pumps (expected %d; N/HeartbeatTicksFor = %d)",
                      (unsigned long long)period, beats, frames, expected, per_frame_rule);
        CHECK(beats == expected && (expected == per_frame_rule || expected == per_frame_rule + 1), name);
    }
    // A boundary pump advances at most to the ceiling: a delay due past it waits
    // for the next boundary rather than being force-advanced to.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        int fired = 0;
        k.RegisterThread(1, 10);
        k.StartThread(1, [&] { k.DelayUs(20000); fired++; });
        PumpBoundary(k);   // base 16667, ceiling 33334; due at 36667 > ceiling
        CHECK(fired == 0 && k.Clock().NowUs() == 16667, "pump does not force-advance past the frame ceiling");
        PumpBoundary(k);   // base 33334, ceiling 50001: due 36667 is reachable
        CHECK(fired == 1 && k.Clock().NowUs() == 36667, "next boundary's pump reaches the delay by forced advance");
    }
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
