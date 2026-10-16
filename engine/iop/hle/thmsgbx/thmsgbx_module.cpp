// engine/iop/hle/thmsgbx/thmsgbx_module.cpp -- ThmsgbxModule over IopKernel.
#include "engine/iop/hle/thmsgbx/thmsgbx_module.h"

#include "engine/iop/iop_error.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"

namespace ps2r::iop::hle {

namespace {
// Guest message pointers travel through the kernel as opaque void* values.
void* ToToken(uint32_t addr) { return reinterpret_cast<void*>(static_cast<uintptr_t>(addr)); }
uint32_t FromToken(void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
}  // namespace

void ThmsgbxModule::CreateMbx(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    auto create = [&] {
        ctx.gpr[2] = static_cast<uint32_t>(k.CreateMbx() + 1);   // 1-based ids for the guest
        created_++;
    };
    if (k.OnKernelThread()) create();
    else k.WithDispatchLock(create);
}

void ThmsgbxModule::DeleteMbx(IopContext& ctx) { ctx.gpr[2] = 0; }

void ThmsgbxModule::SendMbx(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const int mbx = static_cast<int32_t>(ctx.gpr[4]) - 1;
    const uint32_t msg = ctx.gpr[5];
    int r = 0;
    auto send = [&] { r = k.SendMbx(mbx, ToToken(msg)); };
    if (k.OnKernelThread()) send();
    else k.WithDispatchLock(send);
    ctx.gpr[2] = static_cast<uint32_t>(r < 0 ? kUnknownMbxid : 0);
    table_.NotifyHost();
}

void ThmsgbxModule::iSendMbx(IopContext& ctx) { SendMbx(ctx); }

void ThmsgbxModule::ReceiveMbx(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const uint32_t out = ctx.gpr[4];
    const int mbx = static_cast<int32_t>(ctx.gpr[5]) - 1;
    if (!k.OnKernelThread()) {
        ctx.gpr[2] = static_cast<uint32_t>(IopError::IllegalContext);
        return;
    }
    void* msg = nullptr;
    const int r = k.ReceiveMbx(&msg, mbx);
    if (r < 0) {
        ctx.gpr[2] = static_cast<uint32_t>(kUnknownMbxid);
        return;
    }
    if (out) mem_.Write<uint32_t>(out, FromToken(msg));
    ctx.gpr[2] = 0;
}

void ThmsgbxModule::PollMbx(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    const uint32_t out = ctx.gpr[4];
    const int mbx = static_cast<int32_t>(ctx.gpr[5]) - 1;
    int r = 0;
    void* msg = nullptr;
    auto poll = [&] { r = k.PollMbx(&msg, mbx); };
    if (k.OnKernelThread()) poll();
    else k.WithDispatchLock(poll);
    if (r < 0) {
        ctx.gpr[2] = static_cast<uint32_t>(kNoMessage);
        return;
    }
    if (out) mem_.Write<uint32_t>(out, FromToken(msg));
    ctx.gpr[2] = 0;
}

void ThmsgbxModule::ReferMbxStatus(IopContext& ctx) {
    // MbxStatus {attr, option, numWaitThreads, numMessage, topPacket}: nothing queued is reported.
    const uint32_t status = ctx.gpr[5];
    if (status) {
        mem_.Write<uint32_t>(status + 0x08, 0);
        mem_.Write<uint32_t>(status + 0x0C, 0);
        mem_.Write<uint32_t>(status + 0x10, 0);
    }
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
