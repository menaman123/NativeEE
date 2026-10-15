// tests/iop/test_iop_kernel.cpp -- IopKernel scheduling semantics + the FRAME-clock
// determinism property (PHASE4_PLAN section 4.1, ctest IopKernelUnits; the eight
// cases of the OLD suite on the NEW kernel).
//
// Covers: priority scheduling (lower number first, FIFO among equals),
// WakeupThread token banking, semaphore FIFO-vs-priority wake order and direct
// handoff, event-flag AND/OR/clear-on-match modes and the single-waiter
// attribute, message boxes (send/poll/receive, blocking receive), DelayThread
// ordering under forced virtual-clock advance, deferred events (counted queue,
// never the issuing pass), periodic timer rearm semantics, and determinism: two
// fresh kernels running the same scripted workload under the FRAME clock produce
// identical effect sequences. Prints PASS/FAIL per check, exits nonzero on any failure.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/iop/iop_kernel.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;

static int g_failures = 0;

#define CHECK(cond, name)                                                 \
    do {                                                                  \
        if (cond) {                                                       \
            std::printf("PASS: %s\n", name);                              \
        } else {                                                          \
            std::printf("FAIL: %s (line %d)\n", name, __LINE__);          \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

// Drive a FRAME-clock kernel until idle, force-advancing to deadlines.
static void DriveIdle(IopKernel& k, int max_iters = 1000) {
    for (int i = 0; i < max_iters; i++) {
        auto r = k.Dispatch();
        if (!r.next_due_us) {
            if (!r.ran_something) return;
            continue;
        }
        k.Clock().AdvanceToUs(*r.next_due_us);
    }
}

static void TestPriorityOrder() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    std::vector<int> order;
    k.RegisterThread(1, /*prio=*/40);
    k.RegisterThread(2, /*prio=*/20);
    k.RegisterThread(3, /*prio=*/40);
    k.StartThread(1, [&] { order.push_back(1); });
    k.StartThread(2, [&] { order.push_back(2); });
    k.StartThread(3, [&] { order.push_back(3); });
    k.Dispatch();
    CHECK(order == std::vector<int>({2, 1, 3}),
          "priority: lower number first, FIFO among equals");
}

static void TestWakeupBanking() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    std::vector<std::string> fx;
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] {
        fx.push_back("pre-sleep");
        k.SleepThread();          // banked token: returns immediately
        fx.push_back("banked");
        k.SleepThread();          // no token: parks
        fx.push_back("woken");
    });
    k.iWakeupThread(1);           // banks a token BEFORE the first sleep?
    // No: thread not yet run; wakeup arrives at dispatch start while thread
    // is Ready -> banks. First SleepThread consumes it.
    k.Dispatch();
    CHECK(fx == std::vector<std::string>({"pre-sleep", "banked"}),
          "wakeup token banked and consumed by next sleep");
    k.iWakeupThread(1);
    k.Dispatch();
    CHECK(fx.size() == 3 && fx[2] == "woken", "sleep wakes on wakeup");
}

static void TestSemaFifoVsPrio() {
    // FIFO attribute: wake order = wait order regardless of priority.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        std::vector<int> woke;
        k.RegisterSema(0, 0, 4, /*prio_order=*/false);
        k.RegisterThread(1, 40);
        k.RegisterThread(2, 10);
        k.StartThread(1, [&] { k.WaitSema(0); woke.push_back(1); });
        k.StartThread(2, [&] { k.WaitSema(0); woke.push_back(2); });
        k.Dispatch();   // both park; prio-2 parks FIRST (higher priority)
        k.InjectSignalSema(0);
        k.Dispatch();
        k.InjectSignalSema(0);
        k.Dispatch();
        CHECK(woke == std::vector<int>({2, 1}),
              "sema fifo: wake order = wait order");
    }
    // PRIO attribute: the highest-priority waiter wakes first.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        std::vector<int> woke;
        k.RegisterSema(0, 0, 4, /*prio_order=*/true);
        k.RegisterThread(1, 10);   // waits first
        k.RegisterThread(2, 40);   // waits second (lower priority)
        k.RegisterThread(3, 5);    // waits third (highest priority)
        k.StartThread(1, [&] { k.WaitSema(0); woke.push_back(1); });
        k.StartThread(2, [&] { k.WaitSema(0); woke.push_back(2); });
        k.StartThread(3, [&] { k.WaitSema(0); woke.push_back(3); });
        k.Dispatch();
        for (int i = 0; i < 3; i++) {
            k.InjectSignalSema(0);
            k.Dispatch();
        }
        CHECK(woke == std::vector<int>({3, 1, 2}),
              "sema prio: highest-priority waiter first");
    }
    // Direct handoff + count guard.
    {
        IopKernel k;
        k.Clock().SetPolicy(IopClock::Policy::Frame);
        k.RegisterSema(0, 0, 1, false);
        k.SetSemaCount(0, 0);
        k.InjectSignalSema(0);
        k.Dispatch();
        k.InjectSignalSema(0);   // count at max: guard holds it at 1
        k.Dispatch();
        CHECK(k.SemaCount(0) == 1, "sema count capped at max_count");
    }
}

static void TestEventFlagModes() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    std::vector<std::string> fx;
    const int ef = k.CreateEventFlag(0, /*multi=*/true);
    k.RegisterThread(1, 10);
    k.RegisterThread(2, 20);
    k.RegisterThread(3, 30);
    // AND wait (mode 0): needs BOTH bits.
    k.StartThread(1, [&] {
        k.WaitEventFlag(ef, 0x3, 0x0);
        fx.push_back("and");
    });
    // OR wait (mode 1): any bit.
    k.StartThread(2, [&] {
        k.WaitEventFlag(ef, 0x3, 0x1);
        fx.push_back("or");
    });
    // OR + clear (mode 0x11).
    k.StartThread(3, [&] {
        k.WaitEventFlag(ef, 0x4, 0x11);
        fx.push_back("orclear");
    });
    k.Dispatch();
    CHECK(fx.empty(), "eventflag: all waiters parked on empty flag");
    k.SetEventFlag(ef, 0x1);
    k.Dispatch();
    CHECK(fx == std::vector<std::string>({"or"}),
          "eventflag OR wakes on one bit; AND still parked");
    k.SetEventFlag(ef, 0x2);
    k.Dispatch();
    CHECK(fx.size() == 2 && fx[1] == "and", "eventflag AND wakes on both");
    k.SetEventFlag(ef, 0x4);
    k.Dispatch();
    CHECK(fx.size() == 3 && fx[2] == "orclear",
          "eventflag clear-mode waiter wakes");
    CHECK(k.EventFlagValue(ef) == 0, "clear-on-match zeroed the flag");
    // ClearEventFlag keeps only the mask (thevent semantics).
    k.SetEventFlag(ef, 0xFF);
    k.ClearEventFlag(ef, 0x0F);
    CHECK(k.EventFlagValue(ef) == 0x0F, "ClearEventFlag ANDs the keep mask");
}

static void TestMbx() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    const int mbx = k.CreateMbx();
    int a = 1, b = 2;
    CHECK(k.PollMbx(nullptr, mbx) == -1, "mbx poll empty returns -1");
    k.SendMbx(mbx, &a);
    k.SendMbx(mbx, &b);
    void* got = nullptr;
    k.PollMbx(&got, mbx);
    CHECK(got == &a, "mbx FIFO order (first message first)");
    std::vector<void*> recvd;
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] {
        void* m = nullptr;
        k.ReceiveMbx(&m, mbx);   // b is there: immediate
        recvd.push_back(m);
        k.ReceiveMbx(&m, mbx);   // empty: parks
        recvd.push_back(m);
    });
    k.Dispatch();
    CHECK(recvd.size() == 1 && recvd[0] == &b, "mbx receive drains then parks");
    int c = 3;
    k.SendMbx(mbx, &c);          // wakes the parked receiver
    k.Dispatch();
    CHECK(recvd.size() == 2 && recvd[1] == &c, "mbx send wakes receiver");
}

static void TestDelayAndEvents() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    std::vector<std::string> fx;
    k.RegisterThread(1, 10);
    k.RegisterThread(2, 20);
    k.StartThread(1, [&] {
        k.DelayUs(500);
        fx.push_back("d500");
        k.DelayUs(1000);
        fx.push_back("d1500");
    });
    k.StartThread(2, [&] {
        k.DelayUs(700);
        fx.push_back("d700");
    });
    // Deferred event due at t=600.
    k.ScheduleEventUs(600, [&] { fx.push_back("evt600"); });
    DriveIdle(k);
    CHECK(fx == std::vector<std::string>(
                    {"d500", "evt600", "d700", "d1500"}),
          "delays and events fire in deadline order under forced advance");
    // Counted queue: an event scheduled during a pass fires next pass.
    fx.clear();
    k.StartThread(1, [&] {
        k.ScheduleEventUs(0, [&] { fx.push_back("evt"); });
        fx.push_back("body");
    });
    k.Dispatch();
    CHECK(fx == std::vector<std::string>({"body"}),
          "event never fires in the issuing pass");
    k.Dispatch();
    CHECK(fx.size() == 2 && fx[1] == "evt", "event fires next pass");
}

static void TestPeriodicTimer() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    int beats = 0;
    k.StartPeriodicTimerUs(1000, [&]() -> bool {
        beats++;
        return beats < 3;   // rearm twice, then stop
    });
    DriveIdle(k);
    CHECK(beats == 3, "periodic timer rearm=nonzero beats, 0 stops");
}

// Scripted workload for the determinism property: threads with mixed
// priorities ping-pong a semaphore, delay, bank wakeups, and schedule
// events; a periodic timer beats. Record every effect with its virtual
// timestamp.
static std::vector<std::string> RunScriptedWorkload() {
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    std::vector<std::string> fx;
    auto note = [&](const char* what, int who) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%llu:%s:%d",
                      (unsigned long long)k.Clock().NowUs(), what, who);
        fx.push_back(buf);
    };
    k.RegisterSema(0, 0, 1, false);
    k.RegisterThread(1, 30);    // "dispatcher"
    k.RegisterThread(2, 20);    // "worker"
    k.RegisterThread(3, 40);    // "background"
    k.StartThread(2, [&] {
        for (int i = 0; i < 5; i++) {
            k.WaitSema(0);
            note("worker-pass", i);
            k.ScheduleEventUs(k.Clock().NowUs() + 300,
                              [&note, i] { note("completion", i); });
            k.WakeupThread(1);
        }
        note("worker-done", 0);
    });
    k.StartThread(1, [&] {
        for (int i = 0; i < 5; i++) {
            note("dispatch", i);
            k.SignalSema(0);
            k.SleepThread();
            k.DelayUs(400);
        }
        note("dispatcher-done", 0);
    });
    k.StartThread(3, [&] {
        for (int i = 0; i < 8; i++) {
            k.DelayUs(250);
            note("bg", i);
        }
    });
    k.StartPeriodicTimerUs(1000, [&]() -> bool {
        static thread_local int dummy = 0;
        (void)dummy;
        note("beat", 0);
        return k.Clock().NowUs() < 5000;
    });
    // Drive like the FRAME boundary pump: one frame quantum at a time.
    for (int frame = 0; frame < 8; frame++) {
        k.Clock().OnFrameBoundary();
        const uint64_t ceiling = k.Clock().FrameCeilingUs();
        for (int i = 0; i < 4096; i++) {
            auto r = k.Dispatch();
            if (!r.next_due_us || *r.next_due_us > ceiling) break;
            k.Clock().AdvanceToUs(*r.next_due_us);
        }
    }
    return fx;
}

static void TestFrameDeterminism() {
    auto run1 = RunScriptedWorkload();
    auto run2 = RunScriptedWorkload();
    CHECK(!run1.empty(), "scripted workload produced effects");
    CHECK(run1 == run2,
          "determinism: two FRAME-clock runs produce identical effect "
          "sequences");
    if (run1 != run2) {
        const size_t n = run1.size() < run2.size() ? run1.size() : run2.size();
        for (size_t i = 0; i < n; i++) {
            if (run1[i] != run2[i]) {
                std::printf("  first divergence at %zu: '%s' vs '%s'\n", i,
                            run1[i].c_str(), run2[i].c_str());
                break;
            }
        }
    }
}

int main() {
    ps2r::platform::host::RegisterSelf();   // the coroutines' fiber backend
    TestPriorityOrder();
    TestWakeupBanking();
    TestSemaFifoVsPrio();
    TestEventFlagModes();
    TestMbx();
    TestDelayAndEvents();
    TestPeriodicTimer();
    TestFrameDeterminism();
    std::printf("%s: %d failure(s)\n",
                g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
