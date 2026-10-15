// Observable scheduling, wake, lifecycle and error semantics of imported IOP HLE.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "engine/iop/hle/intrman/intrman_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/iop_error.h"
#include "engine/iop/iop_module_table.h"
#include "engine/platform/host/platform.h"

using namespace ps2r::iop;
using namespace ps2r::iop::hle;

static int failures = 0;
#define CHECK(condition, message) do { \
    if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, message); ++failures; } \
} while (0)

static int Priority(ThbaseModule& th, int tid, uint32_t priority) {
    IopContext c;
    c.gpr[4] = static_cast<uint32_t>(tid);
    c.gpr[5] = priority;
    th.ChangeThreadPriority(c);
    return static_cast<int32_t>(c.gpr[2]);
}

static void TestReadyRequeue() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    IopKernel& k = table.Kernel();
    std::vector<int> order;
    for (int tid = 1; tid <= 3; ++tid) {
        k.RegisterThread(tid, tid == 1 ? 10 : 30);
        k.StartThread(tid, [&, tid] { order.push_back(tid); });
    }
    CHECK(Priority(th, 1, 30) == 0, "ready target accepts priority change");
    CHECK(Priority(th, 2, 30) == 0, "same-priority target is also requeued");
    CHECK(Priority(th, 3, 127) == IopError::IllegalPriority, "127 is reserved");
    CHECK(Priority(th, 3, 0xFFFFFFFF) == IopError::IllegalPriority, "negative priority rejected");
    CHECK(Priority(th, 3, 0) == IopError::IllegalContext, "host cannot borrow caller priority");
    CHECK(Priority(th, 0, 20) == IopError::IllegalContext, "host has no self thread");
    CHECK(Priority(th, -1, 20) == IopError::UnknownThid, "unknown thread rejected");
    k.Dispatch();
    CHECK((order == std::vector<int>{3, 1, 2}), "ready reinsertion goes behind equal peers; errors leave order intact");
    CHECK(Priority(th, 1, 20) == IopError::Dormant, "dormant thread rejected");
    const auto count = th.Count();
    CHECK(count.priority_calls == 8 && count.priority_changes == 1 && count.priority_errors == 6,
          "census separates calls, numeric changes, and errors");
    CHECK(k.UnparkedCount() == 0, "requeue does not inflate runnable census");
}

static void TestSelfAndPeerReschedule() {
    for (bool peer : {false, true}) {
        IopModuleTable table(IopClock::Policy::Frame);
        IopMemory mem;
        ThbaseModule th(table, mem, {}, {});
        IopKernel& k = table.Kernel();
        std::vector<int> order;
        k.RegisterThread(1, 10);
        k.RegisterThread(2, peer ? 40 : 20);
        k.RegisterThread(3, peer ? 10 : 20);
        k.StartThread(1, [&] {
            order.push_back(10);
            CHECK(Priority(th, peer ? 2 : 0, peer ? 5 : 20) == 0, "priority syscall succeeds");
            order.push_back(11);
        });
        k.StartThread(2, [&] { order.push_back(2); });
        k.StartThread(3, [&] { order.push_back(3); });
        k.Dispatch();
        if (peer) {
            CHECK((order == std::vector<int>{10, 2, 11, 3}),
                  "promoted peer preempts; interrupted caller stays ahead of equal peers");
        } else {
            CHECK((order == std::vector<int>{10, 2, 3, 11}),
                  "self demotion schedules equal peers before syscall returns");
        }
    }
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    IopKernel& k = table.Kernel();
    std::vector<int> order;
    k.RegisterThread(1, 20);
    k.RegisterThread(2, 40);
    k.StartThread(1, [&] {
        CHECK(Priority(th, 2, 0) == 0, "priority zero inherits caller priority");
        order.push_back(1);
        CHECK(Priority(th, 0, 0) == 0, "self/same-priority call rotates behind peers");
        order.push_back(3);
    });
    k.StartThread(2, [&] { order.push_back(2); });
    k.Dispatch();
    CHECK((order == std::vector<int>{1, 2, 3}), "same-priority self call yields without changing numeric priority");
}

static void TestWaiterRequeue() {
    for (bool priority_queue : {false, true}) {
        IopModuleTable table(IopClock::Policy::Frame);
        IopMemory mem;
        ThbaseModule th(table, mem, {}, {});
        ThsemapModule sem(table, mem);
        IopKernel& k = table.Kernel();
        IopContext c;
        c.gpr[4] = 0x1000;
        mem.Write<uint32_t>(0x1000, priority_queue ? 1 : 0);
        mem.Write<uint32_t>(0x100C, 1);
        sem.CreateSema(c);
        const uint32_t sid = c.gpr[2];
        std::vector<int> order;
        for (int tid = 1; tid <= 2; ++tid) {
            k.RegisterThread(tid, tid == 1 ? 10 : 20);
            k.StartThread(tid, [&, tid] {
                IopContext wait;
                wait.gpr[4] = sid;
                sem.WaitSema(wait);
                CHECK(wait.gpr[2] == 0, "semaphore waiter returns success after handoff");
                order.push_back(tid);
            });
        }
        k.Dispatch();
        CHECK(Priority(th, 1, 20) == 0, "waiting thread priority changes");
        c.gpr[4] = sid;
        sem.SignalSema(c);
        k.Dispatch();
        CHECK(order.size() == 1 && order[0] == (priority_queue ? 2 : 1),
              "priority queue reinserts after equals; FIFO wait order is unchanged");
        sem.SignalSema(c);
        k.Dispatch();
        CHECK(order.size() == 2 && k.SemaCount(static_cast<int>(sid)) == 0,
              "each signal releases exactly one waiter without banking a count");
    }
    IopKernel k;
    k.Clock().SetPolicy(IopClock::Policy::Frame);
    k.RegisterSema(0, 0, 2, true);
    std::vector<int> order;
    for (int tid = 1; tid <= 2; ++tid) {
        k.RegisterThread(tid, tid * 10);
        k.StartThread(tid, [&, tid] { k.WaitSema(0); order.push_back(tid); });
    }
    k.Dispatch();
    CHECK(k.ChangePriority(2, 5) == 0, "parked waiter promotion succeeds");
    k.SetSemaCount(0, 1);
    k.ReconcileSemaWaiters();
    k.Dispatch();
    CHECK((order == std::vector<int>{2}), "restore reconciliation honors priority after a waiter change");
}

static void TestSleepingPriority() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    IopKernel& k = table.Kernel();
    std::vector<int> order;
    for (int tid = 1; tid <= 2; ++tid) {
        k.RegisterThread(tid, tid * 10);
        k.StartThread(tid, [&, tid] { k.SleepThread(); order.push_back(tid); });
    }
    k.Dispatch();
    CHECK(Priority(th, 2, 5) == 0, "sleeping thread can be promoted without waking");
    k.Dispatch();
    CHECK(order.empty(), "priority change does not release sleep");
    for (int tid = 1; tid <= 2; ++tid) {
        IopContext c;
        c.gpr[4] = static_cast<uint32_t>(tid);
        th.WakeupThread(c);
        CHECK(c.gpr[2] == 0, "wakeup succeeds for sleepers");
    }
    k.Dispatch();
    CHECK((order == std::vector<int>{2, 1}), "woken sleepers run in updated priority order");
}

static void TestDelay() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    IopKernel& k = table.Kernel();
    std::vector<int> order;
    for (int tid = 1; tid <= 3; ++tid) {
        k.RegisterThread(tid, tid * 10);
        k.StartThread(tid, [&, tid] {
            IopContext c;
            c.gpr[4] = tid == 1 ? 250000 : tid == 2 ? 100000 : 0;
            th.DelayThread(c);
            CHECK(c.gpr[2] == 0, "kernel delay returns success after expiry");
            order.push_back(tid);
        });
    }
    auto result = k.Dispatch();
    CHECK(order.empty() && result.next_due_us == 100, "zero delay parks at the alarm's 100us minimum");
    CHECK(Priority(th, 1, 5) == 0, "priority can change during a timed wait");
    k.Clock().AdvanceToUs(99);
    k.Dispatch();
    CHECK(order.empty(), "no early wake before minimum delay");
    k.Clock().AdvanceToUs(100);
    k.Dispatch();
    k.Clock().AdvanceToUs(100000);
    k.Dispatch();
    CHECK((order == std::vector<int>{3, 2}), "delay above 100ms is not truncated");
    k.Clock().AdvanceToUs(249999);
    k.Dispatch();
    CHECK(order.size() == 2, "priority change preserves delay deadline");
    k.Clock().AdvanceToUs(250000);
    k.Dispatch();
    CHECK((order == std::vector<int>{3, 2, 1}), "long delay resumes at its requested deadline");
    IopContext host;
    host.gpr[4] = 500;
    th.DelayThread(host);
    CHECK(static_cast<int32_t>(host.gpr[2]) == IopError::IllegalContext, "host delay cannot falsely report success");
    CHECK(k.Clock().NowUs() == 250000, "host error does not advance the clock");
    const auto counts = th.Count();
    CHECK(counts.delay_kernel == 3 && counts.delay_host == 1, "delay reachability counts distinguish contexts");
}

static void TestSemaphoreErrors() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThsemapModule sem(table, mem);
    IopKernel& k = table.Kernel();
    IopContext c;
    c.gpr[4] = 50;
    sem.SignalSema(c);
    CHECK(c.gpr[2] == 0, "host signal accepts a queued request");
    k.Dispatch();
    CHECK(k.InjectedSignalErrors() == 1, "unknown queued signal fails when delivered");
    sem.WaitSema(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::UnknownSemid, "unknown wait fails");
    k.RegisterSema(0, 0, 1);
    c.gpr[4] = 0;
    sem.WaitSema(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::CanNotWait, "host empty wait cannot report a token acquired");
    sem.SignalSema(c);
    sem.iSignalSema(c);
    k.Dispatch();
    CHECK(k.InjectedSignalErrors() == 2, "queued overflow is rejected and counted on delivery");
    k.RegisterThread(2, 20);
    k.StartThread(2, [&] {
        IopContext signal;
        signal.gpr[4] = 50;
        sem.SignalSema(signal);
        CHECK(static_cast<int32_t>(signal.gpr[2]) == IopError::UnknownSemid, "guest unknown signal returns error");
        signal.gpr[4] = 0;
        sem.SignalSema(signal);
        CHECK(static_cast<int32_t>(signal.gpr[2]) == IopError::SemaOverflow, "guest overflow returns error");
    });
    k.Dispatch();
    sem.WaitSema(c);
    CHECK(c.gpr[2] == 0 && k.SemaCount(0) == 0, "one token available after overflow");
    int woke_result = 1;
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] {
        IopContext wait;
        wait.gpr[4] = 0;
        sem.WaitSema(wait);
        woke_result = static_cast<int32_t>(wait.gpr[2]);
    });
    k.Dispatch();
    sem.DeleteSema(c);
    CHECK(c.gpr[2] == 0, "delete releases pending waiters");
    // Reuse the same slot before the old waiter resumes: deletion result
    // belongs to the wait, not the slot's present active state.
    k.RegisterSema(0, 1, 1);
    k.Dispatch();
    CHECK(woke_result == IopError::WaitDelete && k.SemaCount(0) == 1,
          "deleted wait returns error and cannot consume a newly created semaphore's token");
    c.gpr[4] = 0x1000;
    mem.Write<uint32_t>(0x1000, 2);
    mem.Write<uint32_t>(0x100C, 1);
    sem.CreateSema(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalAttr, "unsupported semaphore attributes rejected");
    CHECK(sem.HostWaitCount() == 3 && sem.ErrorCount() == 6, "semaphore shortcut errors and host waits counted");
    ThbaseModule th(table, mem, {}, {});
    CHECK(th.Count().injected_signal_errors == 2, "thread census exposes rejected queued signals");
}

static void TestThreadLifecycle() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    int entries = 0, stale_continuation = 0;
    std::vector<int> restart_order;
    ThbaseModule th(table, mem, [&](uint32_t addr) -> ThbaseModule::GuestFunction {
        if (addr != 0x2000) return {};
        return [&](IopContext& c) {
            ++entries;
            restart_order.push_back(1);
            th.SleepThread(c);
            ++stale_continuation;
        };
    }, {0x100000, 0x4000, 0});
    IopKernel& k = table.Kernel();
    mem.Write<uint32_t>(0x1008, 0x2000);
    mem.Write<uint32_t>(0x100C, 0x1000);
    mem.Write<uint32_t>(0x1010, 20);
    IopContext c;
    c.gpr[4] = 0x1000;
    th.CreateThread(c);
    const uint32_t tid = c.gpr[2];
    c.gpr[4] = tid;
    th.TerminateThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::Dormant, "terminating dormant thread fails");
    th.WakeupThread(c);
    k.Dispatch();
    CHECK(k.InjectedWakeErrors() == 1, "dormant queued wake is rejected without banking a token");
    CHECK(th.Count().injected_wake_errors == 1, "thread census exposes rejected queued wakes");
    th.StartThread(c);
    CHECK(c.gpr[2] == 0, "created thread starts");
    th.StartThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::NotDormant, "duplicate start rejected");
    th.DeleteThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::NotDormant, "live delete rejected");
    k.Dispatch();
    CHECK(entries == 1 && stale_continuation == 0, "failed dormant wake did not skip first sleep");
    CHECK(Priority(th, static_cast<int>(tid), 40) == 0, "change active thread away from creation priority");
    th.TerminateThread(c);
    th.StartThread(c);
    restart_order.clear();
    k.RegisterThread(2, 30);
    k.StartThread(2, [&] { restart_order.push_back(2); });
    k.Dispatch();
    CHECK(entries == 2 && stale_continuation == 0, "terminated thread restarts at entry, never old parked stack");
    CHECK((restart_order == std::vector<int>{1, 2}), "restart restores creation priority before runnable ordering");
    th.TerminateThread(c);
    th.DeleteThread(c);
    CHECK(!k.ThreadRegistered(static_cast<int>(tid)), "terminated thread can be deleted");
    th.StartThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::UnknownThid, "deleted thread cannot start");
    c.gpr[4] = 0;
    th.WakeupThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalThid, "wake zero rejected");
    th.SleepThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalContext, "host sleep cannot report success");
    c.gpr[4] = 0x1000;
    mem.Write<uint32_t>(0x1010, 127);
    th.CreateThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalPriority, "invalid create priority rejected");
    mem.Write<uint32_t>(0x1010, 20);
    mem.Write<uint32_t>(0x1008, 0x2001);
    th.CreateThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalEntry, "unaligned entry rejected");
    mem.Write<uint32_t>(0x1008, 0x3000);
    th.CreateThread(c);
    c.gpr[4] = c.gpr[2];
    th.StartThread(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::IllegalEntry, "unresolved compiled entry cannot report started");
}

static void TestInterruptNesting() {
    IopMemory mem;
    IntrmanModule intr(mem);
    IopContext c;
    c.gpr[4] = 0x1000;
    intr.CpuSuspendIntr(c);
    CHECK(c.gpr[2] == 0 && mem.Read<uint32_t>(0x1000) == 1, "outer suspend saves enabled state");
    c.gpr[4] = 0x1004;
    intr.CpuSuspendIntr(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::CpuDisabled && mem.Read<uint32_t>(0x1004) == 0,
          "nested suspend saves disabled state and returns KE_CPUDI");
    c.gpr[4] = mem.Read<uint32_t>(0x1004);
    intr.CpuResumeIntr(c);
    c.gpr[4] = 0;
    intr.CpuSuspendIntr(c);
    CHECK(static_cast<int32_t>(c.gpr[2]) == IopError::CpuDisabled,
          "restoring inner state does not enable interrupts");
    c.gpr[4] = mem.Read<uint32_t>(0x1000);
    intr.CpuResumeIntr(c);
    c.gpr[4] = 0;
    intr.CpuSuspendIntr(c);
    CHECK(c.gpr[2] == 0, "restoring outer state permits next suspend");
    CHECK(intr.SuspendCount() == 4 && intr.ResumeCount() == 2 && intr.NestedSuspendCount() == 2,
          "interrupt reachability counters include nesting");
}

static void TestHostOwnershipAndCensus() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    IopKernel& k = table.Kernel();
    std::atomic<bool> entered{false}, release{false};
    std::atomic<int> worker_errors{0};
    k.RegisterThread(1, 10);
    k.StartThread(1, [&] {
        entered.store(true, std::memory_order_release);
        for (int i = 0; i < 1000; ++i)
            if (Priority(th, 0, i % 2 ? 10 : 11) != 0) ++worker_errors;
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
    });
    std::thread dispatcher([&] { k.Dispatch(); });
    while (!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    CHECK(!k.OnKernelThread() && k.CurrentTid() == 0, "foreign host sees no guest continuation");
    IopContext host;
    host.gpr[4] = 100;
    th.DelayThread(host);
    CHECK(static_cast<int32_t>(host.gpr[2]) == IopError::IllegalContext, "host cannot park another host's running coroutine");
    uint64_t last_calls = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto counts = th.Count();
        CHECK(counts.priority_calls >= last_calls, "concurrent census is monotonic");
        last_calls = counts.priority_calls;
    }
    release.store(true, std::memory_order_release);
    dispatcher.join();
    const auto counts = th.Count();
    CHECK(counts.priority_calls == 1000 && counts.priority_changes == 1000 &&
          counts.priority_errors == 0 && counts.delay_host == 1 && worker_errors == 0,
          "observer counters survive concurrent reads and reflect completed operations");
}

static void TestConcurrentHostInjection() {
    IopModuleTable table(IopClock::Policy::Frame);
    IopMemory mem;
    ThbaseModule th(table, mem, {}, {});
    ThsemapModule sem(table, mem);
    IopKernel& k = table.Kernel();
    std::atomic<bool> polling{false}, expired{false};
    std::atomic<int> completed{0};
    k.RegisterSema(0, 0, 1);
    k.RegisterThread(1, 10);
    k.RegisterThread(2, 10);
    k.RegisterThread(3, 20);
    k.StartThread(1, [&] { k.SleepThread(); ++completed; });
    k.StartThread(2, [&] { if (k.WaitSema(0) == 0) ++completed; });
    k.StartThread(3, [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        polling.store(true, std::memory_order_release);
        while (completed.load() < 2) {
            if (std::chrono::steady_clock::now() >= deadline) {
                expired.store(true);
                break;
            }
            k.Yield();
        }
    });
    std::thread dispatcher([&] { k.Dispatch(); });
    while (!polling.load(std::memory_order_acquire)) std::this_thread::yield();
    IopContext host;
    host.gpr[4] = 1;
    th.WakeupThread(host);
    CHECK(host.gpr[2] == 0, "host wake accepted while dispatch lock is held");
    host.gpr[4] = 0;
    sem.SignalSema(host);
    CHECK(host.gpr[2] == 0, "host signal accepted while dispatch lock is held");
    dispatcher.join();
    CHECK(!expired && completed == 2, "foreign host injections unblock a polling dispatch before it returns");
    CHECK(k.UnparkedCount() == 0 && k.InjectedWakeErrors() == 0 && k.InjectedSignalErrors() == 0,
          "both injected handoffs complete without leaked readiness or delivery errors");
}

int main() {
    ps2r::platform::host::RegisterSelf();
    TestReadyRequeue();
    TestSelfAndPeerReschedule();
    TestWaiterRequeue();
    TestSleepingPriority();
    TestDelay();
    TestSemaphoreErrors();
    TestThreadLifecycle();
    TestInterruptNesting();
    TestHostOwnershipAndCensus();
    TestConcurrentHostInjection();
    std::printf("IopShortcutSemantics: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
