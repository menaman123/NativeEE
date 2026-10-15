#include "engine/ee/savestate/thread_relauncher.h"

#include <cstdio>
#include <utility>

#include "engine/core/log/log.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"
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

int ThreadRelauncher::Relaunch(const std::vector<ThreadSection::Record>& records, std::string* err) {
    EeRunToken& token = EeRunToken::Instance();
    token.BeginRelaunch();
    int relaunched = 0;
    for (const ThreadSection::Record& rec : records) {
        if (!rec.started || !rec.has_ctx) continue;
        if (rec.chain.empty()) {
            if (err) *err = "tid " + std::to_string(rec.tid) + ": started with a context but an empty chain";
            token.EndRelaunch();
            return -1;
        }
        const ThreadSection::Record* keep = &rec;
        auto body = [this, keep](int, CpuContext& ctx) { WalkChain(*keep, ctx); };
        if (!threads_.StartResumed(rec.tid, rec.ctx, body)) {
            if (err) *err = "tid " + std::to_string(rec.tid) + ": resume worker could not be started";
            token.EndRelaunch();
            return -1;
        }
        // File order readies; the election below picks the thbase winner.
        token.MakeReady(rec.tid, rec.current_priority);
        PS2R_LOG(SAVE, Info, "relaunch tid=%d prio=%d status=%d chain=%zu innermost=0x%x%s", rec.tid,
                 rec.current_priority, rec.status, rec.chain.size(), rec.chain[0].func_addr,
                 rec.is_boundary ? " (boundary)" : "");
        relaunched++;
    }
    token.EndRelaunch();
    return relaunched;
}
