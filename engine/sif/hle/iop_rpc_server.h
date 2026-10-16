#pragma once
// IopRpcServer: the EE-facing RpcRouter::Server of ONE sceSifRegisterRpc record
// a hosted (recompiled) IOP module made -- the generic form of the first
// title's STREAM server, built from what the module told sifcmd instead of
// from title constants. An EE sceSifCallRpc(sid, fno, send, ssize, recv, rsize)
// becomes, on the module's RPC-dispatch thread (SifRpcBridge::RunOnRpcThread
// for the queue's tid): the send bytes copied EE -> IOP into the record's
// `buff` (the SIF DMA ahead of the dispatch), the server function run as
// `void* func(u32 fno, void* buff, int size)` on the register file of the
// thread parked in sceSifRpcLoop (its $sp/$gp; a0/a1/a2, ra = the return
// sentinel), then `rsize` bytes copied IOP -> EE from the returned pointer
// into `recv` (the reply DMA) -- exactly sceSifRpcLoop's dispatch on the
// console. No reply is synthesised: the module's own code answers.
#include <cstdint>
#include <functional>

#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/iop_context.h"
#include "engine/sif/hle/rpc_router.h"

class MainMemory;
namespace ps2r::iop {
class IopMemory;
class SifRpcBridge;
}

class IopRpcServer : public RpcRouter::Server {
public:
    using GuestFunction = std::function<void(ps2r::iop::IopContext&)>;
    using FunctionResolver = std::function<const GuestFunction*(uint32_t addr)>;

    static constexpr uint32_t kReturnSentinel = 0x80001000;   // ra of the server function
    static constexpr uint32_t kScratchFrame = 0x40;           // below the loop's $sp when no record is parked

    IopRpcServer(const ps2r::iop::hle::SifcmdModule::ServerRecord& record, ps2r::iop::IopMemory& iop_mem,
                 MainMemory& ee_ram, ps2r::iop::SifRpcBridge& bridge, ps2r::iop::hle::SifcmdModule& sifcmd,
                 FunctionResolver resolve);
    IopRpcServer(const IopRpcServer&) = delete;
    IopRpcServer& operator=(const IopRpcServer&) = delete;

    void Call(const RpcRouter::CallRequest& req) override;

    const ps2r::iop::hle::SifcmdModule::ServerRecord& Record() const { return record_; }
    uint64_t Calls() const { return calls_; }
    uint64_t SendBytes() const { return send_bytes_; }
    uint64_t RecvBytes() const { return recv_bytes_; }
    uint64_t Unresolved() const { return unresolved_; }
    uint32_t LastReply() const { return last_reply_; }

private:
    ps2r::iop::hle::SifcmdModule::ServerRecord record_;
    ps2r::iop::IopMemory& iop_mem_;
    MainMemory& ee_ram_;
    ps2r::iop::SifRpcBridge& bridge_;
    ps2r::iop::hle::SifcmdModule& sifcmd_;
    FunctionResolver resolve_;
    uint64_t calls_ = 0, send_bytes_ = 0, recv_bytes_ = 0, unresolved_ = 0;
    uint32_t last_reply_ = 0;
};
