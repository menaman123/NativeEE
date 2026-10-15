// EeRpcServer implementation: the IOP -> EE RPC round trip as two copies, one
// guest call and one wakeup (see ee_rpc_server.h).
#include "engine/sif/hle/ee_rpc_server.h"

#include <utility>

#include "engine/iop/iop_memory.h"
#include "engine/iop/sif_rpc_bridge.h"

EeRpcServer::EeRpcServer(const SceSif::EeServer& record, ps2r::iop::IopMemory& iop_mem,
                         ps2r::iop::SifRpcBridge& bridge, EeRead ee_read, EeWrite ee_write, GuestCall guest_call)
    : record_(record),
      iop_mem_(iop_mem),
      bridge_(bridge),
      ee_read_(std::move(ee_read)),
      ee_write_(std::move(ee_write)),
      guest_call_(std::move(guest_call)) {}

bool EeRpcServer::Pending() const {
    std::lock_guard<std::mutex> lk(m_);
    return pending_;
}

bool EeRpcServer::RequestAndWait(uint32_t sid, uint32_t fno, uint32_t send_iop, uint32_t send_size,
                                 uint32_t recv_iop, uint32_t recv_size) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!record_.registered || record_.sid != sid || record_.func == 0) {
            ++rejected_;
            return false;
        }
        if (send_size > kMaxTransfer) send_size = kMaxTransfer;
        if (recv_size > kMaxTransfer) recv_size = kMaxTransfer;
        // The request bytes land in the EE server's buffer ahead of the
        // dispatch, as the SIF DMA moves them.
        const uint32_t buff = record_.buff & kEeRamMask;
        for (uint32_t i = 0; i < send_size; ++i) ee_write_(buff + i, iop_mem_.Read<uint8_t>(send_iop + i));
        req_ = Request{fno, send_size, recv_iop, recv_size};
        pending_ = true;
        done_ = false;
    }
    // Park the worker until the EE services the request or the deadline passes.
    const bool woken = bridge_.WaitEeReplyUs(kReplyDeadlineUs);
    std::lock_guard<std::mutex> lk(m_);
    if (!woken || !done_) {
        // Expired: withdraw so a late service finds nothing to serve.
        pending_ = false;
        done_ = false;
        ++timeouts_;
        return false;
    }
    done_ = false;
    return true;
}

bool EeRpcServer::Service(CpuContext& ctx) {
    Request req;
    uint32_t func = 0, buff = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!pending_) return false;
        pending_ = false;
        req = req_;
        func = record_.func;
        buff = record_.buff;
    }
    // void* func(u32 fno, void* buff, int size) on the boundary thread's
    // context: every GPR and the pc are put back afterwards.
    const auto saved_gpr = ctx.cpuRegs.GPR;
    const uint32_t saved_pc = ctx.cpuRegs.pc;
    ctx.cpuRegs.GPR.r[4].UD[0] = req.fno;
    ctx.cpuRegs.GPR.r[5].UD[0] = buff;
    ctx.cpuRegs.GPR.r[6].UD[0] = req.send_size;
    ctx.cpuRegs.GPR.r[31].UD[0] = kReturnSentinel;
    guest_call_(func, ctx);
    const uint32_t reply_ptr = ctx.cpuRegs.GPR.r[2].UL[0];
    ctx.cpuRegs.GPR = saved_gpr;
    ctx.cpuRegs.pc = saved_pc;

    // The reply bytes go back into the worker's recv buffer (the end-of-RPC DMA).
    if (reply_ptr != 0 && req.recv_iop != 0) {
        const uint32_t src = reply_ptr & kEeRamMask;
        for (uint32_t i = 0; i < req.recv_size; ++i) iop_mem_.Write<uint8_t>(req.recv_iop + i, ee_read_(src + i));
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        done_ = true;
        ++served_;
    }
    bridge_.NotifyEeReply();
    return true;
}
