// engine/iop/hle/sifcmd/sifcmd_module.h -- SifcmdModule: the IOP sifcmd RPC
// framework as STREAM.IRX sees it: sceSifInitRpc (idx 14), sceSifBindRpc
// (15), sceSifCallRpc (16), sceSifRegisterRpc (17), sceSifSetRpcQueue (19),
// sceSifRpcLoop (22). Binding completes synchronously (the client's server
// word is written), registration records id/func in the server data, and the
// RPC loop is the kernel's RPC service loop on the running thread. The one
// sceSifCallRpc site is the module's CD-read request to the EE: its body is
// the title protocol's, installed through SetCdReadRpc.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopModuleTable;
}

namespace ps2r::iop::hle {

class SifcmdModule {
public:
    using GuestFunction = std::function<void(IopContext&)>;

    // SifRpcClientData: the server-data word the bind writes (bind success = non-zero).
    static constexpr uint32_t kClientServerWordOffset = 0x24;
    static constexpr uint32_t kFakeServerTag = 0xDEAD0000u;

    SifcmdModule(IopModuleTable& table, IopMemory& mem) : table_(table), mem_(mem) {}

    // The EE-facing CD-read RPC body (sceSifCallRpc's only caller); unset = reply 0.
    void SetCdReadRpc(GuestFunction fn) { cd_read_rpc_ = std::move(fn); }
    bool HasCdReadRpc() const { return static_cast<bool>(cd_read_rpc_); }

    void sceSifInitRpc(IopContext& ctx);      // int (mode) -> 0
    void sceSifBindRpc(IopContext& ctx);      // int (client, server_id, mode) -> 0; writes client+0x24
    void sceSifCallRpc(IopContext& ctx);      // int (...) -> the CD-read body's result (0)
    void hle_stream_cd_read_rpc(IopContext& ctx);   // the recompiler-emitted thunk for the same body
    void sceSifRegisterRpc(IopContext& ctx);  // int (server_data, id, func, buf, ...) -> 0
    void sceSifSetRpcQueue(IopContext& ctx);  // int (queue, tid) -> 0
    void sceSifRpcLoop(IopContext& ctx);      // void (queue): the kernel's RPC service loop

    uint32_t LastRegisteredId() const { return last_registered_id_; }
    uint32_t LastRegisteredFunc() const { return last_registered_func_; }
    int RpcQueueTid() const { return rpc_queue_tid_; }

private:
    IopModuleTable& table_;
    IopMemory& mem_;
    GuestFunction cd_read_rpc_;
    uint32_t last_registered_id_ = 0;
    uint32_t last_registered_func_ = 0;
    int rpc_queue_tid_ = 0;
};

}  // namespace ps2r::iop::hle
