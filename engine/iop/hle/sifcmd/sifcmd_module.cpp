// engine/iop/hle/sifcmd/sifcmd_module.cpp -- SifcmdModule: sifcmd over the RPC bridge.
#include "engine/iop/hle/sifcmd/sifcmd_module.h"

#include <vector>

#include "engine/iop/hle/sifman/ee_ram_sink.h"
#include "engine/iop/hle/sifman/ee_ram_source.h"
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
    // sceSifRegisterRpc(sd, sid, func, buff, cfunc, cbuff, qd): a0..a3 + three stack words at sp+0x10.
    ServerRecord r;
    r.server_data = ctx.gpr[4];
    r.sid = ctx.gpr[5];
    r.func = ctx.gpr[6];
    r.buff = ctx.gpr[7];
    const uint32_t sp = ctx.gpr[29];
    r.cfunc = mem_.Read<uint32_t>(sp + 0x10);
    r.cbuff = mem_.Read<uint32_t>(sp + 0x14);
    r.queue = mem_.Read<uint32_t>(sp + 0x18);
    r.tid = QueueTid(r.queue);
    if (r.server_data) {
        mem_.Write<uint32_t>(r.server_data + kServerSid, r.sid);
        mem_.Write<uint32_t>(r.server_data + kServerFunc, r.func);
        mem_.Write<uint32_t>(r.server_data + kServerBuff, r.buff);
        mem_.Write<uint32_t>(r.server_data + kServerCfunc, r.cfunc);
        mem_.Write<uint32_t>(r.server_data + kServerCbuff, r.cbuff);
        mem_.Write<uint32_t>(r.server_data + kServerBase, r.queue);
    }
    last_registered_id_ = r.sid;
    last_registered_func_ = r.func;
    bool replaced = false;
    for (ServerRecord& s : servers_) {
        if (s.sid == r.sid) {
            s = r;
            replaced = true;
        }
    }
    if (!replaced) servers_.push_back(r);
    if (registrar_) registrar_(r);
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifSetRpcQueue(IopContext& ctx) {
    const uint32_t queue = ctx.gpr[4];
    const int tid = static_cast<int>(ctx.gpr[5]);
    rpc_queue_tid_ = tid;
    if (queue) {
        queue_tids_[queue] = tid;
        mem_.Write<uint32_t>(queue + kQueueThreadId, static_cast<uint32_t>(tid));
        mem_.Write<uint32_t>(queue + kQueueActive, 0);
    }
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifRpcLoop(IopContext& ctx) {
    IopKernel& k = table_.Kernel();
    if (k.OnKernelThread() && k.CurrentTid() > 0) {
        const int tid = k.CurrentTid();
        loop_ctx_[tid] = &ctx;
        table_.Rpc().RpcServiceLoop();
        loop_ctx_.erase(tid);
    }
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifGetOtherData(IopContext& ctx) {
    // sceSifGetOtherData(rd, ee_src, iop_dst, size, mode): a synchronous EE -> IOP copy
    // (the SDK's call blocks for the DMA with mode 0; the copy IS the completion here).
    const uint32_t ee_src = ctx.gpr[5];
    const uint32_t iop_dst = ctx.gpr[6];
    const uint32_t size = ctx.gpr[7];
    if (!ee_source_ || size == 0) {
        ctx.gpr[2] = static_cast<uint32_t>(-1);
        return;
    }
    std::vector<uint8_t> bytes(size);
    if (!ee_source_->ReadEeRam(ee_src, bytes.data(), bytes.size())) {
        ctx.gpr[2] = static_cast<uint32_t>(-1);
        return;
    }
    uint8_t* dst = mem_.Pointer(iop_dst, size);
    if (!dst) {
        ctx.gpr[2] = static_cast<uint32_t>(-1);
        return;
    }
    for (uint32_t i = 0; i < size; ++i) dst[i] = bytes[i];
    other_data_bytes_ += size;
    ctx.gpr[2] = 0;
}

void SifcmdModule::sceSifRemoveRpc(IopContext& ctx) {
    const uint32_t sd = ctx.gpr[4];
    for (size_t i = 0; i < servers_.size(); ++i) {
        if (servers_[i].server_data == sd) {
            const uint32_t sid = servers_[i].sid;
            servers_.erase(servers_.begin() + static_cast<long>(i));
            if (unregistrar_) unregistrar_(sid);
            break;
        }
    }
    ctx.gpr[2] = sd;
}

void SifcmdModule::sceSifRemoveRpcQueue(IopContext& ctx) {
    const uint32_t qd = ctx.gpr[4];
    queue_tids_.erase(qd);
    ctx.gpr[2] = qd;
}

const SifcmdModule::ServerRecord* SifcmdModule::FindServer(uint32_t sid) const {
    for (const ServerRecord& s : servers_)
        if (s.sid == sid) return &s;
    return nullptr;
}

const IopContext* SifcmdModule::LoopContext(int tid) const {
    auto it = loop_ctx_.find(tid);
    return it == loop_ctx_.end() ? nullptr : it->second;
}

int SifcmdModule::QueueTid(uint32_t queue) const {
    auto it = queue_tids_.find(queue);
    if (it != queue_tids_.end()) return it->second;
    return rpc_queue_tid_;
}

void SifcmdModule::sceSifSendCmd(IopContext& ctx) {
    // sceSifSendCmd(cid, packet, psize, src, dest, dsize): SifCmdHeader {u32 psize:8 | dsize:24,
    // void* dest, int cid, u32 opt} written into the packet, the data DMA (src -> dest) ahead of
    // the packet DMA into the EE's command buffer, whose interrupt runs the EE's handler.
    const uint32_t cid = ctx.gpr[4];
    const uint32_t packet = ctx.gpr[5];
    const uint32_t psize = ctx.gpr[6];
    const uint32_t src = ctx.gpr[7];
    const uint32_t sp = ctx.gpr[29];
    const uint32_t dest = mem_.Read<uint32_t>(sp + 0x10);
    const uint32_t dsize = mem_.Read<uint32_t>(sp + 0x14);
    if (!packet || psize < 0x10 || psize > 0x70) {
        ctx.gpr[2] = 0;
        return;
    }
    mem_.Write<uint32_t>(packet + 0x0, (psize & 0xFFu) | (dsize << 8));
    mem_.Write<uint32_t>(packet + 0x4, dest);
    mem_.Write<uint32_t>(packet + 0x8, cid);
    if (dsize && src && dest && ee_sink_) {
        if (const uint8_t* bytes = mem_.Pointer(src, dsize)) ee_sink_->WriteEeRam(dest, bytes, dsize);
    }
    const uint8_t* pkt = mem_.Pointer(packet, psize);
    if (pkt && command_sink_) command_sink_(cid, pkt, psize);
    commands_sent_++;
    ctx.gpr[2] = 1;
}

void SifcmdModule::isceSifSendCmd(IopContext& ctx) { sceSifSendCmd(ctx); }

}  // namespace ps2r::iop::hle
