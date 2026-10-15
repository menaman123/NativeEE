// tests/iop/test_module_table.cpp -- IopModuleTable (ctest IopModuleTable): the
// FRAME pump points over the kernel. Pinned: the boundary pump's quantum drain
// (dues serviced up to the INCLUSIVE frame ceiling -- P4-CEILING-INCLUSIVE: 60
// pumps of a 16,667 us timer fire 61 beats), the park-loop pump's one-quantum
// cap, OnModuleLoaded running every started thread to its first block before
// returning, the no-op rules (before boot, under REALTIME), PumpAfterEeService
// resuming exactly the woken Wait::Rpc parkee, the policy read from Config, the
// module census and the watch-line grammar; PumpWithinFrame (the EE's in-frame
// poll): it never moves the frame base or the ceiling, runs exactly the dues at
// or before the ceiling (a deferred event inside the frame fires and is
// reported; one past the ceiling waits for the next boundary pump and the pump
// answers "nothing ran"), and is a no-op before boot.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/iop/iop_module_table.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;
using ps2r::iop::IopModuleTable;

static int g_failures = 0;
#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) std::printf("PASS: %s\n", name);               \
        else { std::printf("FAIL: %s (line %d)\n", name, __LINE__); g_failures++; } \
    } while (0)

static void TestQuantumDrain() {
    // A 1,000 us periodic timer under the boundary pump. The kernel's periodic
    // timer fires ONE beat per pass and resyncs forward (no beat burst at the
    // frame jump), so the drained count per pump is the number of dues that
    // fall in (base, ceiling] after the jump -- pinned by the contract, not by
    // a closed form: after every pump the next due is PAST the ceiling and time
    // never runs past it.
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    int beats = 0;
    k.StartPeriodicTimerUs(1000, [&]() -> bool { beats++; return true; });
    m.OnModuleLoaded(0);
    const int pumps = 8;
    bool ceiling_ok = true, bound_ok = true, per_pump_ok = true;
    for (int i = 0; i < pumps; i++) {
        const int before = beats;
        m.PumpAtBoundary();
        const uint64_t ceiling = k.Clock().FrameCeilingUs();
        const IopKernel::DispatchResult r = k.Dispatch();   // probe: nothing due <= ceiling remains
        if (!r.next_due_us || *r.next_due_us <= ceiling || *r.next_due_us > ceiling + 1000) ceiling_ok = false;
        if (k.Clock().NowUs() > ceiling) bound_ok = false;
        const int got = beats - before;
        if (got < 15 || got > 18) per_pump_ok = false;   // ~16.7 dues per quantum, +1 at the jump
    }
    std::printf("quantum drain: %d beats over %d pumps of a 1,000 us timer\n", beats, pumps);
    CHECK(ceiling_ok, "after each boundary pump the next due is past the (inclusive) ceiling and within one period of it");
    CHECK(bound_ok, "forced advances never run past the frame ceiling");
    CHECK(per_pump_ok, "each pump drains every due of its quantum (15..18 beats of a 1 ms timer)");
    CHECK(beats == 134, "recorded count: 134 beats over 8 pumps (one beat per pass + forward resync at the jump)");
    CHECK(k.Clock().Frames() == (uint64_t)pumps, "one frame boundary per PumpAtBoundary");
}

static void TestInclusiveCeiling() {
    IopModuleTable m(IopClock::Policy::Frame);
    int beats = 0;
    m.Kernel().StartPeriodicTimerUs(IopClock::kFrameUs, [&]() -> bool { beats++; return true; });
    m.OnModuleLoaded(0);
    for (int i = 0; i < 60; i++) m.PumpAtBoundary();
    std::printf("60 Hz heartbeat: %d beats over 60 pumps\n", beats);
    CHECK(beats == 61, "P4-CEILING-INCLUSIVE: a due exactly on the next boundary's tick is serviced one pump early");
}

static void TestParkLoopPumpBound() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    bool resumed = false;
    uint64_t resumed_at = 0;
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] {
        k.DelayUs(100000);
        resumed = true;
        resumed_at = k.Clock().NowUs();
        for (;;) k.SleepThread();
    });
    m.OnModuleLoaded(1);
    CHECK(k.Clock().NowUs() == 0, "OnModuleLoaded ran the thread to its first block without advancing time");
    for (int i = 1; i <= 3; i++) {
        m.PumpFromParkLoop();
        CHECK(k.Clock().NowUs() <= (uint64_t)i * IopClock::kFrameUs, "park-loop pump advances at most one quantum per call");
    }
    CHECK(!resumed, "a 100 ms delay has not expired after three park-loop pumps");
    int pumps = 3;
    while (!resumed && pumps < 20) { m.PumpFromParkLoop(); pumps++; }
    std::printf("park loop: resumed after %d pumps at t=%llu us\n", pumps, (unsigned long long)resumed_at);
    CHECK(resumed && resumed_at >= 100000, "the delay expires once the capped advances reach its due");
    CHECK(pumps == 7, "100,000 us = six capped quanta (6 x 16,667 > 100,000) + the dispatch that runs it");
}

static void TestOnModuleLoadedRunsToFirstBlock() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    std::vector<int> ran;
    for (int tid = 1; tid <= 3; tid++) {
        k.RegisterThread(tid, 20 + tid);
        k.StartThread(tid, [&, tid] {
            ran.push_back(tid);
            for (;;) k.SleepThread();
        });
    }
    CHECK(ran.empty(), "started threads have not run before OnModuleLoaded");
    m.OnModuleLoaded(1);
    CHECK(ran.size() == 3 && ran[0] == 1 && ran[1] == 2 && ran[2] == 3,
          "OnModuleLoaded runs every started thread to its first block (priority order)");
    CHECK(k.UnparkedCount() == 0, "no thread is left unparked after boot");
    CHECK(m.Booted() && m.Rpc().RpcTid() == 1, "Booted() and the RPC tid recorded");
}

static void TestNoOpRules() {
    {
        IopModuleTable m(IopClock::Policy::Frame);
        int beats = 0;
        m.Kernel().StartPeriodicTimerUs(1000, [&]() -> bool { beats++; return true; });
        m.PumpAtBoundary();
        m.PumpAfterEeService();
        m.PumpFromParkLoop();
        CHECK(beats == 0 && m.Kernel().Clock().Frames() == 0, "every pump is a no-op before OnModuleLoaded");
    }
    {
        IopModuleTable m(IopClock::Policy::Frame);
        CHECK(m.FramePolicy(), "FRAME policy selected");
        m.OnModuleLoaded(0);
        m.PumpAtBoundary();
        CHECK(!m.Host().Running(), "OnModuleLoaded under FRAME never starts the host thread (the EE drives Dispatch)");
        CHECK(m.Host().Passes() == 0, "the host thread made no dispatch pass under FRAME");
        m.Shutdown();
        CHECK(!m.Host().Running() && m.Host().Passes() == 0, "Shutdown under FRAME leaves the host census at zero");
    }
    {
        IopModuleTable m(IopClock::Policy::Realtime);
        CHECK(!m.FramePolicy(), "REALTIME policy selected");
        m.OnModuleLoaded(0);
        m.PumpAtBoundary();
        m.PumpFromParkLoop();
        CHECK(m.Kernel().Clock().Frames() == 0, "PumpAtBoundary is a no-op under REALTIME (the host thread dispatches)");
        CHECK(m.Host().Running(), "the host thread runs under REALTIME after boot");
        m.Shutdown();
        CHECK(!m.Host().Running(), "Shutdown stops the host thread");
    }
}

static void TestPumpAfterEeServiceResumesParkee() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    int woken2 = -1, woken3 = -1;
    k.RegisterThread(2, 10);
    k.RegisterThread(3, 11);
    k.StartThread(2, [&] {
        woken2 = m.Rpc().WaitEeReplyUs(1000000) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    k.StartThread(3, [&] {
        k.SleepThread();                 // parked in Sleep, must NOT be resumed by the reply
        woken3 = 1;
        for (;;) k.SleepThread();
    });
    m.OnModuleLoaded(0);
    CHECK(k.ThreadWaitKind(2) == IopKernel::WaitKind::Rpc, "the marshalling worker parks in Wait::Rpc");
    CHECK(m.Rpc().EeWaitTid() == 2, "the bridge records the parked tid");
    // Slot 6: the EE services the marshal, then the module table pumps.
    m.Rpc().NotifyEeReply();
    CHECK(woken2 == -1, "the wake is queued, not applied inside the EE call");
    m.PumpAfterEeService();
    CHECK(woken2 == 1, "PumpAfterEeService resumes the woken Wait::Rpc parkee (woken = true)");
    CHECK(woken3 == -1 && k.ThreadWaitKind(3) == IopKernel::WaitKind::Sleep, "the Sleep parkee is untouched");
    CHECK(k.Clock().Frames() == 0, "PumpAfterEeService does not advance the frame base");
}

static void TestPolicyFromConfig() {
    ps2r::Config cfg;
    cfg.iop.dispatch = ps2r::IopDispatch::Frame;
    IopModuleTable frame(cfg);
    CHECK(frame.FramePolicy() && frame.Kernel().Clock().GetPolicy() == IopClock::Policy::Frame,
          "Config iop.dispatch=frame selects the FRAME policy");
    cfg.iop.dispatch = ps2r::IopDispatch::Realtime;
    IopModuleTable rt(cfg);
    CHECK(!rt.FramePolicy() && rt.Kernel().Clock().GetPolicy() == IopClock::Policy::Realtime,
          "Config iop.dispatch=realtime selects the REALTIME policy");
    CHECK(ps2r::Config{}.iop.dispatch == ps2r::IopDispatch::Frame, "the shipped default is frame (until BUG-026 closes)");
}

static void TestModuleCensusAndWatchLine() {
    IopModuleTable m(IopClock::Policy::Frame);
    CHECK(m.RegisterModule("sio2man", 0x203) == 0 && m.RegisterModule("padman", 0x204) == 1, "module registration indices");
    m.RegisterModule("stream", 0, 1);
    CHECK(m.ModuleCount() == 3 && m.Modules()[2].rpc_tid == 1, "module entries kept in registration order");
    CHECK(m.ModulesCensus() == "sio2man:515,padman:516,stream:0", "iop_modules= census string");
    IopKernel& k = m.Kernel();
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] { for (;;) k.SleepThread(); });
    m.OnModuleLoaded(1);
    char buf[256];
    const int n = m.FormatWatchLine(buf, sizeof(buf));
    std::printf("%s\n", buf);
    CHECK(n > 0 && std::strncmp(buf, "[IOPK-WATCH] t1=3/1 t2=0/0 t3=0/0 t4=0/0 unparked=0", 52) == 0,
          "watch line grammar: [IOPK-WATCH] t<i>=<state>/<wait> x4 unparked=<n>");
}

static void TestPumpWithinFrameKeepsTheBase() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    CHECK(!m.PumpWithinFrame(), "PumpWithinFrame before boot is a no-op (false)");
    m.OnModuleLoaded(0);
    m.PumpAtBoundary();   // frame 1: base 16,667, ceiling 33,334
    const uint64_t frames = k.Clock().Frames();
    const uint64_t ceiling = k.Clock().FrameCeilingUs();
    const uint64_t now = k.Clock().NowUs();
    CHECK(frames == 1 && ceiling == 2 * IopClock::kFrameUs, "one boundary pump: frame 1, ceiling at the next boundary's tick");
    // Nothing due: the pump ran nothing and moved nothing.
    CHECK(!m.PumpWithinFrame() && k.Clock().Frames() == frames && k.Clock().FrameCeilingUs() == ceiling && k.Clock().NowUs() == now,
          "nothing due inside the frame: false, base/ceiling/now untouched");
    // A completion inside the frame (a module read's drive end 3,000 us after the issue) fires at the poll.
    int fired = 0;
    k.ScheduleEventUs(now + 3000, [&] { fired++; });
    CHECK(m.PumpWithinFrame() && fired == 1, "a due inside the frame fires at the in-frame pump and is reported (true)");
    CHECK(k.Clock().Frames() == frames && k.Clock().FrameCeilingUs() == ceiling && k.Clock().NowUs() == now + 3000,
          "the frame base and ceiling are unchanged; time advanced only to the due");
    // A completion past the ceiling: not this frame's.
    k.ScheduleEventUs(ceiling + 1, [&] { fired++; });
    CHECK(!m.PumpWithinFrame() && fired == 1 && k.Clock().NowUs() <= ceiling, "a due past the ceiling does not fire at the in-frame pump (false)");
    m.PumpAtBoundary();
    CHECK(fired == 2 && k.Clock().Frames() == frames + 1, "the next boundary pump fires it (the base advanced by the boundary alone)");
}

static void TestPumpToInstantFiresOnlyWhatThePresentPassed() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    CHECK(!m.PumpToInstant(), "PumpToInstant before boot is a no-op (false)");
    m.OnModuleLoaded(0);
    m.PumpAtBoundary();   // frame 1: base 16,667, ceiling 33,334
    const uint64_t frames = k.Clock().Frames();
    const uint64_t ceiling = k.Clock().FrameCeilingUs();
    const uint64_t base = frames * IopClock::kFrameUs;
    CHECK(k.Clock().NowUs() == base && k.Clock().OffsetInFrameUs() == 0, "at the base the in-frame offset is 0");
    // Two dues inside the frame: one the instant will pass, one later in the frame.
    int early = 0, late = 0;
    k.ScheduleEventUs(base + 2000, [&] { early++; });
    k.ScheduleEventUs(base + 9000, [&] { late++; });
    CHECK(!m.PumpToInstant() && early == 0 && late == 0 && k.Clock().NowUs() == base,
          "nothing due at the instant: false, nothing fired, the clock did not move to a later due");
    // The round trips' charge (the bridge's rule, applied here by hand): the instant passes the early due.
    k.Clock().AdvanceToUs(base + 20 * 160);
    CHECK(k.Clock().OffsetInFrameUs() == 3200, "twenty 160 us round trips: the in-frame offset is 3,200 us");
    CHECK(m.PumpToInstant() && early == 1 && late == 0 && k.Clock().NowUs() == base + 3200,
          "the due the instant passed fires (true); the later due waits; the clock stays at the instant");
    CHECK(k.Clock().Frames() == frames && k.Clock().FrameCeilingUs() == ceiling, "the frame base and ceiling are unchanged");
    // The in-frame pump (to the ceiling) still reaches the later due; the boundary pump still moves the base.
    CHECK(m.PumpWithinFrame() && late == 1 && k.Clock().NowUs() == base + 9000, "PumpWithinFrame fires the later due inside the frame");
    k.Clock().AdvanceToUs(ceiling + 100);   // the slice consumed with an overshoot of 100 us
    CHECK(k.Clock().OffsetInFrameUs() == IopClock::kFrameUs + 100, "an overshoot reads past kFrameUs");
    m.PumpAtBoundary();
    CHECK(k.Clock().Frames() == frames + 1 && k.Clock().NowUs() == ceiling + 100 && k.Clock().OffsetInFrameUs() == 100,
          "the boundary moves the base; the overshoot is the next frame's first instant (never backwards)");
}

int main() {
    ps2r::platform::host::RegisterSelf();
    TestPumpWithinFrameKeepsTheBase();
    TestPumpToInstantFiresOnlyWhatThePresentPassed();
    TestQuantumDrain();
    TestInclusiveCeiling();
    TestParkLoopPumpBound();
    TestOnModuleLoadedRunsToFirstBlock();
    TestNoOpRules();
    TestPumpAfterEeServiceResumesParkee();
    TestPolicyFromConfig();
    TestModuleCensusAndWatchLine();
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
