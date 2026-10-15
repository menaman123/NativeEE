#include "engine/ee/hle/kernl_boot.h"

#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/recomp_abi/host_app/memory.h"

uint32_t KernlBoot::heap_base_ = 0;

// thbase (SDK 3.0.1 kernel) InitMainThread: the main thread's heap record starts
// as [4 MiB, initial $sp) before the title's InitHeap call re-seats the base
// (its own request, or the title default when passed -1). The value is the
// kernel's provisional base, observable only by a title that queries the heap
// before InitHeap (crt0 does not); it is a kernel layout constant, not title
// data, and lies above the loadable range (PT_LOAD memsz end < 4 MiB).
constexpr uint32_t kInitMainThreadHeapBase = 0x00400000;
uint32_t KernlBoot::heap_end_ = 0;

namespace {
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }

void Ret0(CpuContext& ctx) { Ret(ctx, 0); }
void Ret1(CpuContext& ctx) { Ret(ctx, 1); }

void EIntr(CpuContext& ctx) {
    ctx.cpuRegs.CP0.n.Status |= 0x1u;
    Ret(ctx, 0);
}
void DIntr(CpuContext& ctx) {
    const uint32_t was_enabled = ctx.cpuRegs.CP0.n.Status & 0x1u;
    ctx.cpuRegs.CP0.n.Status &= ~0x1u;
    ctx.cpuRegs.GPR.r[2].UD[0] = was_enabled ? 1u : 0u;
}
// libkernl StartThread wrapper: the real syscall 34.
void StartThreadBinding(CpuContext& ctx) { SyscallTable::Instance().Dispatch(34, ctx); }
}  // namespace

void KernlBoot::Register(SdkBindingManifest& m) {
    m.Register("libkernl.a", "InitTLB", &KernlBoot::InitTLB);
    m.Register("libkernl.a", "InitTLB32MB", &KernlBoot::InitTLB);
    m.Register("libkernl.a", "InitTLBFunctions", &Ret0);
    m.Register("libkernl.a", "EIntr", &EIntr);
    m.Register("libkernl.a", "DIntr", &DIntr);
    m.Register("libkernl.a", "StartThread", &StartThreadBinding);
    m.Register("libkernl.a", "InitThread", &Ret0);
    m.Register("libkernl.a", "InitAlarm", &Ret0);
    m.Register("libkernl.a", "_InitSys", &Ret0);
    m.Register("libkernl.a", "InitSystemCallTableAddress", &Ret0);
    m.Register("libkernl.a", "supplement_crt0", &Ret0);
    m.Register("libkernl.a", "_sceIDC", &Ret0);
    m.Register("libkernl.a", "sceSifRebootIop", &Ret1);      // 1 = accepted
    m.Register("libkernl.a", "sceSifSyncIop", &Ret1);        // 1 = in sync
    m.Register("libkernl.a", "sceSifInitRebootNotify", &Ret0);
}

uint32_t KernlBoot::InitMainThread(uint32_t gp, uint32_t stack, int32_t stack_size, uint32_t args,
                                   int32_t /*root*/, CpuContext& ctx) {
    const uint32_t sp = BootEnv::MainThreadSp(stack, stack_size);
    auto& tm = ThreadManager::Instance();
    std::shared_ptr<ThreadInfo> info = tm.Current();
    if (!info) info = tm.AdoptCurrentThread(1, 0);
    if (info) {
        std::lock_guard<std::mutex> lock(info->m);
        info->status = THS_RUN;
        info->entry = ctx.cpuRegs.pc;
        info->stack = BootEnv::MainThreadStackBase(stack, stack_size);
        info->stack_size = static_cast<uint32_t>(stack_size);
        info->gp = gp;
        info->priority = 0;
        info->current_priority = 0;
    }
    heap_base_ = kInitMainThreadHeapBase;
    heap_end_ = sp;
    ctx.cpuRegs.GPR.r[28].UL[0] = gp;
    ctx.cpuRegs.GPR.r[29].UL[0] = sp;
    if (args != 0 && args != 0xFFFFFFFFu) BootEnv::Instance().WriteArgv(args);
    return sp;
}

uint32_t KernlBoot::InitHeap(uint32_t heap, int32_t heap_size, CpuContext& ctx) {
    if (heap != 0xFFFFFFFFu) heap_base_ = heap;
    else if (heap_base_ == 0) heap_base_ = BootEnv::Instance().Params().default_heap_base;
    const uint32_t limit =
        BootEnv::Instance().HeapLimit(heap_base_, heap_size, ctx.cpuRegs.GPR.r[29].UL[0]);
    heap_end_ = limit;
    if (limit <= heap_base_) return 0;
    const uint32_t brk = BootEnv::Instance().Params().sbrk_break_word;
    if (brk != 0) memory::write<uint32_t>(brk, heap_base_);
    return limit;
}

uint32_t KernlBoot::EndOfHeap() { return BootEnv::kRamTop; }

void KernlBoot::InitTLB(CpuContext& ctx) { Ret0(ctx); }

void KernlBoot::ContextRestore(CpuContext& ctx) {
    // The kernel's context-switch return: the running thread continues at the
    // saved $ra with its own $sp (the record's ctx is the live context of the
    // thread that owns it; the caller IS that thread).
    auto info = ThreadManager::Instance().Current();
    if (info && info->ctx && info->ctx != &ctx) {
        ctx.cpuRegs.GPR.r[29] = info->ctx->cpuRegs.GPR.r[29];
        ctx.cpuRegs.GPR.r[31] = info->ctx->cpuRegs.GPR.r[31];
    }
    ctx.cpuRegs.pc = ctx.cpuRegs.GPR.r[31].UL[0];
}

void KernlBoot::ResetForTest() {
    heap_base_ = 0;
    heap_end_ = 0;
}
