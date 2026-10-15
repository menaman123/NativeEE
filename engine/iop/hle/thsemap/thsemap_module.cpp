// ThsemapModule: IOP semaphore validation and cooperative waits.
#include "engine/iop/hle/thsemap/thsemap_module.h"

#include "engine/iop/iop_error.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

void ThsemapModule::Return(IopContext& ctx, int result) {
    if (result < 0) errors_.fetch_add(1, std::memory_order_relaxed);
    ctx.gpr[2] = static_cast<uint32_t>(result);
}

void ThsemapModule::CreateSema(IopContext& ctx) {
    const uint32_t param = ctx.gpr[4];
    const uint32_t attr = mem_.Read<uint32_t>(param);
    const int32_t init = static_cast<int32_t>(mem_.Read<uint32_t>(param + 0x08));
    const int32_t max = static_cast<int32_t>(mem_.Read<uint32_t>(param + 0x0C));
    if (attr & ~0x101u) {
        Return(ctx, IopError::IllegalAttr);
        return;
    }
    if (init < 0 || max <= 0 || init > max) {
        Return(ctx, IopError::Error);
        return;
    }
    IopKernel& k = table_.Kernel();
    auto create = [&] {
        for (int i = 0; i < kMaxSemas; ++i) {
            const int sid = (next_sid_ + i) % kMaxSemas;
            if (k.SemaActive(sid)) continue;
            k.RegisterSema(sid, init, max, (attr & 1) != 0);
            next_sid_ = (sid + 1) % kMaxSemas;
            Return(ctx, sid);
            return;
        }
        Return(ctx, IopError::NoMemory);
    };
    if (k.OnKernelThread()) create();
    else k.WithDispatchLock(create);
}

void ThsemapModule::DeleteSema(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    auto remove = [&] { Return(ctx, k.DeleteSema(static_cast<int32_t>(ctx.gpr[4]))); };
    if (k.OnKernelThread()) remove();
    else k.WithDispatchLock(remove);
    table_.NotifyHost();
}

void ThsemapModule::SignalSema(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int sid = static_cast<int32_t>(ctx.gpr[4]);
    if (k.OnKernelThread()) Return(ctx, k.SignalSema(sid));
    else if (sid < 0 || sid >= kMaxSemas) Return(ctx, IopError::UnknownSemid);
    else {
        // Never wait for the dispatcher lock here: a running coroutine may
        // need this signal before Dispatch can return. Validate upon delivery.
        k.InjectSignalSema(sid);
        Return(ctx, IopError::Ok);
    }
    table_.NotifyHost();
}

void ThsemapModule::iSignalSema(IopContext& ctx) { SignalSema(ctx); }

void ThsemapModule::WaitSema(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    auto wait = [&] { Return(ctx, k.WaitSema(static_cast<int32_t>(ctx.gpr[4]))); };
    if (k.OnKernelThread()) wait();
    else {
        host_waits_.fetch_add(1, std::memory_order_relaxed);
        k.WithDispatchLock(wait);
    }
}

}  // namespace ps2r::iop::hle
