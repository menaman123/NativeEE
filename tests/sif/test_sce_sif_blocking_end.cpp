// tests/sif/test_sce_sif_blocking_end.cpp -- SceSifBlockingEndFunction: the end
// function of a BLOCKING sceSifCallRpc (mode bit 0 = SIF_RPCM_NOWAIT clear) has
// run by the time the caller resumes; a NOWAIT call's end function waits for the
// completion duty. libkernl 3.0.1 (SLUS_123.45 0x2d29a8) stores end_func and
// end_param in the client record in both modes and its RPC-end handler runs the
// end function before signalling the blocked caller's semaphore -- the title's
// free-IOP-memory query (FUN_002AD8B8) reads the reply's processed copy right
// after its blocking Ship(0). The runner is the composition root's
// RpcRouter::CompletePending; unbound (boundary mode) nothing runs at the call.
#include <cstdio>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/sif/sif_state.h"

static int g_fail = 0;
#define CHECK(c, msg)                                                                          \
    do {                                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; }      \
    } while (0)

namespace {
struct TestSceSifBlockingEnd : RpcRouter::Server {   // the recording server face (primary type of this file)
    int calls = 0;
    void Call(const RpcRouter::CallRequest&) override { ++calls; }
};
constexpr uint32_t kClient = 0x00200000, kSp = 0x00201000, kSend = 0x00202000, kRecv = 0x00203000;
constexpr uint32_t kEndFunc = 0x00123450, kEndParam = 0x77;
constexpr uint32_t kServer = 0x12345;

void SetArg(CpuContext& c, int r, uint32_t v) { c.cpuRegs.GPR.r[r].UD[0] = v; }

// sceSifCallRpc(client, fno 0, mode, send, 0x10, recv, 0x10, end_func); [sp] = end_param.
void CallRpc(CpuContext& ctx, uint32_t mode, uint32_t end_func) {
    memory::write<uint32_t>(kSp, kEndParam);
    SetArg(ctx, 29, kSp);
    SetArg(ctx, 4, kClient);
    SetArg(ctx, 5, 0);
    SetArg(ctx, 6, mode);
    SetArg(ctx, 7, kSend);
    SetArg(ctx, 8, 0x10);
    SetArg(ctx, 9, kRecv);
    SetArg(ctx, 10, 0x10);
    SetArg(ctx, 11, end_func);
    SetArg(ctx, 31, 0x00100100);
    SceSif::CallRpc(ctx);
}
}  // namespace

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    RpcRouter router;
    SifState sif;
    TestSceSifBlockingEnd server;
    router.RegisterServer(kServer, server, "STREAM");
    SceSif::Bind(SdkBindingManifest::Instance(), router, sif, SceSif::IopHeap{});

    CpuContext ctx{};
    SetArg(ctx, 29, kSp);
    SetArg(ctx, 4, kClient);
    SetArg(ctx, 5, kServer);
    SetArg(ctx, 6, 0);
    SceSif::BindRpc(ctx);
    memory::write<int32_t>(kClient + SceSif::kClientSema, -1);

    // The runner the root binds: delivers the queued end callbacks through a
    // recording guest call (the callback's a0 and ra as CompletePending prepares them).
    std::vector<uint32_t> ran_addr, ran_a0;
    SceSif::SetBlockingEndCallbackRunner([&](CpuContext& c) {
        router.CompletePending(
            c,
            [&](uint32_t addr, CpuContext& cc) {
                ran_addr.push_back(addr);
                ran_a0.push_back(cc.cpuRegs.GPR.r[4].UL[0]);
                CHECK(cc.cpuRegs.GPR.r[31].UL[0] == RpcRouter::kReturnSentinel, "callback ra = the return sentinel");
            },
            [](uint32_t a) { return memory::read<uint32_t>(a); });
    });

    // 1. Blocking (mode 0) with an end function: it ran inside the call, nothing stays queued.
    CallRpc(ctx, 0, kEndFunc);
    CHECK(server.calls == 1, "the server face ran once");
    CHECK(ran_addr.size() == 1 && ran_addr[0] == kEndFunc, "blocking: the end function ran before the call returned");
    CHECK(ran_a0.size() == 1 && ran_a0[0] == kEndParam, "blocking: a0 = end_param");
    CHECK(router.PendingCount() == 0, "blocking: no end callback left for the boundary duty");
    CHECK(ctx.cpuRegs.GPR.r[2].UL[0] == 0, "sceSifCallRpc returns 0");
    CHECK(memory::read<uint32_t>(kClient + SceSif::kClientEndFunc) == kEndFunc, "end_func stored in the client record");
    CHECK(memory::read<uint32_t>(kClient + SceSif::kClientEndParam) == kEndParam, "end_param stored in the client record");

    // 2. NOWAIT (mode 1): the end function is queued, not run at the call.
    CallRpc(ctx, 1, kEndFunc);
    CHECK(server.calls == 2, "the server face ran again");
    CHECK(ran_addr.size() == 1, "nowait: nothing ran at the call");
    CHECK(router.PendingCount() == 1, "nowait: one end callback queued for the duty");
    router.CompletePending(
        ctx, [&](uint32_t addr, CpuContext&) { ran_addr.push_back(addr); },
        [](uint32_t a) { return memory::read<uint32_t>(a); });
    CHECK(ran_addr.size() == 2 && router.PendingCount() == 0, "nowait: the duty delivers it");

    // 3. Blocking with no end function: nothing to run, nothing queued.
    CallRpc(ctx, 0, 0);
    CHECK(server.calls == 3 && ran_addr.size() == 2 && router.PendingCount() == 0, "blocking, end_func 0: nothing");

    // 4. Unbound runner (boundary mode): a blocking call queues as before.
    SceSif::SetBlockingEndCallbackRunner({});
    CallRpc(ctx, 0, kEndFunc);
    CHECK(ran_addr.size() == 2 && router.PendingCount() == 1, "no runner: the blocking call's end function waits for the duty");

    if (g_fail == 0) std::printf("ALL PASSED\n");
    return g_fail ? 1 : 0;
}
