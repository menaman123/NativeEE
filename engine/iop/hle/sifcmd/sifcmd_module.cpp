// engine/iop/hle/sifcmd/sifcmd_module.cpp -- SifcmdModule: sifcmd over the RPC bridge.
#include "engine/iop/hle/sifcmd/sifcmd_module.h"

#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_module_table.h"
#include "engine/iop/sif_rpc_bridge.h"

namespace ps2r::iop::hle {

void SifcmdModule::sceSifInitRpc(IopContext& ctx) { ctx.gpr[2] = 0; }

void SifcmdModule::sceSifBindRpc(IopContext& ctx) {
    // Real hardware binds asynchronously through the EE; here the bind is
    // complete on return: a non-zero server-data word marks success.
    const uint32_t client = ctx.gpr[4];
    const uint32_t server_id = ctx.gpr[5];
    if (client) mem_.Write<uint32_t>(client + kClientServerWordOffset, kFakeServerTag | (server_id & 0xFFFFu));
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifCallRpc(IopContext& ctx) { hle_stream_cd_read_rpc(ctx); }

void SifcmdModule::hle_stream_cd_read_rpc(IopContext& ctx) {
    if (cd_read_rpc_) {
        cd_read_rpc_(ctx);
        return;
    }
    ctx.gpr[2] = 0;
    ctx.FixZero();
}

void SifcmdModule::sceSifRegisterRpc(IopContext& ctx) {
    const uint32_t server_data = ctx.gpr[4];
    const uint32_t server_id = ctx.gpr[5];
    const uint32_t func = ctx.gpr[6];
    mem_.Write<uint32_t>(server_data + 0, server_id);
    mem_.Write<uint32_t>(server_data + 4, func);
    last_registered_id_ = server_id;
    last_registered_func_ = func;
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifSetRpcQueue(IopContext& ctx) {
    rpc_queue_tid_ = static_cast<int>(ctx.gpr[5]);
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifRpcLoop(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (k.OnKernelThread() && k.CurrentTid() > 0) table_.Rpc().RpcServiceLoop();
    ctx.gpr[2] = 0;
}

}  // namespace ps2r::iop::hle
