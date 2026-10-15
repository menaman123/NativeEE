// ThreadManagerThbase: the kernel objects over the run token.
//  Part A ports the OLD r139 kernel checks (T3 self-wakeup banking, T4 sema
//  signal-before-wait banking, T5 two-waiter conservation, T6 event-flag wait),
//  under the BUG-035 token model: a thread modelling a RUNNING guest thread
//  holds the run token (GuestEnter/GuestExit); a HOST-side signaller calls
//  the mutator without it and the parked guest waiter drains the queued wake.
//  Part B pins the thbase rules: priority/ready-queue order through the real
//  StartThread path, sema FIFO vs priority release, the event-flag truth table
//  incl. the 0x10 clear-on-wait rule, and alarms firing only at a boundary.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

using Clock = std::chrono::steady_clock;
static int g_fail = 0;
#define CHECK(cond, msg)                        \
    do {                                        \
        if (cond) std::printf("  PASS: %s\n", msg); \
        else { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    } while (0)

static constexpr int kTestPrio = 64;
static ThreadManager& TM() { return ThreadManager::Instance(); }
static EeRunToken& TOK() { return EeRunToken::Instance(); }
// The calling guest thread releases the token (hardware-park analog) until pred()
// holds, so lower-priority guest threads can run their enrolment.
template <class Pred>
static void YieldUntil(Pred pred) {
    EeRunToken::ParkScope park;
    while (!pred()) std::this_thread::yield();
}

// --- T3: self-wakeup banking (R4) --------------------------------------------
static void TestSelfWakeupBanking() {
    std::printf("[T3] self-wakeup banking (WakeupThread(GetThreadId()) -> SleepThread)\n");
    std::atomic<int> wk_rc{-9999}, slp_rc{-9999};
    std::atomic<bool> done{false};
    std::shared_ptr<ThreadInfo> info;
    std::thread worker([&] {
        info = TM().AdoptCurrentThread(42, kTestPrio);
        TOK().GuestEnter(42, kTestPrio);
        wk_rc = TM().WakeupThread(42);   // bank BEFORE deciding to sleep
        slp_rc = TM().SleepThread();     // must consume the bank
        TOK().GuestExit(42);
        done = true;
    });
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!done.load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool woke_alone = done.load();
    if (!woke_alone) { EeRunToken::HostGate g; TM().ReleaseWaitThread(42); }
    worker.join();
    CHECK(wk_rc.load() == KE_OK, "T3a: WakeupThread(self) returns KE_OK (banked, not rejected)");
    CHECK(woke_alone, "T3b: SleepThread returned without external help (no lost wakeup)");
    CHECK(slp_rc.load() == KE_OK, "T3c: SleepThread consumed the banked wakeup (KE_OK)");
    { std::lock_guard<std::mutex> lk(info->m); CHECK(info->wakeup_count == 0, "T3d: wakeup_count fully consumed"); }
    TM().ResetForTest();
}

// --- T4: sema signal-before-wait banking (S2/S3) -----------------------------
static void TestSemaSignalBeforeWait() {
    std::printf("[T4] sema signal-before-wait banking\n");
    TM().AdoptCurrentThread(1, kTestPrio);
    TOK().GuestEnter(1, kTestPrio);
    const int sid = Semaphores::Instance().Create({0, 1, 0, 0});
    CHECK(sid > 0, "T4a: CreateSema ok");
    CHECK(Semaphores::Instance().Signal(sid) == KE_OK, "T4b: SignalSema (no waiter) returns KE_OK");
    const auto t0 = Clock::now();
    const int rc = Semaphores::Instance().Wait(sid);
    CHECK(rc == KE_OK, "T4c: WaitSema consumed the banked signal (KE_OK)");
    CHECK(Clock::now() - t0 < std::chrono::milliseconds(100), "T4d: WaitSema returned immediately (signal banked, not lost)");
    Semaphores::Instance().Delete(sid);
    TOK().GuestExit(1);
    TM().ResetForTest();
}

// --- T5: two-waiter conservation ------------------------------------------------
static void TestTwoWaiterConservation() {
    std::printf("[T5] two-waiter signal conservation\n");
    TM().AdoptCurrentThread(1, kTestPrio);
    TOK().GuestEnter(1, kTestPrio);
    const int sid = Semaphores::Instance().Create({0, 10, 0, 0});
    CHECK(sid > 0, "T5a: CreateSema ok");
    constexpr int kPerWaiter = 50, kTotal = 2 * kPerWaiter;
    std::atomic<int> completed{0};
    auto waiter = [&](int tid) {
        TM().AdoptCurrentThread(tid, kTestPrio);
        TOK().GuestEnter(tid, kTestPrio);
        for (int i = 0; i < kPerWaiter; i++)
            if (Semaphores::Instance().Wait(sid) == KE_OK) completed.fetch_add(1);
        TOK().GuestExit(tid);
    };
    std::thread w1(waiter, 21), w2(waiter, 22);
    int delivered = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (delivered < kTotal && Clock::now() < deadline) {
        if (Semaphores::Instance().Signal(sid) == KE_OK) delivered++;
        else TM().RotateThreadReadyQueue(kTestPrio);  // KE_SEMA_OVF: yield to same-priority waiters
    }
    TOK().GuestExit(1);
    w1.join(); w2.join();
    CHECK(delivered == kTotal, "T5b: all signals delivered (no OVF livelock)");
    CHECK(completed.load() == kTotal, "T5c: every delivered signal released exactly one wait (conservation)");
    Semaphores::Instance().Delete(sid);
    TM().ResetForTest();
}

// --- T6: host-side SetEventFlag wakes a parked guest waiter through the queue --
static void TestEventFlagHostSignal() {
    std::printf("[T6] waitEventFlag released by a queued host-side set\n");
    const int eid = EventFlags::Instance().Create({0, 0, 0});
    CHECK(eid > 0, "T6a: CreateEventFlag ok");
    std::atomic<int> rc{-9999};
    std::atomic<uint64_t> blocked_ns{0};
    std::thread waiter([&] {
        TM().AdoptCurrentThread(11, kTestPrio);
        TOK().GuestEnter(11, kTestPrio);
        const auto t0 = Clock::now();
        uint32_t res = 0;
        rc = EventFlags::Instance().Wait(eid, 0x1, WEF_OR, &res);
        blocked_ns = static_cast<uint64_t>((Clock::now() - t0).count());
        TOK().GuestExit(11);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const size_t before = EeEventQueue::Instance().Size();
    EventFlags::Instance().Set(eid, 0x1);  // HOST caller: routed, not applied here
    waiter.join();
    CHECK(before == 0 && rc.load() == KE_OK, "T6b: the host set was queued and the parked waiter drained it (KE_OK)");
    CHECK(blocked_ns.load() >= 100ull * 1000 * 1000, "T6c: waiter blocked >= 100 ms (a real park, token released)");
    CHECK(EeEventQueue::Instance().Size() == 0, "T6d: queue empty after the holder's drain");
    EventFlags::Instance().Delete(eid);
    TM().ResetForTest();
}

// --- B1: thbase ready-queue order through StartThread (R1/R3) --------------------
static void TestStartThreadPriorityOrder() {
    std::printf("[B1] StartThread: highest priority first, FIFO within a priority\n");
    std::mutex om;
    std::vector<int> order;
    TM().SetStackBytes(static_cast<size_t>(8) * 1024 * 1024);
    TM().SetGuestBody([&](int tid, CpuContext&) { std::lock_guard<std::mutex> lk(om); order.push_back(tid); });
    TM().AdoptCurrentThread(1, 5);
    TOK().GuestEnter(1, 5);
    ThreadParam lo{}; lo.entry = 0x100000; lo.priority = 50;
    ThreadParam hi{}; hi.entry = 0x100000; hi.priority = 20;
    const int a = TM().CreateThread(lo), b = TM().CreateThread(hi), c = TM().CreateThread(lo);
    CHECK(a == 2 && b == 3 && c == 4, "B1a: thread ids allocate from 2 (id 1 = main)");
    CHECK(TM().StartThread(a, 0, 0x1ff000, 0) == KE_OK && TM().StartThread(b, 0, 0x1ff000, 0) == KE_OK &&
              TM().StartThread(c, 0, 0x1ff000, 0) == KE_OK,
          "B1b: StartThread x3 KE_OK (caller prio 5 is never preempted)");
    CHECK(TM().StartThread(a, 0, 0, 0) == KE_NOT_DORMANT, "B1c: StartThread on a READY thread is KE_NOT_DORMANT");
    ThreadStatus st{};
    CHECK(TM().ReferThreadStatus(b, st) == b && st.status == THS_READY && st.current_priority == 20,
          "B1d: ReferThreadStatus READY / current priority 20");
    TOK().GuestExit(1);  // release: b (prio 20) runs, then a, then c (FIFO at 50)
    TM().JoinAll();
    const bool ok = order.size() == 3 && order[0] == b && order[1] == a && order[2] == c;
    CHECK(ok, "B1e: run order = highest priority, then FIFO within priority (b, a, c)");
    CHECK(TM().ReferThreadStatus(a, st) == a && st.status == THS_DORMANT, "B1f: a finished thread is DORMANT");
    CHECK(TM().DeleteThread(a) == KE_OK && TM().DeleteThread(a) == KE_UNKNOWN_THID, "B1g: DeleteThread DORMANT ok, then unknown");
    ThreadParam z{}; z.priority = 0; const int zt = TM().CreateThread(z);
    ThreadParam big{}; big.priority = 500; const int bt = TM().CreateThread(big);
    CHECK(TM().ReferThreadStatus(zt, st) == zt && st.initial_priority == 1 && TM().ReferThreadStatus(bt, st) == bt &&
              st.initial_priority == 127,
          "B1h: priority 0 clamps to 1, >127 clamps to 127 (R10)");
    TM().SetGuestBody(nullptr);
    TM().ResetForTest();
}

// --- B2: sema release order FIFO vs SA_THPRI (S2) ----------------------------------
static int WaitqCount(int sid) {
    auto s = Semaphores::Instance().Lookup(sid);
    std::lock_guard<std::mutex> lk(s->m);
    return static_cast<int>(s->waitq.size());
}
static int WaitqFrontTid(int sid) {
    auto s = Semaphores::Instance().Lookup(sid);
    std::lock_guard<std::mutex> lk(s->m);
    return s->waitq.empty() ? 0 : s->waitq.front().tid;
}
static void TestSemaFifoVsPriority() {
    std::printf("[B2] SignalSema releases FIFO by default, by priority under SA_THPRI\n");
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t attr = pass ? SA_THPRI : 0;
        TM().AdoptCurrentThread(1, 1);
        TOK().GuestEnter(1, 1);
        const int sid = Semaphores::Instance().Create({attr, 10, 0, 0});
        auto waiter = [&](int tid, int prio) {
            TM().AdoptCurrentThread(tid, prio);
            TOK().GuestEnter(tid, prio);
            Semaphores::Instance().Wait(sid);
            TOK().GuestExit(tid);
        };
        // Enrol in a fixed order: tid 30 (prio 60) first, then tid 31 (prio 20).
        std::thread w30(waiter, 30, 60);
        YieldUntil([&] { return WaitqCount(sid) == 1; });
        std::thread w31(waiter, 31, 20);
        YieldUntil([&] { return WaitqCount(sid) == 2; });
        Semaphores::Instance().Signal(sid);  // releases the head: the OTHER stays enrolled
        const int remaining = WaitqFrontTid(sid);
        Semaphores::Instance().Signal(sid);
        TOK().GuestExit(1);
        w30.join(); w31.join();
        CHECK(pass ? remaining == 30 : remaining == 31,
              pass ? "B2b: SA_THPRI releases the highest-priority waiter first (31; 30 stays)"
                   : "B2a: default attr releases FIFO (30 first; 31 stays)");
        Semaphores::Instance().Delete(sid);
        TM().ResetForTest();
    }
    TM().AdoptCurrentThread(1, 1);
    TOK().GuestEnter(1, 1);
    const int sid = Semaphores::Instance().Create({0, 1, 1, 0});
    CHECK(Semaphores::Instance().Signal(sid) == KE_SEMA_OVF, "B2c: Signal at max_count returns KE_SEMA_OVF");
    CHECK(Semaphores::Instance().Poll(sid) == KE_OK && Semaphores::Instance().Poll(sid) == KE_SEMA_ZERO,
          "B2d: Poll consumes then KE_SEMA_ZERO");
    CHECK(Semaphores::Instance().Signal(999) == KE_UNKNOWN_SEMID, "B2e: unknown sema id");
    Semaphores::Instance().Delete(sid);
    TOK().GuestExit(1);
    TM().ResetForTest();
}

// --- B3: event-flag truth table incl. the 0x10 clear rule (E2/E5) ---------------
static void TestEventFlagTruthTable() {
    std::printf("[B3] event-flag truth table\n");
    using EF = EventFlags;
    bool ok = EF::Satisfied(0x5, 0x1, WEF_OR) && EF::Satisfied(0x5, 0x3, WEF_OR) && !EF::Satisfied(0x5, 0x2, WEF_OR) &&
              EF::Satisfied(0x5, 0x5, WEF_AND) && !EF::Satisfied(0x5, 0x3, WEF_AND) && !EF::Satisfied(0x0, 0x1, WEF_OR);
    CHECK(ok, "B3a: OR = any waited bit set; AND = all waited bits set");
    ok = EF::AfterRelease(0x7, 0x1, WEF_OR) == 0x7 && EF::AfterRelease(0x7, 0x1, WEF_OR | WEF_CLEAR) == 0x6 &&
         EF::AfterRelease(0x7, 0x3, WEF_AND | WEF_CLEAR) == 0x4 && EF::AfterRelease(0x7, 0x1, WEF_OR | WEF_CLEAR_ALL) == 0x0;
    CHECK(ok, "B3b: no mode keeps the pattern; 0x10 clears the WAITED bits; 0x20 clears all");
    TM().AdoptCurrentThread(1, 1);
    TOK().GuestEnter(1, 1);
    const int eid = EventFlags::Instance().Create({0, 0, 0x7});
    uint32_t res = 0;
    CHECK(EventFlags::Instance().Poll(eid, 0x1, WEF_OR | WEF_CLEAR, &res) == KE_OK && res == 0x7, "B3c: Poll returns the pre-clear pattern");
    CHECK(EventFlags::Instance().Poll(eid, 0x1, WEF_OR, &res) == KE_EVF_COND, "B3d: bit 0 cleared by the previous 0x10 wait -> KE_EVF_COND");
    CHECK(EventFlags::Instance().Poll(eid, 0x6, WEF_AND, &res) == KE_OK && res == 0x6, "B3e: remaining bits satisfy AND");
    CHECK(EventFlags::Instance().Wait(eid, 0x0, WEF_OR, &res) == KE_EVF_ILPAT && EventFlags::Instance().Wait(eid, 0x1, 0x40, &res) == KE_ILLEGAL_MODE,
          "B3f: bits 0 -> KE_EVF_ILPAT; unknown mode bit -> KE_ILLEGAL_MODE");
    CHECK(EventFlags::Instance().Clear(eid, 0x2) == KE_OK && EventFlags::Instance().Poll(eid, 0x4, WEF_OR, &res) == KE_EVF_COND,
          "B3g: ClearEventFlag keeps only the masked bits");
    // Multiple waiters: attr without EA_MULTI refuses a second parked waiter.
    std::thread w([&] {
        TM().AdoptCurrentThread(12, 64);
        TOK().GuestEnter(12, 64);
        EventFlags::Instance().Wait(eid, 0x8, WEF_OR, nullptr);
        TOK().GuestExit(12);
    });
    YieldUntil([&] { auto f = EventFlags::Instance().Lookup(eid); std::lock_guard<std::mutex> lk(f->m); return f->waiters == 1; });
    CHECK(EventFlags::Instance().Poll(eid, 0x8, WEF_OR, &res) == KE_EVF_MULTI, "B3h: second waiter without EA_MULTI -> KE_EVF_MULTI");
    EventFlags::Instance().Set(eid, 0x8);
    TOK().GuestExit(1);
    w.join();
    EventFlags::Instance().Delete(eid);
    TM().ResetForTest();
}

// --- B4: alarms fire only at a boundary step ---------------------------------------
static void TestAlarmBoundary() {
    std::printf("[B4] alarms are tick-domain and fire only at FireDue\n");
    Alarms& al = Alarms::Instance();
    std::vector<int> fired;
    const int id1 = al.SetAlarm(100, 3, [&](int id, uint32_t, uint32_t) { fired.push_back(id); }, 0);
    const int id2 = al.SetAlarm(100, 1, [&](int id, uint32_t, uint32_t) { fired.push_back(id); }, 0);
    const int id3 = al.SetAlarm(100, 3, [&](int id, uint32_t, uint32_t) { fired.push_back(id); }, 0);
    CHECK(al.ArmedCount() == 3 && fired.empty(), "B4a: SetAlarm arms, nothing fires between boundaries");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(fired.empty(), "B4b: wall time passing fires nothing (no wall timer)");
    CHECK(al.FireDue(100) == 0 && al.FireDue(101) == 1 && fired.size() == 1 && fired[0] == id2, "B4c: boundary at tick 101 fires only the due alarm");
    CHECK(al.ReleaseAlarm(id3) == KE_OK && al.ReleaseAlarm(id3) == KE_ERROR, "B4d: ReleaseAlarm cancels once, KE_ERROR after");
    CHECK(al.FireDue(103) == 1 && fired.size() == 2 && fired[1] == id1 && al.ArmedCount() == 0, "B4e: the remaining alarm fires at its deadline; the released one never does");
    al.ResetForTest();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("ThreadManagerThbase\n");
    TestSelfWakeupBanking();
    TestSemaSignalBeforeWait();
    TestTwoWaiterConservation();
    TestEventFlagHostSignal();
    TestStartThreadPriorityOrder();
    TestSemaFifoVsPriority();
    TestEventFlagTruthTable();
    TestAlarmBoundary();
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
