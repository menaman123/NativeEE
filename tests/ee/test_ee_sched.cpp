// EeRunTokenAndEventQueue: the run-token contract and the BUG-035 drain invariant.
//   1. SINGLE RUNNER: no instant with two holders.
//   2. SCHEDULING-POINT-ONLY switches: a holder that never yields is never
//      descheduled (no time-slicing).
//   3. thbase election: priority then FIFO seq; preemption; ChangeThreadPriority
//      re-enqueues at the tail; a READY tid whose host thread has exited is
//      never elected (BUG-040 -- the wedge reproduced deterministically).
//   4. Event queue: FIFO application, HasPending edge, park poke.
//   5. BUG-035 both halves: (a) non-holder drain is a counted no-op that loses
//      nothing; (b) applying never re-enqueues -- and the predicate is the ONE
//      helper every routing site calls (enumerated over the real mutators).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

using Event = EeEventQueue::Event;
static EeRunToken& TOK() { return EeRunToken::Instance(); }
static EeEventQueue& Q() { return EeEventQueue::Instance(); }

// Test-side applier model: a faithful copy of the mutators' routing -- a caller
// that may not mutate re-routes; otherwise the application is logged.
static std::mutex g_apply_mu;
static std::vector<Event> g_applied;
static std::map<int, int> g_apply_count;
static void InstallModelApplier() {
    Q().SetApplier([](const Event& ev) {
        if (Q().RouteIfForeign(ev)) return;
        std::lock_guard<std::mutex> lk(g_apply_mu);
        g_applied.push_back(ev);
        g_apply_count[ev.a]++;
    });
}
static void ClearLog() { std::lock_guard<std::mutex> lk(g_apply_mu); g_applied.clear(); g_apply_count.clear(); }

static void TestSingleRunner() {
    std::atomic<int> inside{0};
    std::atomic<bool> violation{false};
    std::atomic<long> switches{0};
    const int kThreads = 4, kIters = 400;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; t++)
        ts.emplace_back([&, t] {
            const int tid = 10 + t;
            TOK().GuestEnter(tid, 32);
            for (int i = 0; i < kIters; i++) {
                if (inside.fetch_add(1) != 0) violation = true;
                std::this_thread::yield();
                if (inside.fetch_sub(1) != 1) violation = true;
                switches.fetch_add(1);
                TOK().Rotate(32);
            }
            TOK().GuestExit(tid);
        });
    for (auto& t : ts) t.join();
    CHECK(!violation.load(), "single_runner_no_concurrent_holders");
    CHECK(switches.load() == kThreads * kIters, "all_quanta_ran");
}

static void TestNoAsyncDescheduling() {
    std::atomic<bool> lost{false}, other_ran{false};
    std::thread holder([&] {
        TOK().GuestEnter(20, 10);
        for (int i = 0; i < 200000; i++) {
            if (!TOK().CurrentThreadHoldsToken()) lost = true;
            if (other_ran.load(std::memory_order_relaxed)) lost = true;
        }
        TOK().GuestExit(20);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::thread comp([&] { TOK().GuestEnter(21, 90); other_ran = true; TOK().GuestExit(21); });
    holder.join(); comp.join();
    CHECK(!lost.load(), "no_deschedule_without_scheduling_point");
}

// Election rigs: the boss must HOLD the token before any worker is launched
// (BUG-040: a worker elected first exits, the late boss MakeReady's the exited
// tid, and a READY slot no host thread will attach wedges the token).
// Workers count themselves in before GuestEnter (they block inside it).
static void WaitUntil(const std::atomic<bool>& flag) { while (!flag.load()) std::this_thread::yield(); }

static void TestPriorityElection() {
    std::vector<int> order; std::mutex om; std::atomic<int> entered{0}; std::atomic<bool> boss_holds{false};
    std::thread boss([&] {
        TOK().GuestEnter(30, 5);
        TOK().MakeReady(31, 50); TOK().MakeReady(32, 20); TOK().MakeReady(33, 50);
        boss_holds = true;
        while (entered.load() < 3) std::this_thread::yield();
        TOK().GuestExit(30);
    });
    auto worker = [&](int tid, int prio) { entered.fetch_add(1); TOK().GuestEnter(tid, prio); { std::lock_guard<std::mutex> lk(om); order.push_back(tid); } TOK().GuestExit(tid); };
    WaitUntil(boss_holds);
    std::thread w33(worker, 33, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::thread w31(worker, 31, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::thread w32(worker, 32, 20);
    boss.join(); w31.join(); w32.join(); w33.join();
    CHECK(order.size() == 3 && order[0] == 32 && order[1] == 31 && order[2] == 33, "election_prio_then_fifo_seq");
}

static void TestPreemption() {
    std::atomic<bool> hi_ran{false}, hi_before{false}, lo_holds{false};
    std::thread lo([&] { TOK().GuestEnter(40, 60); lo_holds = true; TOK().MakeReady(41, 10); TOK().PreemptPoint(); hi_before = hi_ran.load(); TOK().GuestExit(40); });
    WaitUntil(lo_holds);
    std::thread hi([&] { TOK().GuestEnter(41, 10); hi_ran = true; TOK().GuestExit(41); });
    lo.join(); hi.join();
    CHECK(hi_before.load(), "preempt_point_hands_over_to_higher_priority");
}

static void TestChangePriorityTail() {
    std::vector<int> order; std::mutex om; std::atomic<int> entered{0}; std::atomic<bool> boss_holds{false};
    std::thread boss([&] {
        TOK().GuestEnter(50, 5);
        TOK().MakeReady(51, 30); TOK().MakeReady(52, 30); TOK().SetPriority(51, 30);
        boss_holds = true;
        while (entered.load() < 2) std::this_thread::yield();
        TOK().GuestExit(50);
    });
    auto worker = [&](int tid, int prio) { entered.fetch_add(1); TOK().GuestEnter(tid, prio); { std::lock_guard<std::mutex> lk(om); order.push_back(tid); } TOK().GuestExit(tid); };
    WaitUntil(boss_holds);
    std::thread w51(worker, 51, 30);
    std::thread w52(worker, 52, 30);
    boss.join(); w51.join(); w52.join();
    CHECK(order.size() == 2 && order[0] == 52 && order[1] == 51, "change_priority_moves_to_tail");
}

// BUG-040 regression, deterministic: tid 70's host thread has exited; the
// holder MakeReady's it (a READY tid with NO host thread) and exits. Before the
// fix the election picked 70 and the next GuestEnter parked forever.
static void TestRetiredTidCannotWedge() {
    std::thread gone([&] { TOK().GuestEnter(70, 50); TOK().GuestExit(70); });
    gone.join();
    const uint64_t noops = TOK().RetiredReadyNoops();
    std::thread boss([&] { TOK().GuestEnter(71, 5); TOK().MakeReady(70, 1); TOK().GuestExit(71); });
    boss.join();
    CHECK(TOK().RetiredReadyNoops() == noops + 1, "b040_makeready_of_exited_tid_is_counted_noop");
    CHECK(TOK().OwnerForTest() != 70, "b040_exited_tid_never_elected");
    std::atomic<bool> next_ran{false};
    std::thread next([&] { TOK().GuestEnter(72, 60); next_ran = true; TOK().GuestExit(72); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!next_ran.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool ran_in_time = next_ran.load();
    if (!ran_in_time) TOK().Shutdown();  // wedged: release the parkee so the run can report
    next.join();
    CHECK(ran_in_time, "b040_next_guest_enter_runs_after_exited_tid_made_ready");
    // A reused thread id re-attaches: its next GuestEnter un-retires it and runs.
    std::atomic<bool> again_ran{false};
    std::thread again([&] { TOK().GuestEnter(70, 50); again_ran = true; TOK().GuestExit(70); });
    again.join();
    CHECK(again_ran.load(), "b040_reused_tid_reattaches_on_guest_enter");
    if (!ran_in_time) TOK().ResetForTest();
}

static void TestEventQueueFifo() {
    ClearLog();
    CHECK(!Q().HasPending(), "queue_initially_empty");
    Q().Enqueue({Event::kSignalSema, 1, 0}); Q().Enqueue({Event::kIntcRaise, 2, 0}); Q().Enqueue({Event::kSetEventFlag, 3, 0xAB});
    CHECK(Q().HasPending(), "queue_pending_after_enqueue");
    Q().DrainOnToken();
    CHECK(!Q().HasPending(), "queue_empty_after_drain");
    std::lock_guard<std::mutex> lk(g_apply_mu);
    CHECK(g_applied.size() == 3 && g_applied[0].kind == Event::kSignalSema && g_applied[0].a == 1 && g_applied[1].kind == Event::kIntcRaise &&
              g_applied[1].a == 2 && g_applied[2].kind == Event::kSetEventFlag && g_applied[2].a == 3 && g_applied[2].b == 0xAB,
          "queue_fifo_order_preserved");
}

static void TestEventQueuePoke() {
    std::mutex m; std::condition_variable cv; std::atomic<bool> woke{false};
    std::thread parker([&] { EeEventQueue::ParkReg reg(&m, &cv); std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&] { return Q().HasPending(); }); woke = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!woke.load(), "parker_stays_parked_until_enqueue");
    Q().Enqueue({Event::kWakeupThread, 7, 0});
    parker.join();
    CHECK(woke.load(), "enqueue_pokes_registered_park");
    Q().DrainOnToken();
    ClearLog();
}

static void TestNonHolderDrainCountedNoop() {
    ClearLog();
    Q().Enqueue({Event::kSignalSema, 100, 0}); Q().Enqueue({Event::kWakeupThread, 101, 0});
    CHECK(Q().Size() == 2, "b035a_two_events_queued");
    const uint64_t before = Q().NonHolderDrainRequests();
    Q().DrainIfPendingOnHolder();  // this thread does NOT hold the token
    CHECK(Q().NonHolderDrainRequests() == before + 1, "b035a_nonholder_request_counted");
    CHECK(Q().Size() == 2, "b035a_no_event_lost_or_popped");
    { std::lock_guard<std::mutex> lk(g_apply_mu); CHECK(g_applied.empty(), "b035a_nothing_applied_off_token"); }
    std::thread holder([&] { TOK().GuestEnter(60, 10); Q().DrainIfPendingOnHolder(); TOK().GuestExit(60); });
    holder.join();
    CHECK(Q().Size() == 0, "b035a_holder_drained_queue");
    std::lock_guard<std::mutex> lk(g_apply_mu);
    CHECK(g_applied.size() == 2 && g_applied[0].a == 100 && g_applied[1].a == 101, "b035a_holder_applied_all_in_fifo_order");
}

static void TestApplyingNeverReenqueues() {
    ClearLog();
    Q().Enqueue({Event::kSignalSema, 200, 0}); Q().Enqueue({Event::kSetEventFlag, 201, 0x1}); Q().Enqueue({Event::kResumeThread, 202, 0});
    CHECK(Q().Size() == 3, "b035b_three_events_queued");
    std::atomic<bool> drained{false};
    std::thread nonholder([&] { Q().DrainOnToken(); drained = true; });  // deliberate guard bypass
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!drained.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!drained.load()) { std::printf("FAIL b035b_drain_terminates (re-enqueue spin!)\n"); std::fflush(stdout); std::_Exit(1); }
    nonholder.join();
    CHECK(true, "b035b_drain_terminates");
    CHECK(Q().Size() == 0, "b035b_queue_strictly_decreased_to_empty");
    std::lock_guard<std::mutex> lk(g_apply_mu);
    CHECK(g_applied.size() == 3 && g_apply_count[200] == 1 && g_apply_count[201] == 1 && g_apply_count[202] == 1,
          "b035b_each_event_applied_exactly_once_no_duplicates");
}

static void TestHolderAppliesExactlyOnce() {
    ClearLog();
    Q().Enqueue({Event::kWakeupThread, 300, 0});
    std::thread holder([&] { TOK().GuestEnter(61, 10); Q().DrainIfPendingOnHolder(); Q().DrainIfPendingOnHolder(); TOK().GuestExit(61); });
    holder.join();
    CHECK(Q().Size() == 0, "b035c_queue_empty_after_holder_drain");
    std::lock_guard<std::mutex> lk(g_apply_mu);
    CHECK(g_apply_count[300] == 1 && g_applied.size() == 1, "b035c_guest_state_mutated_exactly_once");
}

// The routing sites, enumerated: every wake-type mutator of the real kernel
// objects. For each: a foreign caller ENQUEUES (state untouched, queue +1);
// the holder's drain APPLIES it (state changed, queue back to 0, no re-enqueue).
static void TestRoutingSitesEnumerated() {
    ThreadManager& tm = ThreadManager::Instance();
    tm.ResetForTest();
    tm.InstallEventApplier();
    const int sid = Semaphores::Instance().Create({0, 4, 0, 0});
    const int eid = EventFlags::Instance().Create({0, 0, 0xF});
    auto sema_count = [&] { SemaStatus s{}; Semaphores::Instance().ReferStatus(sid, s); return s.count; };
    auto evf_bits = [&] { auto f = EventFlags::Instance().Lookup(eid); std::lock_guard<std::mutex> lk(f->m); return f->bits; };
    // A dormant-free target for the thread mutators: adopt tid 77 on a helper
    // thread that parks in SleepThread (so WakeupThread has a real sleeper).
    std::atomic<int> slept{-1};
    std::thread sleeper([&] { tm.AdoptCurrentThread(77, 40); TOK().GuestEnter(77, 40); slept = tm.SleepThread(); TOK().GuestExit(77); });
    for (;;) { auto i = tm.Lookup(77); if (i) { std::lock_guard<std::mutex> lk(i->m); if (i->status == THS_WAIT) break; } std::this_thread::yield(); }
    // tid 78 suspends ITSELF on its own guest thread (token released across the
    // park); the foreign ResumeThread below must route, and the drain applies it.
    std::thread suspended([&] { tm.AdoptCurrentThread(78, 40); TOK().GuestEnter(78, 40); tm.SuspendThread(78); TOK().GuestExit(78); });
    for (;;) { auto i = tm.Lookup(78); if (i) { std::lock_guard<std::mutex> lk(i->m); if (i->status == THS_SUSPEND) break; } std::this_thread::yield(); }
    // A BUSY holder (tid 79, prio 1) runs guest code during the enumeration: the
    // pokes wake the parked waiters but nobody can acquire, so the queue
    // accumulates -- the interrupt-arrives-mid-run picture. Its next scheduling
    // point (DrainIfPendingOnHolder) delivers every event in place.
    std::atomic<bool> held{false}, go{false}, may{false};
    std::thread holder([&] {
        TOK().GuestEnter(79, 1);
        held = true;
        while (!go.load()) std::this_thread::yield();
        may = EeEventQueue::CallerMayMutate();
        Q().DrainIfPendingOnHolder();
        TOK().GuestExit(79);
    });
    while (!held.load()) std::this_thread::yield();
    // Site 1..6: SignalSema, SetEventFlag, ClearEventFlag, WakeupThread,
    // ResumeThread, ReleaseWaitThread (target 77 is READY by then -> KE_NOT_WAIT, no mutation).
    struct Site { const char* name; std::function<int()> call; std::function<bool()> untouched; };
    const uint32_t evf0 = evf_bits();
    std::vector<Site> sites = {
        {"SignalSema", [&] { return Semaphores::Instance().Signal(sid); }, [&] { return sema_count() == 0; }},
        {"SetEventFlag", [&] { return EventFlags::Instance().Set(eid, 0x10); }, [&] { return evf_bits() == evf0; }},
        {"ClearEventFlag", [&] { return EventFlags::Instance().Clear(eid, 0x1); }, [&] { return evf_bits() == evf0; }},
        {"WakeupThread", [&] { return tm.WakeupThread(77); }, [&] { auto i = tm.Lookup(77); std::lock_guard<std::mutex> lk(i->m); return i->status == THS_WAIT; }},
        {"ResumeThread", [&] { return tm.ResumeThread(78); }, [&] { auto i = tm.Lookup(78); std::lock_guard<std::mutex> lk(i->m); return i->suspend_count == 1; }},
        {"ReleaseWaitThread", [&] { return tm.ReleaseWaitThread(77); }, [&] { return true; }},
    };
    bool all_routed = true;
    size_t expect = 0;
    for (auto& s : sites) {
        const int rc = s.call();  // this thread holds no token and is not applying -> must route
        const bool routed = rc == KE_OK && Q().Size() == ++expect && s.untouched();
        if (!routed) std::printf("  site %s not routed (rc=%d size=%zu)\n", s.name, rc, Q().Size());
        all_routed &= routed;
    }
    CHECK(all_routed, "b035d_every_routing_site_enqueues_for_a_foreign_caller (6 sites)");
    CHECK(!EeEventQueue::CallerMayMutate() && !EeEventQueue::ApplyingOnThisThread(), "b035d_predicate_false_off_token");
    // The holder reaches its scheduling point: every site applies in place; nothing is re-enqueued.
    go = true;
    holder.join();
    sleeper.join();
    suspended.join();
    CHECK(may.load(), "b035d_predicate_true_for_the_holder");
    CHECK(Q().Size() == 0 && sema_count() == 1 && evf_bits() == ((evf0 | 0x10) & 0x1) && slept.load() == KE_OK,
          "b035d_holder_drain_applied_all_sites_in_place_queue_empty");
    { auto i = tm.Lookup(78); std::lock_guard<std::mutex> lk(i->m); CHECK(i->suspend_count == 0, "b035d_resume_applied_by_drain"); }
    Semaphores::Instance().Delete(sid);
    EventFlags::Instance().Delete(eid);
    tm.ResetForTest();
    InstallModelApplier();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    InstallModelApplier();
    TestSingleRunner();
    TestNoAsyncDescheduling();
    TestPriorityElection();
    TestPreemption();
    TestChangePriorityTail();
    TestRetiredTidCannotWedge();
    TestEventQueueFifo();
    TestEventQueuePoke();
    TestNonHolderDrainCountedNoop();
    TestApplyingNeverReenqueues();
    TestHolderAppliesExactlyOnce();
    TestRoutingSitesEnumerated();
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
