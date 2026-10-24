#include "engine/ee/intc/intc.h"

#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

#include <algorithm>
#include <vector>

Intc& Intc::Instance() {
    static Intc intc;
    return intc;
}

namespace {
// The registered controller (Register binds `this`; the queue applier is a
// plain function pointer, so the binding lives here, not in the singleton).
Intc* g_registered = nullptr;
void ApplyQueuedRaise(int cause) {
    if (g_registered) g_registered->RaiseInPlace(cause);
}
// MIPS: 32-bit values live sign-extended in the 64-bit GPR (the kernel's
// ISR trampoline loads them with lw/addiu).
uint64_t SignExtend32(uint32_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); }
}  // namespace

void Intc::Register(MmioRouter& router) {
    router.Register(kStatAddr, 0x20, *this, "INTC");
    SyscallTable::Instance().BindIntcMask(&mask_);
    g_registered = this;
    ThreadManager::Instance().SetIntcRaise(&ApplyQueuedRaise);
}

uint64_t Intc::Read(uint32_t addr, unsigned) {
    switch (addr & ~0x3u) {
        case kStatAddr: return Stat();
        case kMaskAddr: return mask_;
        default: return 0;
    }
}

void Intc::Write(uint32_t addr, unsigned, uint64_t value) {
    const uint32_t v = static_cast<uint32_t>(value);
    switch (addr & ~0x3u) {
        case kStatAddr: {   // W1C ack: count the set bits the guest clears itself
            const uint32_t was = stat_.fetch_and(~v, std::memory_order_acq_rel);
            for (int c = 0; c < kCount; ++c)
                if (((was & v) >> c) & 1u) ++guest_acked_[c];
            break;
        }
        case kMaskAddr: mask_ ^= v; break;                                       // toggle
        default: break;
    }
}

void Intc::RaiseInPlace(int cause) {
    if (cause < 0 || cause >= kCount) return;
    const uint32_t was = stat_.fetch_or(1u << cause, std::memory_order_acq_rel);
    ++raises_;
    if (!((was >> cause) & 1u)) ++raised_[cause];   // an edge; a re-raise of a set bit is not one
}

void Intc::Raise(int cause) {
    if (cause < 0 || cause >= kCount) return;
    EeEventQueue::Event ev;
    ev.kind = EeEventQueue::Event::kIntcRaise;
    ev.a = cause;
    if (EeEventQueue::Instance().RouteIfForeign(ev)) {
        queued_raises_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    RaiseInPlace(cause);
}

void Intc::RaiseDeferred(int cause) {
    if (cause < 0 || cause >= kCount) return;
    deferred_.fetch_or(1u << cause, std::memory_order_acq_rel);
    deferred_raises_.fetch_add(1, std::memory_order_relaxed);
    // A fully parked guest set has no observer: the no-op event's poke wakes
    // one park (the queue's policy names it) whose re-park's idle grant
    // collects; a holder collects at its own observation point.
    EeEventQueue::Event ev;
    ev.kind = EeEventQueue::Event::kDeferredLatched;
    EeEventQueue::Instance().Enqueue(ev);
}

uint32_t Intc::CollectDeferred() {
    if (!EeRunToken::Instance().CurrentThreadHoldsToken()) return 0;
    const uint32_t causes = deferred_.exchange(0, std::memory_order_acq_rel);
    if (causes == 0) return 0;
    deferred_collects_.fetch_add(1, std::memory_order_relaxed);
    for (int c = 0; c < kCount; ++c)
        if ((causes >> c) & 1u) RaiseInPlace(c);
    return causes;
}

bool Intc::Dispatch(CpuContext& ctx, int cause, int site) {
    if (cause < 0 || cause >= kCount) return false;
    const uint32_t bit = 1u << cause;
    if (!(Stat() & mask_ & bit)) return false;
    if (in_dispatch_) {
        ++nested_refusals_;
        if (site < 0 || site >= kSiteCount) site = kSiteOther;
        if (cause != in_dispatch_cause_) ++refusals_[site][in_dispatch_cause_][cause];
        return false;
    }
    in_dispatch_ = true;   // cleared below; no other return path follows
    in_dispatch_cause_ = cause;
    // Interrupt context for the run token: an i-syscall inside the handler only
    // records its preemption; the hand-over is at IsrEnd below, after the ack.
    EeRunToken::Instance().IsrBegin();
    bool ran = false;
    // Copy the records: a handler may add/remove handlers while it runs.
    const auto handlers = SyscallTable::Instance().IntcHandlers();
    for (const auto& h : handlers) {
        if (h.cause != cause || !h.active) continue;
        const uint32_t saved_pc = ctx.cpuRegs.pc;
        const auto saved_gpr = ctx.cpuRegs.GPR;
        ctx.cpuRegs.GPR.r[4].UD[0] = SignExtend32(h.arg);
        ctx.cpuRegs.GPR.r[28].UD[0] = SignExtend32(h.gp);
        ctx.cpuRegs.GPR.r[31].UD[0] = SignExtend32(kIsrReturnStub);
        if (guest_call_ && guest_call_(h.pc, ctx)) ran = true;
        ctx.cpuRegs.pc = saved_pc;
        ctx.cpuRegs.GPR = saved_gpr;
    }
    stat_.fetch_and(~bit, std::memory_order_acq_rel);  // ack this cause only
    ++dispatches_;
    ++dispatched_[cause];
    in_dispatch_ = false;
    in_dispatch_cause_ = -1;
    EeRunToken::Instance().IsrEnd();  // the handler's exit: a thread it readied runs now
    return ran;
}

int Intc::DispatchPending(CpuContext& ctx, int site) {
    if (in_dispatch_) {
        // One refusal per attempt (the counter's meaning is unchanged); the census
        // names every OTHER pending cause the attempt would have run. The cause in
        // dispatch keeps its own STAT bit until the ack at its exit, so it is always
        // "pending" here -- that is the handler in progress, not a refused edge.
        ++nested_refusals_;
        if (site < 0 || site >= kSiteCount) site = kSiteOther;
        const uint32_t pending = Stat() & mask_ & ~(1u << in_dispatch_cause_);
        for (int c = 0; c < kCount; ++c)
            if ((pending >> c) & 1u) ++refusals_[site][in_dispatch_cause_][c];
        return 0;
    }
    int n = 0;
    const uint32_t pending = Stat() & mask_;
    for (int c = 0; c < kCount; ++c)
        if ((pending >> c) & 1u) n += Dispatch(ctx, c, site) ? 1 : 0;
    return n;
}

const char* Intc::CauseName(int cause) {
    static const char* const kNames[kCount] = {"GS", "SBUS", "VBON", "VBOF", "VIF0", "VIF1", "VU0", "VU1",
                                               "IPU", "TIM0", "TIM1", "TIM2", "TIM3", "SFIFO", "VU0WD"};
    return (cause >= 0 && cause < kCount) ? kNames[cause] : "?";
}

const char* Intc::SiteName(int site) {
    static const char* const kNames[kSiteCount] = {"other", "boundary", "yield", "syscall", "timer"};
    return (site >= 0 && site < kSiteCount) ? kNames[site] : "?";
}

std::string Intc::RefusalCensus() const {
    struct Row { uint64_t n; int site, in, refused; };
    std::vector<Row> rows;
    for (int s = 0; s < kSiteCount; ++s)
        for (int i = 0; i < kCount; ++i)
            for (int r = 0; r < kCount; ++r)
                if (refusals_[s][i][r]) rows.push_back({refusals_[s][i][r], s, i, r});
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.n != b.n) return a.n > b.n;
        if (a.site != b.site) return a.site < b.site;
        return a.in != b.in ? a.in < b.in : a.refused < b.refused;
    });
    std::string out;
    for (const Row& r : rows) {
        if (!out.empty()) out += ',';
        out += std::string(SiteName(r.site)) + "/" + CauseName(r.in) + ">" + CauseName(r.refused) + "=" +
               std::to_string(r.n);
    }
    return out;
}

std::string Intc::EdgeCensus() const {
    std::string out;
    const uint32_t stat = Stat();
    for (int c = 0; c < kCount; ++c) {
        if (!raised_[c]) continue;
        if (!out.empty()) out += ',';
        out += std::string(CauseName(c)) + ":" + std::to_string(raised_[c]) + "/" + std::to_string(dispatched_[c]) +
               "/" + std::to_string(guest_acked_[c]) + "/" + std::to_string((stat >> c) & 1u);
    }
    return out;
}

void Intc::RestoreRegisters(uint32_t stat, uint32_t mask) {
    stat_.store(stat, std::memory_order_release);
    mask_ = mask;
}

void Intc::ResetForTest() {
    stat_.store(0);
    deferred_.store(0);
    mask_ = 0;
    raises_ = dispatches_ = 0;
    queued_raises_.store(0);
    deferred_raises_.store(0);
    deferred_collects_.store(0);
    in_dispatch_ = false;
    in_dispatch_cause_ = -1;
    nested_refusals_ = 0;
    for (int c = 0; c < kCount; ++c) raised_[c] = dispatched_[c] = guest_acked_[c] = 0;
    for (auto& site : refusals_)
        for (auto& row : site)
            for (auto& n : row) n = 0;
}
