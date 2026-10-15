// tests/iop/test_iop_clock_rebase.cpp -- IopClock::RebaseToFrame (ctest IopClockRebase,
// BUG-066): after a savestate restore the FRAME clock's frame base must be the
// restored image's vsync frame count, not only its microsecond base. The restore
// path (IopmSection::ReconcileKernel) rebases the clock to the anchor's frame
// count; the module plugin then re-arms the STREAM.IRX heartbeat hard timer with
// its due one period past the restored now. Pins:
//   (i)   RebaseToFrame(F): Frames() == F, NowUs() == F * kFrameUs, and ONE
//         OnFrameBoundary advances NowUs() by exactly one quantum -- a bare
//         AdvanceToUs(F * kFrameUs) (the former rebase) leaves Frames() at 0, so a
//         boundary's base (1 quantum) lies below the rebased time and cannot move it;
//   (ii)  a periodic timer armed at `now + 16727` (the title heartbeat's period,
//         compare 0x0e75 at 36.864 MHz / 64) after the rebase beats on the SECOND
//         bare boundary (the first boundary's base, F+1 quanta, is 60 us short of
//         the due); under the boundary pump's forced advances (inclusive ceiling,
//         the module table's DrainQuantum) it beats in the FIRST pump;
//   (iii) a due of the pre-load world (below the rebased base) fires at once;
//   (iv)  the rebase never moves time backwards (forward-only like every advance)
//         and is a no-op under Realtime (documented, never hash-gated).
#include <cstdint>
#include <cstdio>

#include "engine/iop/iop_kernel.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;

static int g_failures = 0;
#define CHECK(cond, name)                                                            \
    do {                                                                             \
        if (cond) std::printf("PASS: %s\n", name);                                   \
        else { std::printf("FAIL: %s (line %d)\n", name, __LINE__); g_failures++; } \
    } while (0)

static constexpr uint64_t kAnchorFrame = 40304;    // slowspot_f40298's restored vsync frame
static constexpr uint64_t kHeartbeatUs = 16727;    // the module's heartbeat period

// The module table's DrainQuantum: advance the base, then dispatch with forced
// advances bounded (inclusively) by the frame ceiling.
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
    const uint64_t base = kAnchorFrame * IopClock::kFrameUs;
    // (i) the rebase moves frames AND time; one boundary then advances one quantum.
    {
        IopClock c;
        c.SetPolicy(IopClock::Policy::Frame);
        c.RebaseToFrame(kAnchorFrame);
        CHECK(c.Frames() == kAnchorFrame, "RebaseToFrame sets Frames() to the restored frame count");
        CHECK(c.NowUs() == base, "RebaseToFrame moves time to frames * kFrameUs");
        CHECK(c.FrameCeilingUs() == base + IopClock::kFrameUs, "ceiling after the rebase = one quantum past the rebased base");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == base + IopClock::kFrameUs && c.Frames() == kAnchorFrame + 1,
              "one boundary after the rebase advances NowUs() by exactly kFrameUs");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == base + 2 * IopClock::kFrameUs, "the second boundary advances another quantum");
    }
    // The contrast (the former rebase): a bare AdvanceToUs leaves the frame base behind.
    {
        IopClock c;
        c.SetPolicy(IopClock::Policy::Frame);
        c.AdvanceToUs(base);
        CHECK(c.Frames() == 0, "a bare AdvanceToUs does not move the frame base");
        c.OnFrameBoundary();
        CHECK(c.NowUs() == base && c.Frames() == 1,
              "after a bare AdvanceToUs a boundary's base (1 quantum) cannot move the rebased time");
    }
    // (ii) a heartbeat armed after the rebase beats.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        k.Clock().RebaseToFrame(kAnchorFrame);
        int beats = 0;
        k.StartPeriodicTimerUs(kHeartbeatUs, [&]() -> bool { beats++; return true; });
        k.Dispatch();   // takes the injection; due = base + 16727 > now
        CHECK(beats == 0 && k.TimerBeats() == 0, "armed at now + 16727: no beat at the rebased now");
        k.Clock().OnFrameBoundary();
        k.Dispatch();
        CHECK(beats == 0 && k.Clock().NowUs() == base + IopClock::kFrameUs,
              "first bare boundary: base F+1 is 60 us short of the due, no beat");
        k.Clock().OnFrameBoundary();
        k.Dispatch();
        CHECK(beats == 1 && k.TimerBeats() == 1, "second bare boundary: the heartbeat beats once (TimerBeats census = 1)");
        for (int f = 0; f < 60; f++) {
            k.Clock().OnFrameBoundary();
            k.Dispatch();
        }
        CHECK(beats >= 59 && beats <= 61, "the heartbeat keeps beating about once per frame afterwards");
        CHECK(k.TimerBeats() == (uint64_t)beats, "TimerBeats counts every beat");
    }
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        k.Clock().RebaseToFrame(kAnchorFrame);
        int beats = 0;
        k.StartPeriodicTimerUs(kHeartbeatUs, [&]() -> bool { beats++; return true; });
        k.Dispatch();
        PumpBoundary(k);
        CHECK(beats == 1 && k.Clock().NowUs() == base + kHeartbeatUs,
              "under the boundary pump (inclusive ceiling) the heartbeat beats in the first pump, time at the due");
    }
    // Without the frame rebase the same kernel scenario never beats (the BUG-066 shape).
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        k.Clock().AdvanceToUs(base);
        int beats = 0;
        k.StartPeriodicTimerUs(kHeartbeatUs, [&]() -> bool { beats++; return true; });
        for (int f = 0; f < 10; f++) PumpBoundary(k);
        CHECK(beats == 0 && k.Clock().NowUs() == base,
              "contrast: with only the microsecond base rebased, 10 pumps never reach the due (the frozen clock)");
    }
    // (iii) a pre-load due (below the rebased base) fires at once.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        int fired = 0;
        k.ScheduleEventUs(5 * IopClock::kFrameUs, [&] { fired++; });
        k.Dispatch();   // injection taken in generation 1; fires at generation 2 at the earliest
        k.Clock().RebaseToFrame(kAnchorFrame);
        k.Dispatch();
        CHECK(fired == 1, "a due of the pre-load world (below the rebased base) expires at once");
    }
    // (iv) forward-only; Realtime no-op.
    {
        IopClock c;
        c.SetPolicy(IopClock::Policy::Frame);
        c.AdvanceToUs(base + 1000);
        c.RebaseToFrame(kAnchorFrame);
        CHECK(c.NowUs() == base + 1000 && c.Frames() == kAnchorFrame,
              "the rebase never moves time backwards past a forced advance (frame base still set)");
        IopClock r;
        r.SetPolicy(IopClock::Policy::Realtime);
        r.RebaseToFrame(kAnchorFrame);
        CHECK(r.Frames() == 0 && r.NowUs() < base, "Realtime ignores the rebase");
    }
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
