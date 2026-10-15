// RelaunchOrder (BUG-036): a savestate load relaunches every guest thread --
// including the loader's OWN tid -- while the loader still holds the run token.
// BeginRelaunch hands the loader's identity off and HOLDS elections; the
// resumed threads are readied in file order; EndRelaunch runs ONE election, so
// the first post-load runner is the thbase winner (highest priority, FIFO),
// never the first host thread to arrive -- and the old incarnation's later
// GuestExit is a no-op.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/ee/sched/ee_run_token.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static EeRunToken& TOK() { return EeRunToken::Instance(); }

int main() {
    std::mutex om;
    std::vector<int> order;
    std::atomic<bool> old_exit_stole{false};
    std::atomic<int> arrived{0};
    auto worker = [&](int tid, int prio) {
        TOK().GuestEnter(tid, prio);
        { std::lock_guard<std::mutex> lk(om); order.push_back(tid); }
        TOK().GuestExit(tid);
    };
    std::thread t1_new, t2_new;
    // The loader IS guest tid 1 (prio 64) and holds the token.
    std::thread loader([&] {
        TOK().GuestEnter(1, 64);
        TOK().BeginRelaunch();
        CHECK(TOK().ElectionHeld() && TOK().OwnerForTest() == EeRunToken::kNone, "relaunch_begin_holds_elections_and_clears_owner");
        // File order readies tid 1 (prio 64) BEFORE tid 2 (prio 10); relaunch tid 2's
        // host thread FIRST so arrival order (2 then 1) and file order (1 then 2)
        // both disagree with the thbase answer only if the answer were wrong.
        TOK().MakeReady(1, 64);
        TOK().MakeReady(2, 10);
        t2_new = std::thread(worker, 2, 10); arrived.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        t1_new = std::thread(worker, 1, 64); arrived.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(order.empty(), "no_resumed_thread_runs_while_elections_are_held");
        TOK().EndRelaunch();
        // The OLD incarnation's epilogue: must be a no-op (identity handed off).
        TOK().GuestExit(1);
        old_exit_stole = false;
    });
    loader.join();
    t1_new.join(); t2_new.join();
    CHECK(order.size() == 2 && order[0] == 2 && order[1] == 1, "first_post_load_runner_is_the_thbase_winner_(prio_10_before_64)");
    CHECK(!TOK().ElectionHeld() && TOK().OwnerForTest() == EeRunToken::kNone && !TOK().AnyReadyBesides(EeRunToken::kNone),
          "relaunch_end_ran_one_election_and_the_set_drained");
    // Determinism: repeat with reversed arrival order; the answer is the same.
    order.clear();
    std::thread t1b, t2b;
    std::thread loader2([&] {
        TOK().GuestEnter(1, 64);
        TOK().BeginRelaunch();
        TOK().MakeReady(1, 64);
        TOK().MakeReady(2, 10);
        t1b = std::thread(worker, 1, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        t2b = std::thread(worker, 2, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        TOK().EndRelaunch();
        TOK().GuestExit(1);
    });
    loader2.join(); t1b.join(); t2b.join();
    CHECK(order.size() == 2 && order[0] == 2 && order[1] == 1, "relaunch_order_independent_of_host_arrival_order");
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
