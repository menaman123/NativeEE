#include "engine/ee/intc/intc.h"

#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

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
        case kStatAddr: stat_.fetch_and(~v, std::memory_order_acq_rel); break;  // W1C ack
        case kMaskAddr: mask_ ^= v; break;                                       // toggle
        default: break;
    }
}

void Intc::RaiseInPlace(int cause) {
    if (cause < 0 || cause >= kCount) return;
    stat_.fetch_or(1u << cause, std::memory_order_acq_rel);
    ++raises_;
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

bool Intc::Dispatch(CpuContext& ctx, int cause) {
    if (cause < 0 || cause >= kCount) return false;
    const uint32_t bit = 1u << cause;
    if (!(Stat() & mask_ & bit)) return false;
    if (in_dispatch_) {
        ++nested_refusals_;
        return false;
    }
    in_dispatch_ = true;   // cleared below; no other return path follows
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
    in_dispatch_ = false;
    EeRunToken::Instance().IsrEnd();  // the handler's exit: a thread it readied runs now
    return ran;
}

int Intc::DispatchPending(CpuContext& ctx) {
    if (in_dispatch_) {
        ++nested_refusals_;
        return 0;
    }
    int n = 0;
    const uint32_t pending = Stat() & mask_;
    for (int c = 0; c < kCount; ++c)
        if ((pending >> c) & 1u) n += Dispatch(ctx, c) ? 1 : 0;
    return n;
}

void Intc::RestoreRegisters(uint32_t stat, uint32_t mask) {
    stat_.store(stat, std::memory_order_release);
    mask_ = mask;
}

void Intc::ResetForTest() {
    stat_.store(0);
    mask_ = 0;
    raises_ = dispatches_ = 0;
    queued_raises_.store(0);
    in_dispatch_ = false;
    nested_refusals_ = 0;
}
