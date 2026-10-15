#include "engine/sif/hle/sce_sif.h"

#include <cstdio>

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/sif_state.h"

namespace {

RpcRouter* g_router = nullptr;
SifState* g_sif = nullptr;
SceSif::IopHeap g_heap;
SceSif::EeServer g_ee_server;
SceSif::HandleToServerId g_handle_to_sid;
SceSif::EndCallbackRunner g_blocking_end_runner;
uint64_t g_init_rpc_calls = 0;
SceSif::InitRpcStatics g_init_statics;
SceSif::ModuleLoader g_module_loader;
SceSif::HeapCensus g_heap_census;
char g_park_detail[64];

uint32_t A(const CpuContext& ctx, int n) { return ctx.cpuRegs.GPR.r[4 + n].UL[0]; }
uint32_t Ra(const CpuContext& ctx) { return ctx.cpuRegs.GPR.r[31].UL[0]; }
void Ret(CpuContext& ctx, uint32_t v) { ctx.cpuRegs.GPR.r[2].UD[0] = v; }
void RetS(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].UD[0] = static_cast<uint64_t>(static_cast<int64_t>(v)); }

struct Row {
    const char* name;
    SdkBindingManifest::Handler handler;
};
const Row kRows[] = {
    {"sceSifInitRpc", &SceSif::InitRpc},           {"sceSifInitCmd", &SceSif::InitCmd},
    {"sceSifBindRpc", &SceSif::BindRpc},           {"sceSifCallRpc", &SceSif::CallRpc},
    {"sceSifCheckStatRpc", &SceSif::CheckStatRpc}, {"sceSifRegisterRpc", &SceSif::RegisterRpc},
    {"sceSifSetRpcQueue", &SceSif::SetRpcQueue},   {"sceSifGetNextRequest", &SceSif::GetNextRequest},
    {"sceSifRpcLoop", &SceSif::RpcLoop},           {"sceSifAddCmdHandler", &SceSif::AddCmdHandler},
    {"sceSifLoadModule", &SceSif::LoadModule},     {"_sceSifLoadModule", &SceSif::LoadModule},
    {"_lf_bind", &SceSif::LfBind},                 {"sceSifInitIopHeap", &SceSif::InitIopHeap},
    {"sceSifAllocIopHeap", &SceSif::AllocIopHeap}, {"sceSifFreeIopHeap", &SceSif::FreeIopHeap},
    {"sceSifFreeSysMemory", &SceSif::FreeIopHeap}, {"sceSifWriteBackDCache", &SceSif::WriteBackDCache},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
// The SIF rows no earlier owner held: registered as new rows.
const Row kRegisteredRows[] = {
    {"sceSifRebootIop", &SceSif::RebootIop},
    {"sceSifSyncIop", &SceSif::SyncIop},
    {"sceSifInitRebootNotify", &SceSif::InitRebootNotify},
    {"sceFsSifCallRpc", &SceSif::FsSifCallRpc},
};
const char* g_row_names[kRowCount];

}  // namespace

int SceSif::Bind(SdkBindingManifest& m, RpcRouter& router, SifState& sif, IopHeap heap,
                 HandleToServerId handle_to_sid) {
    g_router = &router;
    g_sif = &sif;
    g_heap = std::move(heap);
    g_handle_to_sid = std::move(handle_to_sid);
    int n = 0;
    for (const Row& r : kRows)
        if (m.Rebind("libkernl.a", r.name, r.handler)) ++n;
    for (const Row& r : kRegisteredRows) m.Register("libkernl.a", r.name, r.handler);
    return n;
}

void SceSif::SetHandleToServerId(HandleToServerId fn) { g_handle_to_sid = std::move(fn); }
void SceSif::SetBlockingEndCallbackRunner(EndCallbackRunner fn) { g_blocking_end_runner = std::move(fn); }
void SceSif::SetInitRpcStatics(InitRpcStatics statics) { g_init_statics = std::move(statics); }

const char* const* SceSif::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; ++i) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

const SceSif::EeServer& SceSif::RegisteredEeServer() { return g_ee_server; }
uint64_t SceSif::InitRpcCalls() { return g_init_rpc_calls; }

// The original completed the IOP handshake in place: the game's later
// sceSifGetReg(SMFLAG) & CMDINIT poll sees the reply at once (sif_hle.cpp:126-131).
// libkernl's own statics (the is_initialized word, the cleared client-queue
// block, the queue/ring and handler words) are seeded on the first call from the
// bound InitRpcStatics -- title data the SDK body writes on retail, so the
// boundary image carries them (P4-SIFINIT-STATICS); a repeat call returns 1.
void SceSif::InitRpc(CpuContext& ctx) {
    ++g_init_rpc_calls;
    if (g_init_statics.init_flag_addr != 0) {
        if (memory::read<uint32_t>(g_init_statics.init_flag_addr) != 0) {
            Ret(ctx, 1);
            return;
        }
        memory::write<uint32_t>(g_init_statics.init_flag_addr, 1);
        for (uint32_t i = 0; i < g_init_statics.queue_clear_size; ++i)
            memory::write<uint8_t>(g_init_statics.queue_clear_addr + i, 0);
        for (const InitRpcStatics::Word& w : g_init_statics.words) memory::write<uint32_t>(w.addr, w.value);
    }
    if (g_sif) g_sif->SetSmflagBits(SifState::kCmdInit | SifState::kBootEnd);
    Ret(ctx, 1);
}

void SceSif::InitCmd(CpuContext& ctx) { Ret(ctx, 0); }
void SceSif::AddCmdHandler(CpuContext& ctx) { Ret(ctx, 0); }

// int sceSifBindRpc(sceSifClientData* cd, u32 sid, u32 mode) (sif_hle.cpp:684-740).
void SceSif::BindRpc(CpuContext& ctx) {
    const uint32_t client = A(ctx, 0);
    const uint32_t sid = A(ctx, 1);
    if (client == 0 || client >= 0x02000000u) {
        RetS(ctx, -1);
        return;
    }
    memory::write<uint32_t>(client + kClientCommand, 0);
    memory::write<uint32_t>(client + kClientServer, ServerHandle(sid));
    memory::write<uint32_t>(client + kClientPacket, kPacketHandle);
    memory::write<uint32_t>(client + kClientNext, 0);
    memory::write<uint32_t>(client + kClientSema, 0xFFFFFFFFu);
    memory::write<uint32_t>(client + kClientUnused, 0);
    memory::write<uint32_t>(client + kClientBuff, 0);
    memory::write<uint32_t>(client + kClientCbuff, 0);
    memory::write<uint32_t>(client + kClientEndFunc, 0);
    memory::write<uint32_t>(client + kClientEndParam, 0);
    if (g_router) g_router->Bind(client, sid);
    Ret(ctx, 0);
}

// int sceSifCallRpc(cd, func, mode, send, send_size, recv, recv_size, end_func, end_param):
// a0-a3 then t0-t3 (the generated code keeps arguments 5-8 in t0-t3) and
// end_param at [sp+0] (sif_hle.cpp:813-932).
void SceSif::CallRpc(CpuContext& ctx) {
    const uint32_t client = A(ctx, 0);
    const uint32_t mode = A(ctx, 2);   // bit 0 = SIF_RPCM_NOWAIT; clear = the caller blocks until the reply
    const uint32_t sp = ctx.cpuRegs.GPR.r[29].UL[0];
    RpcRouter::CallRequest req;
    req.client_addr = client;
    req.func = A(ctx, 1);
    req.send = A(ctx, 3);
    req.send_size = ctx.cpuRegs.GPR.r[8].UL[0];
    req.recv = ctx.cpuRegs.GPR.r[9].UL[0];
    req.recv_size = ctx.cpuRegs.GPR.r[10].UL[0];
    const uint32_t end_func = ctx.cpuRegs.GPR.r[11].UL[0];
    const uint32_t end_param = memory::read<uint32_t>(sp);
    if (g_router) {
        if (const RpcRouter::Binding* b = g_router->FindByClient(client)) {
            req.server_id = b->server_id;
        } else {
            // A client bound before the router existed (a restored anchor):
            // the composition root's hook recovers the id from the handle word.
            const uint32_t handle = memory::read<uint32_t>(client + kClientServer);
            if (g_handle_to_sid) req.server_id = g_handle_to_sid(handle);
        }
    }
    if (!g_router || !g_router->Call(req)) {
        std::snprintf(g_park_detail, sizeof(g_park_detail), "sid=0x%x func=%u client=0x%x", req.server_id, req.func,
                      client);
        GuestPark::Forever("sif_server_unbound", Ra(ctx), g_park_detail);
    }
    if (client != 0) {
        // Completed: sceSifCheckStatRpc's condition (next == cbuff), queued flag cleared.
        memory::write<uint32_t>(client + kClientNext, memory::read<uint32_t>(client + kClientCbuff));
        memory::write<uint32_t>(client + kClientCommand, memory::read<uint32_t>(client + kClientCommand) & ~1u);
        memory::write<uint32_t>(client + kClientEndFunc, end_func);
        memory::write<uint32_t>(client + kClientEndParam, end_param);
        if (end_func != 0) g_router->QueueEndCallback(client);
        // Blocking mode: the end function has run by the time the caller resumes.
        if (end_func != 0 && (mode & 1u) == 0 && g_blocking_end_runner) g_blocking_end_runner(ctx);
        const int32_t sema = memory::read<int32_t>(client + kClientSema);
        if (sema > 0) Semaphores::Instance().Signal(sema);
    }
    Ret(ctx, 0);
}

void SceSif::CheckStatRpc(CpuContext& ctx) { Ret(ctx, 0); }

// sceSifRegisterRpc(sd, sid, func, buff, cfunc, cbuff, qd): the EE-side server
// the IOP's stream notifier calls back into (libkernl.cpp:617-623).
void SceSif::RegisterRpc(CpuContext& ctx) {
    g_ee_server.sid = A(ctx, 1);
    g_ee_server.func = A(ctx, 2);
    g_ee_server.buff = A(ctx, 3);
    g_ee_server.registered = true;
    Ret(ctx, 0);
}

void SceSif::SetRpcQueue(CpuContext& ctx) { Ret(ctx, 0); }
void SceSif::GetNextRequest(CpuContext& ctx) { Ret(ctx, 0); }

// The server thread parks in SleepThread; the EE RPC server duty wakes it
// when a request is pending (libkernl.cpp:597-600).
void SceSif::RpcLoop(CpuContext& ctx) { RetS(ctx, ThreadManager::Instance().SleepThread()); }

void SceSif::LoadModule(CpuContext& ctx) {
    if (!g_module_loader) { Ret(ctx, 0); return; }
    std::string path;
    for (uint32_t a = A(ctx, 0), n = 0; n < 256; ++a, ++n) {
        const char c = static_cast<char>(memory::read<uint8_t>(a));
        if (!c) break;
        path += c;
    }
    RetS(ctx, g_module_loader(path));
}
void SceSif::SetModuleLoader(ModuleLoader fn) { g_module_loader = std::move(fn); }
void SceSif::LfBind(CpuContext& ctx) { Ret(ctx, 1); }
void SceSif::InitIopHeap(CpuContext& ctx) { Ret(ctx, 0); }

void SceSif::AllocIopHeap(CpuContext& ctx) {
    const uint32_t size = A(ctx, 0);
    if (!g_heap.alloc) GuestPark::Forever("iop_heap_unbound", Ra(ctx));
    const uint32_t addr = g_heap.alloc(size);
    ++g_heap_census.allocs;
    if (addr) g_heap_census.bytes += size; else ++g_heap_census.refused;
    RetS(ctx, static_cast<int32_t>(addr));
}

void SceSif::FreeIopHeap(CpuContext& ctx) {
    if (g_heap.free) g_heap.free(A(ctx, 0));
    ++g_heap_census.frees;
    Ret(ctx, 0);
}
SceSif::HeapCensus SceSif::Heap() { return g_heap_census; }

void SceSif::WriteBackDCache(CpuContext& ctx) { Ret(ctx, 0); }

// The IOP is never rebooted: the module table already runs the post-reboot
// set, so the reboot is accepted and the sync reports in-sync at once
// (libkernl.cpp:496-497); the reboot notifier has nothing to notify (:428).
void SceSif::RebootIop(CpuContext& ctx) { Ret(ctx, 1); }
void SceSif::SyncIop(CpuContext& ctx) { Ret(ctx, 1); }
void SceSif::InitRebootNotify(CpuContext& ctx) { Ret(ctx, 0); }
void SceSif::FsSifCallRpc(CpuContext& ctx) { Ret(ctx, 0); }
