#include "engine/ee/savestate/thread_relauncher.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <cstdio>
#include <utility>

#include "engine/core/log/log.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "engine/ee/savestate/thread_capture.h"

ThreadRelauncher::ThreadRelauncher(ThreadManager& threads, EnterFn enter)
    : threads_(threads), enter_(std::move(enter)) {}

void ThreadRelauncher::WalkChain(const ThreadSection::Record& record, CpuContext& ctx) const {
    const auto& chain = record.chain;
    for (size_t k = 0; k < chain.size(); k++) {
        if (auto info = threads_.Lookup(record.tid)) threads_.ThrowIfTerminated(info);
        const uint32_t fn = chain[k].func_addr;
        // Level 0 enters at the boundary continuation (ctx.$ra) or the function
        // start; a deeper level re-enters at the previous frame's ra, its
        // post-call continuation.
        const uint32_t enter = (k == 0) ? (record.is_boundary ? ctx.cpuRegs.GPR.r[31].UL[0] : 0u) : chain[k - 1].ra;
        ThreadCapture::ResumeScope resume_scope(chain, k);
        if (!enter_(fn, enter, ctx)) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "tid=%d level=%zu enter=0x%x", record.tid, k, enter);
            GuestPark::Forever("resume_no_function", fn, detail);
        }
    }
    // Outermost frame returned: the thread ends like ExitThread (RunGuestThread's tail).
}

namespace {
std::atomic<uint64_t> g_unparked_waiters{0};
}  // namespace

uint64_t ThreadRelauncher::UnparkedWaiters() { return g_unparked_waiters.load(std::memory_order_relaxed); }

int ThreadRelauncher::Relaunch(const std::vector<ThreadSection::Record>& records, std::string* err) {
    EeRunToken& token = EeRunToken::Instance();
    auto start = [&](const ThreadSection::Record& rec) -> bool {
        const ThreadSection::Record* keep = &rec;
        auto body = [this, keep](int, CpuContext& ctx) { WalkChain(*keep, ctx); };
        if (!threads_.StartResumed(rec.tid, rec.ctx, body)) {
            if (err) *err = "tid " + std::to_string(rec.tid) + ": resume worker could not be started";
            return false;
        }
        // File order readies; the election picks the thbase winner.
        token.MakeReady(rec.tid, rec.current_priority);
        PS2R_LOG(SAVE, Info, "relaunch tid=%d prio=%d status=%d chain=%zu innermost=0x%x%s", rec.tid,
                 rec.current_priority, rec.status, rec.chain.size(), rec.chain[0].func_addr,
                 rec.is_boundary ? " (boundary)" : "");
        return true;
    };
    for (const ThreadSection::Record& rec : records) {
        if (!rec.started || !rec.has_ctx) continue;
        if (rec.chain.empty()) {
            if (err) *err = "tid " + std::to_string(rec.tid) + ": started with a context but an empty chain";
            return -1;
        }
    }
    // Phase 1: the waiters re-park quietly. Their re-executed wait bodies run on a
    // full back-edge budget: the process's residual (the boot's, or the unwound
    // session's) must not trip a field under a re-parking waiter.
    EeYieldPoint::Instance().ReloadBudget();
    token.BeginRelaunch();
    token.SetRelaunchQuiet(true);
    int relaunched = 0;
    std::vector<int> waiters;
    for (const ThreadSection::Record& rec : records) {
        if (!rec.started || !rec.has_ctx || rec.is_boundary) continue;
        if (!start(rec)) { token.SetRelaunchQuiet(false); token.EndRelaunch(); return -1; }
        waiters.push_back(rec.tid);
        relaunched++;
    }
    token.EndRelaunch();
    // Every waiter parked (its wait body re-executed to NotePark) and the token released: bounded wait.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        bool all = token.OwnerTid() == EeRunToken::kNone;
        for (const int tid : waiters) {
            const auto info = threads_.Lookup(tid);
            if (!info) { all = false; break; }
            std::lock_guard<std::mutex> lk(info->m);
            if (!info->capture_parked || info->status != THS_WAIT) { all = false; break; }
        }
        if (all) break;
        if (std::chrono::steady_clock::now() > deadline) {
            g_unparked_waiters.fetch_add(1, std::memory_order_relaxed);
            PS2R_LOG(SAVE, Warn, "relaunch: a waiter did not park within 10 s; the boundary thread starts anyway");
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    // The back-edge budget the save saw (the YLDP section, staged by its restore) is
    // seated now: the waiters' re-executed wait bodies have parked (their back-edges
    // were never in the saved run), and the boundary thread -- the saved run's
    // holder -- is the next guest code to run. Nothing staged = a pre-v5 image.
    if (EeYieldPoint::Instance().ApplyRestoredBudget())
        PS2R_LOG(SAVE, Info, "relaunch: back-edge budget remaining re-seated at %u",
                 EeYieldPoint::Instance().Stats().last_restored_remaining);
    // Phase 2: the boundary thread, alone, as the save saw it.
    token.BeginRelaunch();
    token.SetRelaunchQuiet(false);
    for (const ThreadSection::Record& rec : records) {
        if (!rec.started || !rec.has_ctx || !rec.is_boundary) continue;
        if (!start(rec)) { token.EndRelaunch(); return -1; }
        relaunched++;
    }
    token.EndRelaunch();
    return relaunched;
}
