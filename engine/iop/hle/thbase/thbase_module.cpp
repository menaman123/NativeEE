// engine/iop/hle/thbase/thbase_module.cpp -- ThbaseModule: thbase over IopKernel.
#include "engine/iop/hle/thbase/thbase_module.h"

#include <utility>

#include "engine/iop/iop_error.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

namespace {
constexpr uint32_t kMinDelayUsec = 100;   // threadman's SetAlarm minimum
}  // namespace

ThbaseModule::ThbaseModule(IopModuleTable& table, IopMemory& mem, FunctionResolver resolve, ThreadLayout layout)
    : table_(table), mem_(mem), resolve_(std::move(resolve)), layout_(layout) {}

void ThbaseModule::SetStackAllocator(StackAlloc alloc, StackFree free) {
    stack_alloc_ = std::move(alloc);
    stack_free_ = std::move(free);
}

ThbaseModule::Counters ThbaseModule::Count() const {
    Counters result;
    result.priority_changes = priority_changes_.load(std::memory_order_relaxed);
    result.priority_errors = priority_errors_.load(std::memory_order_relaxed);
    result.priority_calls = priority_calls_.load(std::memory_order_relaxed);
    result.delay_kernel = delay_kernel_.load(std::memory_order_relaxed);
    result.delay_host = delay_host_.load(std::memory_order_relaxed);
    result.injected_wake_errors = table_.Kernel().InjectedWakeErrors();
    result.injected_signal_errors = table_.Kernel().InjectedSignalErrors();
    return result;
}

void ThbaseModule::CreateThread(IopContext& ctx) {
    const uint32_t param = ctx.gpr[4];
    const uint32_t entry = mem_.Read<uint32_t>(param + 0x08);
    const uint32_t priority = mem_.Read<uint32_t>(param + 0x10);
    if (priority > 126) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalPriority);
        return;
    }
    if (entry & 3) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalEntry);
        return;
    }
    const uint32_t stack_size = mem_.Read<uint32_t>(param + 0x0C);
    if (stack_size < 0x130) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalStackSize);
        return;
    }
    IopKernel& k = table_.Kernel();
    auto create = [&] {
        for (int i = 0; i < kMaxThreads - 1; ++i) {
            const int tid = 1 + (next_tid_ - 1 + i) % (kMaxThreads - 1);
            if (k.ThreadRegistered(tid)) continue;
            uint32_t stack_base = 0;
            if (stack_alloc_) {
                const uint32_t rounded = (stack_size + 0xFu) & ~0xFu;
                stack_base = stack_alloc_(rounded);
                if (!stack_base) {
                    ctx.gpr[2] = static_cast<uint32_t>(IopError::NoMemory);
                    return;
                }
                stack_size_[tid] = rounded;
            }
            next_tid_ = tid + 1;
            entry_[tid] = entry;
            gp_[tid] = layout_.gp ? layout_.gp : ctx.gpr[28];
            stack_base_[tid] = stack_base;
            started_[tid] = false;
            k.RegisterThread(tid, priority);
            ctx.gpr[2] = static_cast<uint32_t>(tid);
            return;
        }
        ctx.gpr[2] = static_cast<uint32_t>(IopError::NoMemory);
    };
    if (k.OnKernelThread()) create();
    else k.WithDispatchLock(create);
}

void ThbaseModule::DeleteThread(IopContext& ctx) {
    const int tid = static_cast<int32_t>(ctx.gpr[4]);
    IopKernel& k = table_.Kernel();
    auto remove = [&] {
        if (tid == 0) { ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalThid); return; }
        if (!k.ThreadRegistered(tid)) { ctx.gpr[2] = static_cast<uint32_t>(IopError::UnknownThid); return; }
        if (k.ThreadState(tid) != IopKernel::State::Dormant) {
            ctx.gpr[2] = static_cast<uint32_t>(IopError::NotDormant);
            return;
        }
        k.UnregisterThread(tid);
        if (stack_base_[tid] && stack_free_) stack_free_(stack_base_[tid]);
        stack_base_[tid] = 0;
        stack_size_[tid] = 0;
        entry_[tid] = 0;
        started_[tid] = false;
        ctx.gpr[2] = 0;
    };
    if (k.OnKernelThread()) remove();
    else k.WithDispatchLock(remove);
}

void ThbaseModule::ExitThread(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (!k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    const int tid = k.CurrentTid();
    if (tid > 0 && tid < kMaxThreads) started_[tid] = false;
    ctx.gpr[2] = 0;
    k.ExitThread();
}

void ThbaseModule::ReferThreadStatus(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int tid = ctx.gpr[4] == 0 ? k.CurrentTid() : static_cast<int32_t>(ctx.gpr[4]);
    const uint32_t out = ctx.gpr[5];
    if (tid <= 0 || tid >= kMaxThreads || !k.ThreadRegistered(tid)) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::UnknownThid);
        return;
    }
    uint32_t status = 0x10;   // THS_DORMANT
    switch (k.ThreadState(tid)) {
        case IopKernel::State::Run: status = 0x01; break;
        case IopKernel::State::Ready: status = 0x02; break;
        case IopKernel::State::Wait: status = 0x04; break;
        case IopKernel::State::Dormant: status = 0x10; break;
    }
    if (out) {
        mem_.Write<uint32_t>(out + 0x00, 0);                                      // attr
        mem_.Write<uint32_t>(out + 0x04, 0);                                      // option
        mem_.Write<uint32_t>(out + 0x08, status);
        mem_.Write<uint32_t>(out + 0x0C, entry_[tid]);
        mem_.Write<uint32_t>(out + 0x10, stack_base_[tid]);
        mem_.Write<uint32_t>(out + 0x14, stack_size_[tid]);
        mem_.Write<uint32_t>(out + 0x18, gp_[tid]);
        mem_.Write<uint32_t>(out + 0x1C, k.ThreadPriority(tid));
        mem_.Write<uint32_t>(out + 0x20, k.ThreadPriority(tid));
        mem_.Write<uint32_t>(out + 0x24, static_cast<uint32_t>(k.ThreadWaitKind(tid)));
        mem_.Write<uint32_t>(out + 0x28, 0);
        mem_.Write<uint32_t>(out + 0x2C, 0);
    }
    ctx.gpr[2] = 0;
}

void ThbaseModule::ExitDeleteThread(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (!k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    const int tid = k.CurrentTid();
    if (tid > 0 && tid < kMaxThreads) {
        // The stack is released when the thread is gone; the body's return (below the
        // kernel's ExitThread) never touches it again.
        if (stack_base_[tid] && stack_free_) stack_free_(stack_base_[tid]);
        stack_base_[tid] = 0;
        stack_size_[tid] = 0;
        entry_[tid] = 0;
        started_[tid] = false;
    }
    ctx.gpr[2] = 0;
    k.ExitThread();   // does not return to the body
}

void ThbaseModule::RotateThreadReadyQueue(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (k.OnKernelThread()) k.Yield();
    ctx.gpr[2] = 0;
}

void ThbaseModule::iWakeupThread(IopContext& ctx) { WakeupThread(ctx); }

void ThbaseModule::StartThread(IopContext& ctx) {
    const int tid = static_cast<int32_t>(ctx.gpr[4]);
    const uint32_t arg = ctx.gpr[5];
    IopKernel& k = table_.Kernel();
    auto start = [&] {
        if (tid == 0) { ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalThid); return; }
        if (!k.ThreadRegistered(tid)) { ctx.gpr[2] = static_cast<uint32_t>(IopError::UnknownThid); return; }
        if (k.ThreadState(tid) != IopKernel::State::Dormant) {
            ctx.gpr[2] = static_cast<uint32_t>(IopError::NotDormant);
            return;
        }
        GuestFunction body = resolve_ ? resolve_(entry_[tid]) : GuestFunction{};
        if (!body) {
            ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalEntry);
            return;
        }
        started_[tid] = ArmBody(tid, arg, body);
        ctx.gpr[2] = static_cast<uint32_t>(started_[tid] ? IopError::Ok : IopError::NoMemory);
    };
    if (k.OnKernelThread()) start();
    else k.WithDispatchLock(start);
    table_.NotifyHost();
}

bool ThbaseModule::ArmBody(int ktid, uint32_t arg, const GuestFunction& body) {
    const uint32_t entry = entry_[ktid];
    const ThreadLayout layout = layout_;
    const uint32_t gp = gp_[ktid] ? gp_[ktid] : layout.gp;
    const uint32_t sp = stack_base_[ktid] ? stack_base_[ktid] + stack_size_[ktid] - 0x10
                                          : layout.stack_top - static_cast<uint32_t>(ktid) * layout.stack_stride - 0x10;
    return table_.Kernel().StartThread(ktid, [this, ktid, body, entry, arg, sp, gp] {
        IopContext thread_ctx = IopContext::ForEntry(sp, gp);
        thread_ctx.gpr[4] = arg;
        thread_ctx.pc = entry;
        body(thread_ctx);
        started_[ktid] = false;
    });
}

bool ThbaseModule::RestoreThread(int tid, uint32_t entry, uint32_t priority, uint32_t arg, bool started) {
    if (tid <= 0 || tid >= kMaxThreads || entry == 0) return false;
    IopKernel& k = table_.Kernel();
    if (k.ThreadRegistered(tid) && k.ThreadState(tid) != IopKernel::State::Dormant) return false;   // live: not ours
    if (!k.ThreadRegistered(tid)) k.RegisterThread(tid, priority);
    entry_[tid] = entry;
    if (tid >= next_tid_) next_tid_ = tid + 1;
    if (!started) return false;
    GuestFunction body = resolve_ ? resolve_(entry) : GuestFunction{};
    if (!body) return false;   // no recompiled function at the entry: the thread never runs (StartThread's shape)
    started_[tid] = ArmBody(tid, arg, body);
    // StartThread restores creation priority; re-parking a savestate instead
    // needs the effective priority recorded in that image.
    if (started_[tid] && priority > 0 && priority <= 126 && k.ThreadPriority(tid) != priority)
        k.ChangePriority(tid, priority);
    return started_[tid];
}

void ThbaseModule::TerminateThread(IopContext& ctx) {
    const int tid = static_cast<int32_t>(ctx.gpr[4]);
    IopKernel& k = table_.Kernel();
    auto terminate = [&] {
        if (!k.ThreadRegistered(tid)) { ctx.gpr[2] = static_cast<uint32_t>(IopError::UnknownThid); return; }
        if (tid == k.CurrentTid()) { ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalThid); return; }
        if (k.ThreadState(tid) == IopKernel::State::Dormant) {
            ctx.gpr[2] = static_cast<uint32_t>(IopError::Dormant);
            return;
        }
        k.TerminateThread(tid);
        started_[tid] = false;
        ctx.gpr[2] = 0;
    };
    if (k.OnKernelThread()) terminate();
    else k.WithDispatchLock(terminate);
}

void ThbaseModule::ChangeThreadPriority(IopContext& ctx) {
    priority_calls_.fetch_add(1, std::memory_order_relaxed);
    IopKernel& k = table_.Kernel();
    auto change = [&] {
        const int tid = ctx.gpr[4] == 0 ? k.CurrentTid() : static_cast<int32_t>(ctx.gpr[4]);
        const uint32_t old_priority = k.ThreadPriority(tid);
        const uint32_t priority = ctx.gpr[5] == 0 ? k.ThreadPriority(k.CurrentTid()) : ctx.gpr[5];
        const int result = k.ChangePriority(static_cast<int32_t>(ctx.gpr[4]), ctx.gpr[5]);
        if (result != IopError::Ok) priority_errors_.fetch_add(1, std::memory_order_relaxed);
        else if (priority != old_priority) priority_changes_.fetch_add(1, std::memory_order_relaxed);
        ctx.gpr[2] = static_cast<uint32_t>(result);
    };
    if (k.OnKernelThread()) change();
    else k.WithDispatchLock(change);
}

void ThbaseModule::GetThreadId(IopContext& ctx) {
    ctx.gpr[2] = static_cast<uint32_t>(table_.Kernel().CurrentTid());
}

void ThbaseModule::SleepThread(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (!k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    k.SleepThread();
    ctx.gpr[2] = 0;
}

void ThbaseModule::WakeupThread(IopContext& ctx) {
    const int tid = static_cast<int32_t>(ctx.gpr[4]);
    IopKernel& k = table_.Kernel();
    if (k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(k.WakeupThread(tid));
    } else if (tid == 0) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalThid);
    } else if (tid < 0 || tid >= kMaxThreads) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::UnknownThid);
    } else {
        // Host success means injection accepted. Registration/state validation
        // runs on dispatch and failures enter InjectedWakeErrors().
        k.iWakeupThread(tid);
        ctx.gpr[2] = 0;
    }
    table_.NotifyHost();
}

void ThbaseModule::DelayThread(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (!k.OnKernelThread()) {
        delay_host_.fetch_add(1, std::memory_order_relaxed);
        // The host loader / interrupt callback has no guest continuation to
        // park. Report the unsupported context instead of claiming a delay.
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    delay_kernel_.fetch_add(1, std::memory_order_relaxed);
    k.DelayUs(ctx.gpr[4] < kMinDelayUsec ? kMinDelayUsec : ctx.gpr[4]);
    ctx.gpr[2] = 0;
}

void ThbaseModule::USec2SysClock(IopContext& ctx) {
    const uint32_t usec = ctx.gpr[4];
    const uint32_t dest = ctx.gpr[5];
    const uint64_t ticks = static_cast<uint64_t>(usec) * kSysClockPerUsec;
    if (dest) {
        mem_.Write<uint32_t>(dest + 0, static_cast<uint32_t>(ticks));
        mem_.Write<uint32_t>(dest + 4, static_cast<uint32_t>(ticks >> 32));
    }
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
