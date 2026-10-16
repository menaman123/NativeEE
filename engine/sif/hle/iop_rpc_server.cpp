#include "engine/sif/hle/iop_rpc_server.h"

#include <cstdio>
#include <cstring>

#include "engine/ee/mem/main_memory.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/sif_rpc_bridge.h"

IopRpcServer::IopRpcServer(const ps2r::iop::hle::SifcmdModule::ServerRecord& record, ps2r::iop::IopMemory& iop_mem,
                           MainMemory& ee_ram, ps2r::iop::SifRpcBridge& bridge, ps2r::iop::hle::SifcmdModule& sifcmd,
                           FunctionResolver resolve)
    : record_(record), iop_mem_(iop_mem), ee_ram_(ee_ram), bridge_(bridge), sifcmd_(sifcmd), resolve_(std::move(resolve)) {}

void IopRpcServer::Call(const RpcRouter::CallRequest& req) {
    calls_++;
    const int tid = record_.tid;
    bridge_.RunOnRpcThread(
        [this, req] {
            using ps2r::iop::IopContext;
            // 1. The request DMA: send bytes into the record's receive buffer.
            if (record_.buff && req.send && req.send_size) {
                const uint8_t* src = ee_ram_.Pointer(MainMemory::Phys(req.send), req.send_size);
                uint8_t* dst = iop_mem_.Pointer(record_.buff, req.send_size);
                if (src && dst) {
                    std::memcpy(dst, src, req.send_size);
                    send_bytes_ += req.send_size;
                }
            }
            // 2. The dispatch: func(fno, buff, size) on the RPC thread's own register file.
            const GuestFunction* fn = resolve_ ? resolve_(record_.func) : nullptr;
            if (!fn) {
                unresolved_++;
                std::fprintf(stderr, "iop: RPC server 0x%08x: no recompiled function at 0x%08x\n", record_.sid,
                             record_.func);
                return;
            }
            IopContext ctx;
            if (const IopContext* loop = sifcmd_.LoopContext(record_.tid)) {
                ctx = *loop;
                ctx.gpr[29] -= kScratchFrame;
            } else {
                ctx = IopContext::ForEntry(0, 0);
            }
            ctx.gpr[4] = req.func;
            ctx.gpr[5] = record_.buff;
            ctx.gpr[6] = req.send_size;
            ctx.gpr[31] = kReturnSentinel;
            ctx.pc = record_.func;
            (*fn)(ctx);
            // 3. The reply DMA: rsize bytes from the returned pointer into the client's recv buffer.
            const uint32_t reply = ctx.gpr[2];
            last_reply_ = reply;
            if (reply && req.recv && req.recv_size) {
                const uint8_t* src = iop_mem_.Pointer(reply, req.recv_size);
                uint8_t* dst = ee_ram_.Pointer(MainMemory::Phys(req.recv), req.recv_size);
                if (src && dst) {
                    std::memcpy(dst, src, req.recv_size);
                    recv_bytes_ += req.recv_size;
                }
            }
        },
        tid);
}
