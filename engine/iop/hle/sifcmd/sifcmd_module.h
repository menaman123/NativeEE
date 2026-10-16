// engine/iop/hle/sifcmd/sifcmd_module.h -- SifcmdModule: the IOP sifcmd RPC
// framework as a hosted module sees it: sceSifInitRpc (idx 14), sceSifBindRpc
// (15), sceSifCallRpc (16), sceSifRegisterRpc (17), sceSifSetRpcQueue (19),
// sceSifRpcLoop (22), sceSifGetOtherData (23), sceSifRemoveRpc (24),
// sceSifRemoveRpcQueue (25), sceSifSendCmd (12) / isceSifSendCmd (13). Binding completes synchronously (the client's
// server word is written); registration fills the SifRpcServerData record
// (sid/func/buff/cfunc/cbuff) and keeps it with its queue's thread, telling
// the composition's registrar (which puts an EE-facing server for that id on
// the RpcRouter); the RPC loop is the kernel's RPC service loop on the running
// thread, whose register file is kept so the EE-driven dispatch runs the
// server function on that thread's own stack and $gp. sceSifGetOtherData
// copies EE RAM into IOP RAM through the bound EeRamSource. The one
// sceSifCallRpc site of the first title is the module's CD-read request to
// the EE: its body is the title protocol's, installed through SetCdReadRpc.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class EeRamSource;
class EeRamSink;

class SifcmdModule {
public:
    using GuestFunction = std::function<void(IopContext&)>;

    // SifRpcClientData: the server-data word the bind writes (bind success = non-zero).
    static constexpr uint32_t kClientServerWordOffset = 0x24;
    static constexpr uint32_t kFakeServerTag = 0xDEAD0000u;
    // SifRpcServerData (SDK): +0 sid, +4 func, +8 buff, +0x10 cfunc, +0x14 cbuff, +0x40 base (queue).
    static constexpr uint32_t kServerSid = 0x00, kServerFunc = 0x04, kServerBuff = 0x08, kServerCfunc = 0x10,
                              kServerCbuff = 0x14, kServerBase = 0x40;
    // SifRpcDataQueue (SDK): +0 thread_id, +4 active.
    static constexpr uint32_t kQueueThreadId = 0x00, kQueueActive = 0x04;

    // One sceSifRegisterRpc record, with the thread of the queue it was put on.
    struct ServerRecord {
        uint32_t server_data = 0;
        uint32_t sid = 0, func = 0, buff = 0, cfunc = 0, cbuff = 0, queue = 0;
        int tid = 0;
    };
    using Registrar = std::function<void(const ServerRecord&)>;
    using Unregistrar = std::function<void(uint32_t sid)>;
    // The EE side of sceSifSendCmd: a command packet (its SifCmdHeader filled: size,
    // dest, cid) for the EE's command buffer + the handler registered for `cid`.
    using CommandSink = std::function<void(uint32_t cid, const uint8_t* packet, uint32_t size)>;

    SifcmdModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    // The EE-facing CD-read RPC body (sceSifCallRpc's only caller); unset = reply 0.
    void SetCdReadRpc(GuestFunction fn) { cd_read_rpc_ = std::move(fn); }
    bool HasCdReadRpc() const { return static_cast<bool>(cd_read_rpc_); }
    // The composition's server registrar / unregistrar (RpcRouter side).
    void SetRegistrar(Registrar reg, Unregistrar unreg = {}) {
        registrar_ = std::move(reg);
        unregistrar_ = std::move(unreg);
    }
    // EE RAM reads for sceSifGetOtherData (unbound: the call answers 0 bytes, result -1).
    void SetEeRamSource(EeRamSource* src) { ee_source_ = src; }
    // EE RAM writes for sceSifSendCmd's data part, and the command packet's destination.
    void SetEeRamSink(EeRamSink* sink) { ee_sink_ = sink; }
    void SetCommandSink(CommandSink sink) { command_sink_ = std::move(sink); }

    void sceSifInitRpc(IopContext& ctx);      // int (mode) -> 0
    void sceSifBindRpc(IopContext& ctx);      // int (client, server_id, mode) -> 0; writes client+0x24
    void sceSifCallRpc(IopContext& ctx);      // int (...) -> the CD-read body's result (0)
    void hle_stream_cd_read_rpc(IopContext& ctx);   // the recompiler-emitted thunk for the same body
    void sceSifRegisterRpc(IopContext& ctx);  // int (sd, sid, func, buff, cfunc, cbuff, qd) -> 0
    void sceSifSetRpcQueue(IopContext& ctx);  // int (queue, tid) -> 0
    void sceSifRpcLoop(IopContext& ctx);      // void (queue): the kernel's RPC service loop
    void sceSifGetOtherData(IopContext& ctx); // int (rd, ee_src, iop_dst, size, mode) -> 0 / -1
    void sceSifRemoveRpc(IopContext& ctx);    // sd* (sd, qd) -> sd
    void sceSifRemoveRpcQueue(IopContext& ctx);   // qd* (qd) -> qd
    // u32 (cid, packet, psize, src, dest, dsize) -> dma id: the data part copied src -> dest
    // (EE), the packet's header filled and the packet handed to the EE's command buffer (idx 12).
    void sceSifSendCmd(IopContext& ctx);
    void isceSifSendCmd(IopContext& ctx);         // the interrupt-context form (idx 13)
    uint64_t CommandsSent() const { return commands_sent_; }

    uint32_t LastRegisteredId() const { return last_registered_id_; }
    uint32_t LastRegisteredFunc() const { return last_registered_func_; }
    int RpcQueueTid() const { return rpc_queue_tid_; }
    const std::vector<ServerRecord>& Servers() const { return servers_; }
    const ServerRecord* FindServer(uint32_t sid) const;
    // The register file of the thread parked in sceSifRpcLoop (nullptr = none).
    const IopContext* LoopContext(int tid) const;
    int QueueTid(uint32_t queue) const;
    uint64_t OtherDataBytes() const { return other_data_bytes_; }

private:
    IopModuleTable& table_;
    IopMemory& mem_;
    GuestFunction cd_read_rpc_;
    Registrar registrar_;
    Unregistrar unregistrar_;
    EeRamSource* ee_source_ = nullptr;
    EeRamSink* ee_sink_ = nullptr;
    CommandSink command_sink_;
    uint64_t commands_sent_ = 0;
    uint32_t last_registered_id_ = 0;
    uint32_t last_registered_func_ = 0;
    int rpc_queue_tid_ = 0;
    std::map<uint32_t, int> queue_tids_;          // queue descriptor -> thread
    std::vector<ServerRecord> servers_;
    std::map<int, const IopContext*> loop_ctx_;   // tid -> the register file parked in RpcLoop
    uint64_t other_data_bytes_ = 0;
};

}  // namespace ps2r::iop::hle
