// tests/sif/test_ee_rpc_server.cpp -- EeRpcServer: the IOP -> EE RPC round trip
// over a test EE RAM, an IopMemory and the kernel's SifRpcBridge (FRAME).
//
//   1. An IOP worker (kernel coroutine) calls RequestAndWait: the request bytes
//      are copied IOP -> EE into the server's registered `buff`, the request is
//      posted, the worker parks in Wait::Rpc.
//   2. The EE services it (slot 6): the server function runs as a guest call
//      with a0 = fno, a1 = buff, a2 = size, ra = 0x80001000, and EVERY GPR plus
//      the pc are restored afterwards; v0's recv_size bytes are copied EE -> IOP
//      into the worker's recv buffer; NotifyEeReply wakes exactly the Wait::Rpc
//      parkee, which resumes with the reply in place (true).
//   3. Rejections: no EE server registered / a sid mismatch -> false at once
//      (Rejected()); both transfer sizes clamp to 0x100; an expired deadline
//      withdraws the request (Timeouts(); a late Service finds nothing, a late
//      reply is dropped).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/iop_module_table.h"
#include "engine/platform/host/platform.h"
#include "engine/sif/hle/ee_rpc_server.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/sif/sif_state.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;
using ps2r::iop::IopMemory;
using ps2r::iop::IopModuleTable;

static int g_fail = 0;
#define CHECK(c, msg)                                                                          \
    do {                                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; }      \
    } while (0)

namespace {
constexpr uint32_t kSid = 0x00001234;        // the sid the EE registers (any value; the module's own knowledge)
constexpr uint32_t kFunc = 0x00123450;       // the server function's address (never executed: the guest call is a lambda)
constexpr uint32_t kBuff = 0x00310000;       // the server's request buffer in EE RAM
constexpr uint32_t kReply = 0x00320000;      // where the server function builds its reply
constexpr uint32_t kSendIop = 0x00041000, kRecvIop = 0x00042000;  // the worker's buffers in IOP RAM
constexpr uint32_t kSendBytes = 0x38, kRecvBytes = 0x2C;

uint8_t Pattern(uint32_t seed, uint32_t i) { return static_cast<uint8_t>((seed * 29u + i * 5u + (i >> 2)) & 0xFFu); }

// What the guest server function observed (the lambda standing in for the recompiled func).
struct GuestSeen {
    int calls = 0;
    uint32_t addr = 0, fno = 0, buff = 0, size = 0, ra = 0;
    uint8_t request[EeRpcServer::kMaxTransfer] = {};
};

void RegisterEeServer(uint32_t sid, uint32_t func, uint32_t buff) {
    CpuContext ctx{};
    ctx.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    ctx.cpuRegs.GPR.r[4].UL[0] = 0x00330000;  // sd
    ctx.cpuRegs.GPR.r[5].UL[0] = sid;
    ctx.cpuRegs.GPR.r[6].UL[0] = func;
    ctx.cpuRegs.GPR.r[7].UL[0] = buff;
    SceSif::RegisterRpc(ctx);
}
}  // namespace

int main() {
    ps2r::platform::host::RegisterSelf();
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    IopMemory iop;
    RpcRouter router;
    SifState sif;
    SceSif::Bind(SdkBindingManifest::Instance(), router, sif, SceSif::IopHeap{});

    GuestSeen seen;
    EeRpcServer server(
        SceSif::RegisteredEeServer(), iop, m.Rpc(), [](uint32_t a) { return memory::read<uint8_t>(a); },
        [](uint32_t a, uint8_t v) { memory::write<uint8_t>(a, v); },
        [&seen](uint32_t addr, CpuContext& ctx) {
            // void* func(u32 fno, void* buff, int size): record the call, clobber
            // every register (the restore is what the test proves), build the reply.
            ++seen.calls;
            seen.addr = addr;
            seen.fno = ctx.cpuRegs.GPR.r[4].UL[0];
            seen.buff = ctx.cpuRegs.GPR.r[5].UL[0];
            seen.size = ctx.cpuRegs.GPR.r[6].UL[0];
            seen.ra = ctx.cpuRegs.GPR.r[31].UL[0];
            for (uint32_t i = 0; i < seen.size && i < EeRpcServer::kMaxTransfer; ++i) seen.request[i] = memory::read<uint8_t>(seen.buff + i);
            for (uint32_t i = 0; i < EeRpcServer::kMaxTransfer + 8; ++i) memory::write<uint8_t>(kReply + i, Pattern(seen.fno, i));
            for (int r = 1; r < 32; ++r) ctx.cpuRegs.GPR.r[r].UD[0] = 0xBAD0000000000000ull | static_cast<uint64_t>(r);
            ctx.cpuRegs.pc = 0xDEADBEEF;
            ctx.cpuRegs.GPR.r[2].UD[0] = kReply;  // v0 = the reply pointer
        });

    // ---- rejection before any registration -----------------------------------
    int rejected_early = -1;
    k.RegisterThread(2, 20);
    k.StartThread(2, [&] {
        rejected_early = server.RequestAndWait(kSid, 1, kSendIop, 8, kRecvIop, 8) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    m.OnModuleLoaded(0);  // boots the table: every started thread runs to its first block
    CHECK(rejected_early == 0 && server.Rejected() == 1, "no EE server registered: RequestAndWait returns false at once (Rejected 1)");
    CHECK(k.ThreadWaitKind(2) == IopKernel::WaitKind::Sleep && m.Rpc().EeWaitTid() == 0, "the worker never parked in Wait::Rpc");

    // ---- the round trip ---------------------------------------------------------
    RegisterEeServer(kSid, kFunc, kBuff);
    CHECK(SceSif::RegisteredEeServer().registered && SceSif::RegisteredEeServer().sid == kSid, "sceSifRegisterRpc recorded sid/func/buff");
    for (uint32_t i = 0; i < kSendBytes; ++i) iop.Write<uint8_t>(kSendIop + i, Pattern(3, i));
    for (uint32_t i = 0; i < EeRpcServer::kMaxTransfer + 8; ++i) iop.Write<uint8_t>(kRecvIop + i, 0xEE);
    memory::write<uint8_t>(kBuff + kSendBytes, 0xEE);  // guard past the request
    int woken = -1;
    k.RegisterThread(3, 20);
    k.StartThread(3, [&] {
        woken = server.RequestAndWait(kSid, 7, kSendIop, kSendBytes, kRecvIop, kRecvBytes) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    k.Dispatch();  // the started worker runs to its first block (the table is already booted)
    CHECK(k.ThreadWaitKind(3) == IopKernel::WaitKind::Rpc && m.Rpc().EeWaitTid() == 3, "the worker parked in Wait::Rpc");
    CHECK(server.Pending(), "the request is posted");
    bool req_copied = true;
    for (uint32_t i = 0; i < kSendBytes; ++i) req_copied = req_copied && memory::read<uint8_t>(kBuff + i) == Pattern(3, i);
    CHECK(req_copied && memory::read<uint8_t>(kBuff + kSendBytes) == 0xEE, "the request bytes are in the server's buff byte for byte, no over-run");
    m.PumpAtBoundary();
    CHECK(woken == -1 && k.ThreadWaitKind(3) == IopKernel::WaitKind::Rpc, "a boundary without service leaves the worker parked (deadline 120 ms)");

    // The EE services at slot 6 with a fully stamped context.
    CpuContext ctx{};
    for (int r = 0; r < 32; ++r) ctx.cpuRegs.GPR.r[r].UD[0] = 0x1000000000000000ull * static_cast<uint64_t>(r % 15 + 1) + static_cast<uint64_t>(r);
    ctx.cpuRegs.GPR.r[0].UD[0] = 0;
    ctx.cpuRegs.pc = 0x00200ABC;
    const auto stamped = ctx.cpuRegs.GPR;
    CHECK(server.Service(ctx), "Service serves the posted request");
    CHECK(seen.calls == 1 && seen.addr == kFunc, "the server function ran once at its registered address");
    CHECK(seen.fno == 7 && seen.buff == kBuff && seen.size == kSendBytes, "a0 = fno, a1 = buff, a2 = size");
    CHECK(seen.ra == EeRpcServer::kReturnSentinel, "ra = 0x80001000");
    bool restored = ctx.cpuRegs.pc == 0x00200ABC;
    for (int r = 0; r < 32; ++r) restored = restored && ctx.cpuRegs.GPR.r[r].UD[0] == stamped.r[r].UD[0] && ctx.cpuRegs.GPR.r[r].UD[1] == stamped.r[r].UD[1];
    CHECK(restored, "every GPR and the pc are restored after the guest call");
    CHECK(!server.Pending() && server.Served() == 1, "served: nothing pending, Served 1");
    bool reply_copied = true;
    for (uint32_t i = 0; i < kRecvBytes; ++i) reply_copied = reply_copied && iop.Read<uint8_t>(kRecvIop + i) == Pattern(7, i);
    CHECK(reply_copied && iop.Read<uint8_t>(kRecvIop + kRecvBytes) == 0xEE, "recv_size reply bytes copied EE -> IOP byte for byte, no over-run");
    CHECK(woken == -1, "the worker has not run yet (the wake lands at the slot-6 pump)");
    m.PumpAfterEeService();
    CHECK(woken == 1, "NotifyEeReply woke the Wait::Rpc parkee: RequestAndWait returned true");
    CHECK(k.ThreadWaitKind(3) == IopKernel::WaitKind::Sleep && m.Rpc().EeWaitTid() == 0, "the worker moved on to its next park");
    CHECK(!server.Service(ctx), "a second Service with nothing posted serves nothing");
    std::printf("round trip: fno %u buff %08x size %u ra %08x; served %llu rejected %llu timeouts %llu\n", seen.fno, seen.buff, seen.size,
                seen.ra, (unsigned long long)server.Served(), (unsigned long long)server.Rejected(), (unsigned long long)server.Timeouts());

    // ---- sid mismatch -------------------------------------------------------------
    int mismatch = -1;
    k.RegisterThread(4, 20);
    k.StartThread(4, [&] {
        mismatch = server.RequestAndWait(kSid + 1, 1, kSendIop, 8, kRecvIop, 8) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    k.Dispatch();
    CHECK(mismatch == 0 && server.Rejected() == 2, "a sid mismatch is rejected at once (Rejected 2)");

    // ---- the 0x100 clamp on both sizes ----------------------------------------------
    for (uint32_t i = 0; i < 0x200; ++i) iop.Write<uint8_t>(kSendIop + i, Pattern(5, i));
    for (uint32_t i = 0; i < 0x200; ++i) iop.Write<uint8_t>(kRecvIop + i, 0xEE);
    for (uint32_t i = 0; i < 0x200; ++i) memory::write<uint8_t>(kBuff + i, 0xEE);
    int clamp_woken = -1;
    k.RegisterThread(5, 20);
    k.StartThread(5, [&] {
        clamp_woken = server.RequestAndWait(kSid, 9, kSendIop, 0x200, kRecvIop, 0x200) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    k.Dispatch();
    CHECK(k.ThreadWaitKind(5) == IopKernel::WaitKind::Rpc, "the clamped request parks the worker");
    CHECK(memory::read<uint8_t>(kBuff + 0xFF) == Pattern(5, 0xFF) && memory::read<uint8_t>(kBuff + 0x100) == 0xEE,
          "send_size clamps to 0x100: byte 0xFF copied, byte 0x100 untouched");
    CHECK(server.Service(ctx) && seen.size == 0x100, "the server function sees a2 = 0x100");
    CHECK(iop.Read<uint8_t>(kRecvIop + 0xFF) == Pattern(9, 0xFF) && iop.Read<uint8_t>(kRecvIop + 0x100) == 0xEE,
          "recv_size clamps to 0x100: byte 0xFF copied, byte 0x100 untouched");
    m.PumpAfterEeService();
    CHECK(clamp_woken == 1 && server.Served() == 2, "the clamped call completes (Served 2)");

    // ---- deadline withdrawal ---------------------------------------------------------
    int expired = -1;
    k.RegisterThread(6, 20);
    k.StartThread(6, [&] {
        expired = server.RequestAndWait(kSid, 11, kSendIop, 8, kRecvIop, 8) ? 1 : 0;
        for (;;) k.SleepThread();
    });
    k.Dispatch();
    CHECK(server.Pending() && k.ThreadWaitKind(6) == IopKernel::WaitKind::Rpc, "the request is posted and the worker parked");
    int boundaries = 0;
    while (expired == -1 && boundaries < 64) {  // one frame quantum per boundary; the deadline is 120 ms
        m.PumpAtBoundary();
        ++boundaries;
    }
    CHECK(expired == 0 && boundaries >= 7 && boundaries <= 9, "the deadline expires after ~8 frame boundaries: RequestAndWait returns false");
    CHECK(!server.Pending() && server.Timeouts() == 1, "the expired request is withdrawn (Timeouts 1)");
    CHECK(!server.Service(ctx) && seen.calls == 2 && server.Served() == 2, "a late Service finds nothing to serve");
    CHECK(k.ThreadWaitKind(6) == IopKernel::WaitKind::Sleep, "the worker went on to its next park");
    m.Rpc().NotifyEeReply();  // a late reply
    m.PumpAfterEeService();
    CHECK(k.ThreadWaitKind(6) == IopKernel::WaitKind::Sleep && m.Rpc().EeWaitTid() == 0, "a late reply is dropped: the worker's Sleep is not released");
    std::printf("deadline: expired after %d boundaries; served %llu rejected %llu timeouts %llu\n", boundaries,
                (unsigned long long)server.Served(), (unsigned long long)server.Rejected(), (unsigned long long)server.Timeouts());
    CHECK(iop.UnroutedCount() == 0, "no IOP access fell outside IOP RAM");

    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
