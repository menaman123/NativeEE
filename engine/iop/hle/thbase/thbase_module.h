// engine/iop/hle/thbase/thbase_module.h -- ThbaseModule: the IOP kernel's
// thbase library as the recompiled module sees it, routed to IopKernel:
// CreateThread (idx 4), DeleteThread (5), StartThread (6), ExitDeleteThread
// (9), TerminateThread (10), ChangeThreadPriority (14),
// RotateThreadReadyQueue (16), GetThreadId (20), SleepThread (24),
// WakeupThread (25), iWakeupThread (26), DelayThread (33), USec2SysClock (39).
// Arguments in $a0..$a3, result in $v0. A started thread's body runs the
// recompiled function at the ThreadParam entry on a fresh IopContext whose
// $sp/$gp come from the title's ThreadLayout (data, not engine constants) --
// or, with a stack allocator bound (SetStackAllocator), from a stack of the
// ThreadParam's own size taken from the sysmem heap as the console's
// CreateThread does, and the CREATING thread's $gp (several hosted modules,
// each with its own $gp) when the layout's gp is 0.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class ThbaseModule {
public:
    using GuestFunction = std::function<void(IopContext&)>;
    // Resolves a guest code address to its recompiled body (empty = unknown).
    using FunctionResolver = std::function<GuestFunction(uint32_t addr)>;

    // Per-thread stack: $sp = stack_top - tid * stack_stride - 0x10; $gp fixed.
    struct ThreadLayout {
        uint32_t stack_top = 0;
        uint32_t stack_stride = 0;
        uint32_t gp = 0;
    };

    static constexpr int kMaxThreads = 32;
    static constexpr uint32_t kSysClockPerUsec = 37;   // USec2SysClock: 36.864 MHz rounded up

    // Observer-only totals, excluded from savestates. Count() is race-free
    // during realtime dispatch; individual fields may straddle a call.
    struct Counters {
        uint64_t priority_calls = 0;
        uint64_t priority_changes = 0;
        uint64_t priority_errors = 0;
        uint64_t delay_kernel = 0;
        uint64_t delay_host = 0;
        uint64_t injected_wake_errors = 0;
        uint64_t injected_signal_errors = 0;
    };
    Counters Count() const;

    ThbaseModule(IopModuleTable& table, IopMemory& mem, FunctionResolver resolve, ThreadLayout layout);

    // Per-thread stacks from the heap (the console's shape): alloc(size) -> base
    // address (0 = exhausted), free(base). Unbound: the ThreadLayout band formula.
    using StackAlloc = std::function<uint32_t(uint32_t size)>;
    using StackFree = std::function<void(uint32_t addr)>;
    void SetStackAllocator(StackAlloc alloc, StackFree free);

    // int CreateThread(ThreadParam*) -> tid; param +0x08 entry, +0x0C stack size, +0x10 priority.
    void CreateThread(IopContext& ctx);
    // int DeleteThread(tid) -> 0.
    void DeleteThread(IopContext& ctx);
    // int StartThread(tid, arg) -> 0; arms the body with $a0 = arg.
    void StartThread(IopContext& ctx);
    // int ExitThread() -> never returns: the running thread ends (idx 8).
    void ExitThread(IopContext& ctx);
    // int ExitDeleteThread() -> never returns: the running thread ends and is deleted.
    void ExitDeleteThread(IopContext& ctx);
    // int ReferThreadStatus(tid, ThreadStatus*) -> 0: {attr, option, status, entry, stack,
    // stackSize, gpReg, initPriority, currentPriority, waitType, waitId, wakeupCount} (idx 22).
    void ReferThreadStatus(IopContext& ctx);
    // int RotateThreadReadyQueue(prio) -> 0: the running thread yields to equals.
    void RotateThreadReadyQueue(IopContext& ctx);
    // int iWakeupThread(tid): the interrupt-context form (same rule as WakeupThread here).
    void iWakeupThread(IopContext& ctx);
    // int TerminateThread(tid) -> 0.
    void TerminateThread(IopContext& ctx);
    // int ChangeThreadPriority(tid, prio): requeue and reschedule, or an IOP error.
    void ChangeThreadPriority(IopContext& ctx);
    // int GetThreadId() -> the running kernel thread's tid.
    void GetThreadId(IopContext& ctx);
    // int SleepThread() -> 0; parks the running thread until WakeupThread.
    void SleepThread(IopContext& ctx);
    // int WakeupThread(tid): operation result in kernel context; a host call
    // returns queue acceptance (delivery errors: Kernel().InjectedWakeErrors()).
    void WakeupThread(IopContext& ctx);
    // int DelayThread(usec): cooperative timed park; no thread -> illegal context.
    void DelayThread(IopContext& ctx);
    // int USec2SysClock(usec, SysClock* out) -> 0; out = usec * 37 as a 64-bit value.
    void USec2SysClock(IopContext& ctx);

    int NextTid() const { return next_tid_; }
    uint32_t ThreadEntry(int tid) const { return (tid > 0 && tid < kMaxThreads) ? entry_[tid] : 0; }

    // Savestate restore of one thread-table record (IOPM). The loader process
    // booted the module to its first block only; a thread the guest created and
    // started LATER (the saver's title did it through CreateThread/StartThread
    // before the image was taken) does not exist in this kernel. The record
    // re-creates it with its entry/priority and, when `started`, arms the same
    // body StartThread arms (fresh context, $a0 = arg) -- the thread then runs
    // from its entry to its first park on the restored RAM, as it did on the
    // saver. A thread that is already live is left alone (the kernel's own
    // reconcile owns its wait state). Returns true when a body was armed.
    bool RestoreThread(int tid, uint32_t entry, uint32_t priority, uint32_t arg, bool started);

private:
    bool ArmBody(int tid, uint32_t arg, const GuestFunction& body);

    IopModuleTable& table_;
    IopMemory& mem_;
    FunctionResolver resolve_;
    ThreadLayout layout_;
    StackAlloc stack_alloc_;
    StackFree stack_free_;
    int next_tid_ = 1;
    uint32_t entry_[kMaxThreads] = {};
    uint32_t gp_[kMaxThreads] = {};           // the creating thread's $gp (layout gp == 0)
    uint32_t stack_base_[kMaxThreads] = {};   // heap-allocated stack (allocator bound), 0 = band formula
    uint32_t stack_size_[kMaxThreads] = {};
    bool started_[kMaxThreads] = {};
    std::atomic<uint64_t> priority_calls_{0}, priority_changes_{0}, priority_errors_{0};
    std::atomic<uint64_t> delay_kernel_{0}, delay_host_{0};
};

}  // namespace ps2r::iop::hle
