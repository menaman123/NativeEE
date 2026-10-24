// engine/iop/hle/thevent/thevent_module.cpp -- TheventModule over IopKernel.
#include "engine/iop/hle/thevent/thevent_module.h"

#include "engine/iop/iop_error.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

void TheventModule::CreateEventFlag(IopContext& ctx) {
    const uint32_t param = ctx.gpr[4];
    const uint32_t attr = mem_.Read<uint32_t>(param + 0x00);
    const uint32_t init = mem_.Read<uint32_t>(param + 0x08);
    IopKernel& k = table_.Kernel();
    auto create = [&] {
        // Kernel ids are 0-based; the guest sees 1-based ids (0 is never a valid ef id).
        ctx.gpr[2] = static_cast<uint32_t>(k.CreateEventFlag(init, (attr & kAttrMulti) != 0) + 1);
        created_++;
    };
    if (k.OnKernelThread()) create();
    else k.WithDispatchLock(create);
}

void TheventModule::DeleteEventFlag(IopContext& ctx) {
    // The kernel keeps event-flag records by index; a deleted flag is simply never
    // waited on again by the module. Waiters, if any, are released with the delete code.
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    auto del = [&] { k.SetEventFlag(ef, 0xFFFFFFFFu); };
    if (k.OnKernelThread()) del();
    else k.WithDispatchLock(del);
    ctx.gpr[2] = 0;
}

void TheventModule::SetEventFlag(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t bits = ctx.gpr[5];
    int r = 0;
    auto set = [&] { r = k.SetEventFlag(ef, bits); };
    if (k.OnKernelThread()) set();
    else k.WithDispatchLock(set);
    ctx.gpr[2] = static_cast<uint32_t>(r < 0 ? IopError::UnknownEvfid : 0);
    table_.NotifyHost();
}

void TheventModule::iSetEventFlag(IopContext& ctx) { SetEventFlag(ctx); }

void TheventModule::ClearEventFlag(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t keep = ctx.gpr[5];
    int r = 0;
    auto clear = [&] { r = k.ClearEventFlag(ef, keep); };
    if (k.OnKernelThread()) clear();
    else k.WithDispatchLock(clear);
    ctx.gpr[2] = static_cast<uint32_t>(r < 0 ? IopError::UnknownEvfid : 0);
}

void TheventModule::iClearEventFlag(IopContext& ctx) { ClearEventFlag(ctx); }

void TheventModule::WaitEventFlag(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t bits = ctx.gpr[5];
    const uint32_t mode = ctx.gpr[6];
    const uint32_t res = ctx.gpr[7];
    if (!k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    if (bits == 0) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::EvfIllegalPattern);
        return;
    }
    const int r = k.WaitEventFlag(ef, bits, mode);
    // The value at the time the wait was satisfied (before WEF_CLEAR): the kernel
    // has already cleared on match, so report the satisfied pattern.
    if (res) mem_.Write<uint32_t>(res, (mode & kModeClear) ? bits : k.EventFlagValue(ef));
    ctx.gpr[2] = static_cast<uint32_t>(r < 0 ? IopError::EvfMulti : 0);
}

void TheventModule::PollEventFlag(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t bits = ctx.gpr[5];
    const uint32_t mode = ctx.gpr[6];
    const uint32_t res = ctx.gpr[7];
    int result = 0;
    auto poll = [&] {
        const uint32_t value = k.EventFlagValue(ef);
        const bool match = (mode & kModeOr) ? (value & bits) != 0 : (value & bits) == bits;
        if (res) mem_.Write<uint32_t>(res, value);
        if (!match) {
            result = IopError::EvfCondition;
            return;
        }
        if (mode & kModeClear) k.ClearEventFlag(ef, 0);
    };
    if (k.OnKernelThread()) poll();
    else k.WithDispatchLock(poll);
    ctx.gpr[2] = static_cast<uint32_t>(result);
}

void TheventModule::ReferEventFlagStatus(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int ef = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t status = ctx.gpr[5];
    if (status) {
        // EventFlagStatus {attr, option, initPattern, currentPattern, numThreads}
        mem_.Write<uint32_t>(status + 0x0C, k.EventFlagValue(ef));
    }
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
