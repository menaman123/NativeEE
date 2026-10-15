// tests/sound/test_libsd_module.cpp -- LibsdModule over SpuCore: the voice
// transfer moves bytes IOP RAM -> SPU RAM and back, the registered handler
// fires as a DEFERRED kernel event (never inside SdVoiceTrans) with
// (a0 = chan, a1 = arg), a status poll retires a pending completion inline,
// and SdProcBatch is the real batch (the BUG-003 pitch-restore vector: SET
// records applied, GET records stored into rets). Then the libsdr surface:
// SdrRemote::Dispatch over the same core, the SDRDRV RPC face, SdBlockTrans
// on the mixer clock, and the LibsdrBindings rows through the manifest.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"
#include "engine/sound/hle/libsdr_bindings.h"
#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/hle/sdr_remote.h"
#include "engine/sound/spu_core.h"

using ps2r::iop::IopContext;
using ps2r::iop::IopKernel;
using ps2r::iop::IopMemory;
using ps2r::iop::hle::LibsdModule;
using ps2r::sound::SpuCore;
using ps2r::sound::hle::LibsdrBindings;
using ps2r::sound::hle::SdBlockTrans;
using ps2r::sound::hle::SdrRemote;

static int g_failures = 0;
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static constexpr uint32_t kHandlerAddr = 0x13a8;   // a module-relative handler address
static constexpr uint32_t kStack = 0x100000;
static constexpr uint32_t kSrc = 0x20000, kDst = 0x30000, kSpuAddr = 0x8000, kBytes = 0x400;

static void Call(LibsdModule& m, int ord, IopContext& ctx, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    ctx.gpr[4] = a0; ctx.gpr[5] = a1; ctx.gpr[6] = a2; ctx.gpr[7] = a3;
    m.Call(ord, ctx);
}

static void TestLibsd() {
    IopMemory mem;
    SpuCore core;
    IopKernel kernel;
    LibsdModule libsd(mem, core);
    struct Fired { uint32_t addr, a0, a1, sp; };
    std::vector<Fired> fired;
    libsd.SetCompletion(&kernel, [&fired](uint32_t addr, IopContext& c) { fired.push_back({addr, c.gpr[4], c.gpr[5], c.gpr[29]}); });
    IopContext ctx;
    ctx.gpr[29] = kStack;

    Call(libsd, 0, ctx, 0);   // SdInit
    CHECK(core.SpuRam()[SpuCore::kSilenceBlockAddr + 1] == 0x03);
    CHECK(ctx.gpr[2] == 0);
    Call(libsd, 15, ctx, 0, kHandlerAddr, 0xCAFE);   // SdSetTransIntrHandler(core 0)
    uint32_t h = 0, a = 0;
    CHECK(core.GetTransIntrHandler(0, h, a) && h == kHandlerAddr && a == 0xCAFE);

    // VoiceTrans write: 0x400 bytes IOP -> SPU; the fifth argument on the stack.
    for (uint32_t i = 0; i < kBytes; i++) mem.Write<uint8_t>(kSrc + i, static_cast<uint8_t>(i * 7 + 3));
    mem.Write<uint32_t>(kStack + 0x10, kBytes);
    Call(libsd, 9, ctx, 0, 0, kSrc, kSpuAddr);
    CHECK(ctx.gpr[2] == kBytes);
    CHECK(std::memcmp(core.SpuRam() + kSpuAddr, mem.Pointer(kSrc, kBytes), kBytes) == 0);
    CHECK(libsd.VoiceTransBytes() == kBytes);
    CHECK(fired.empty());                 // never inside the call
    CHECK(libsd.CompletionPending(0));
    CHECK(libsd.PendingSequence(0) == 1);
    // The kernel's dispatch delivers the completion: a0 = chan, a1 = arg, sp = scratch.
    kernel.Dispatch();
    CHECK(fired.size() == 1);
    if (!fired.empty()) {
        CHECK(fired[0].addr == kHandlerAddr && fired[0].a0 == 0 && fired[0].a1 == 0xCAFE);
        CHECK(fired[0].sp == LibsdModule::kScratchStack);
    }
    CHECK(!libsd.CompletionPending(0));
    CHECK(libsd.HandlersFired() == 1);

    // Read back SPU -> IOP (mode 1), then the module's status poll retires the completion inline.
    Call(libsd, 9, ctx, 0, 1, kDst, kSpuAddr);
    CHECK(ctx.gpr[2] == kBytes);
    CHECK(std::memcmp(mem.Pointer(kDst, kBytes), mem.Pointer(kSrc, kBytes), kBytes) == 0);
    CHECK(libsd.CompletionPending(0));
    Call(libsd, 11, ctx, 0, 0);   // SdVoiceTransStatus(chan 0, no wait)
    CHECK(ctx.gpr[2] == 1);
    CHECK(fired.size() == 2 && !libsd.CompletionPending(0));
    kernel.Dispatch();            // the stale event fires nothing
    CHECK(fired.size() == 2);
    // A second transfer on the same core delivers the older pending completion first.
    Call(libsd, 9, ctx, 0, 0, kSrc, kSpuAddr + 0x1000);
    Call(libsd, 9, ctx, 0, 0, kSrc, kSpuAddr + 0x2000);
    CHECK(fired.size() == 3);
    kernel.Dispatch();
    CHECK(fired.size() == 4);
    // Core 1 has no handler: nothing fires, the transfer still completes.
    Call(libsd, 9, ctx, 1, 0, kSrc, kSpuAddr + 0x3000);
    kernel.Dispatch();
    CHECK(fired.size() == 4 && ctx.gpr[2] == kBytes);
    // STOP moves nothing; the statuses of the no-model rows.
    Call(libsd, 9, ctx, 0, 2, kSrc, kSpuAddr);
    CHECK(ctx.gpr[2] == 0);
    kernel.Dispatch();
    Call(libsd, 10, ctx, 0); CHECK(ctx.gpr[2] == 0);
    Call(libsd, 12, ctx, 0); CHECK(ctx.gpr[2] == 0);
    Call(libsd, 13, ctx, 0); CHECK(ctx.gpr[2] == 0);
    Call(libsd, 14, ctx, 0); CHECK(ctx.gpr[2] == 0);

    // Param/switch/addr/core-attr rows forwarded to the core.
    Call(libsd, 1, ctx, 0x0206, 0x1000);   // PITCH voice 3 core 0
    Call(libsd, 2, ctx, 0x0206);
    CHECK(ctx.gpr[2] == 0x1000);
    Call(libsd, 5, ctx, 0x2006, 0x6000);   // SSA voice 3
    Call(libsd, 6, ctx, 0x2206);           // NAX voice 3 (not keyed: the shadow is 0 until KON)
    CHECK(ctx.gpr[2] == 0 || ctx.gpr[2] == 0x6000);
    Call(libsd, 3, ctx, 0x1500, 1u << 3);  // KON voice 3
    Call(libsd, 6, ctx, 0x2206);
    CHECK(ctx.gpr[2] == 0x6000);           // eager NAX = SSA
    Call(libsd, 4, ctx, 0x1700);           // ENDX latch core 0
    CHECK(ctx.gpr[2] == 0);
    Call(libsd, 7, ctx, 0x0006, 0x1234);   // SD_CORE_MUTE (attr 3) core 0
    CHECK(core.CoreAttr(0, 3) == 0x1234);

    // SdProcBatch: the BUG-003 vector -- restore three paused voices' pitch through
    // SETPARAM records, then GETPARAM records read them back into rets.
    const uint32_t batch = 0x40000, rets = 0x41000;
    const uint16_t voices[3] = {0x0200 | (0 << 1), 0x0200 | (1 << 1), 0x0200 | (2 << 1)};
    const uint32_t pitches[3] = {0x0800, 0x1000, 0x1800};
    for (int i = 0; i < 3; i++) {
        mem.Write<uint16_t>(batch + i * 8 + 0, 0x1);
        mem.Write<uint16_t>(batch + i * 8 + 2, voices[i]);
        mem.Write<uint32_t>(batch + i * 8 + 4, pitches[i]);
    }
    for (int i = 0; i < 3; i++) {
        mem.Write<uint16_t>(batch + (3 + i) * 8 + 0, 0x10);
        mem.Write<uint16_t>(batch + (3 + i) * 8 + 2, voices[i]);
        mem.Write<uint32_t>(batch + (3 + i) * 8 + 4, 0);
    }
    mem.Write<uint16_t>(batch + 6 * 8 + 0, 0x12);          // GETADDR NAX voice 3
    mem.Write<uint16_t>(batch + 6 * 8 + 2, 0x2206);
    mem.Write<uint16_t>(batch + 7 * 8 + 0, 0x7F);          // unknown: skipped, rets 0
    for (int i = 0; i < 8; i++) mem.Write<uint32_t>(rets + i * 4, 0xFFFFFFFF);
    Call(libsd, 8, ctx, batch, rets, 8);
    CHECK(ctx.gpr[2] == 0);
    for (int i = 0; i < 3; i++) CHECK(core.GetParam(voices[i]) == pitches[i]);
    for (int i = 0; i < 3; i++) CHECK(mem.Read<uint32_t>(rets + i * 4) == 0);            // SET rows store 0
    for (int i = 0; i < 3; i++) CHECK(mem.Read<uint32_t>(rets + (3 + i) * 4) == pitches[i]);
    CHECK(mem.Read<uint32_t>(rets + 6 * 4) == 0x6000);
    CHECK(mem.Read<uint32_t>(rets + 7 * 4) == 0);
    CHECK(libsd.BatchRecords() == 8);
    Call(libsd, 8, ctx, batch, 0, 3);          // null rets: applied, nothing stored
    CHECK(libsd.BatchRecords() == 11);
    Call(libsd, 8, ctx, 0, rets, 3);           // null batch: nothing
    Call(libsd, 8, ctx, batch, rets, 513);     // oversized: nothing
    CHECK(libsd.BatchRecords() == 11);
    CHECK(libsd.Calls(8) == 4 && libsd.Calls(9) == 6);
    CHECK(libsd.CallsCensus().find("9:6") != std::string::npos);
    libsd.Call(16, ctx);
    CHECK(ctx.gpr[2] == 0);
    libsd.ResetPendingSequence();
    CHECK(libsd.PendingSequence(0) == 0 && !libsd.CompletionPending(0));
    std::printf("libsd: done (%d failures so far)\n", g_failures);
}

static void TestLibsdr() {
    SpuCore core;
    uint64_t frames = 0;
    SdBlockTrans block(8, [&frames] { return frames; });
    CHECK(block.BlockFrames() == 384);
    std::vector<uint32_t> ee(0x1000, 0);
    SdrRemote remote(core, block, [&ee](uint32_t a) { return ee[(a / 4) % ee.size()]; },
                     [&ee](uint32_t a, uint32_t v) { ee[(a / 4) % ee.size()] = v; });
    CHECK(SdrRemote::kServerId == 0x80000701u);
    // The EE row (Dispatch) keeps the reference scope: rSdSetParam lands on the
    // shared core; the status queries and rSdInit answer 0 without touching it.
    core.SetSwitch(0x1500, 1u);                                            // a module voice is playing
    CHECK(remote.Dispatch(SdrRemote::kInit, 0, 0, 0, 0) == 0);
    CHECK(core.Ring().Size() == 1);                                        // no SilenceAll was queued
    CHECK(remote.Dispatch(SdrRemote::kSetParam, 0x0980, 0x3FFF, 0, 0) == 0);   // MVOLL core 0
    CHECK(core.GetParam(0x0980) == 0x3FFF);
    CHECK(remote.Dispatch(SdrRemote::kGetParam, 0x0980, 0, 0, 0) == 0);        // not forwarded: 0
    CHECK(remote.Dispatch(SdrRemote::kSetCoreAttr, 0x0005, 7, 0, 0) == 0);
    CHECK(core.CoreAttr(1, 2) == 0);                                            // not forwarded
    CHECK(remote.Dispatch(SdrRemote::kVoiceTrans, 0, 0, 0, 0) == 0);
    // The full model behind the SIF face (Invoke).
    CHECK(remote.Invoke(SdrRemote::kGetParam, 0x0980, 0, 0, 0) == 0x3FFF);
    CHECK(remote.Invoke(SdrRemote::kSetCoreAttr, 0x0005, 7, 0, 0) == 0);   // SD_CORE_IRQ_ENABLE core 1
    CHECK(remote.Invoke(SdrRemote::kGetCoreAttr, 0x0005, 0, 0, 0) == 7);
    CHECK(remote.Invoke(SdrRemote::kInit, 0, 0, 0, 0) == 0);
    CHECK(core.Ring().Size() == 2);                                        // SilenceAll queued by the face
    core.Ring().Clear();
    // The virtual block: armed, the address ramps with mixer frames, completes at 384, flips.
    CHECK(remote.Dispatch(SdrRemote::kBlockTrans, 0, 0x13, 0x100000, 0x6000) == 0);
    uint32_t st = remote.Dispatch(SdrRemote::kBlockTransStatus, 0, 0, 0, 0);
    CHECK((st >> 24) == 0 && (st & 0xFFFFFF) == 1);
    frames = 192;
    st = remote.Dispatch(SdrRemote::kBlockTransStatus, 0, 0, 0, 0);
    CHECK((st & 0xFFFFFF) == 0x2000);
    frames = 384;
    st = remote.Dispatch(SdrRemote::kBlockTransStatus, 0, 0, 0, 0);
    CHECK(st == (1u << 24));               // complete: addr 0, buffer flipped
    CHECK(remote.Dispatch(SdrRemote::kBlockTransStatus, 0, 0, 0, 0) == (1u << 24));
    CHECK(remote.Dispatch(SdrRemote::kBlockTrans, 0, 0x13, 0x100000, 0x6000) == 0);
    CHECK(remote.Dispatch(SdrRemote::kBlockTrans, 0, SdBlockTrans::kStop, 0, 0) == 0);
    CHECK(remote.Dispatch(SdrRemote::kBlockTransStatus, 0, 0, 0, 0) == (1u << 24));
    // Savestate shape: elapsed frames survive an Export/Import re-base.
    frames = 1000;
    block.Arm(1, 0, 0);
    frames = 1100;
    bool active; uint32_t bi; uint64_t el;
    block.Export(1, active, bi, el);
    CHECK(active && el == 100);
    frames = 5000;
    block.Import(1, active, bi, el);
    CHECK((block.Status(1) & 0xFFFFFF) == (0x4000u * 100u) / 384u);
    // The RPC face: func 1 SetParam {entry, value}, func 2 GetParam -> reply word.
    RpcRouter::CallRequest req;
    req.server_id = SdrRemote::kServerId; req.func = 1; req.send = 0x100; req.send_size = 8; req.recv = 0x200; req.recv_size = 4;
    ee[0x100 / 4] = 0x0A80; ee[0x104 / 4] = 0x2000;   // MVOLR core 0
    remote.Call(req);
    CHECK(core.GetParam(0x0A80) == 0x2000);
    req.func = 2; ee[0x100 / 4] = 0x0A80;
    remote.Call(req);
    CHECK(ee[0x200 / 4] == 0x2000);
    req.func = 9; remote.Call(req); CHECK(ee[0x200 / 4] == 0x1000);
    req.func = 0xA; remote.Call(req); CHECK(ee[0x200 / 4] == 60);
    CHECK(remote.Calls() > 0);
    // The manifest rows.
    SdkBindingManifest& manifest = SdkBindingManifest::Instance();
    manifest.ResetForTest();
    CHECK(LibsdrBindings::Bind(manifest, &remote) == 2);
    size_t n = 0;
    const char* const* names = LibsdrBindings::RowNames(&n);
    CHECK(n == 2 && std::strcmp(names[0], "sceSdRemoteInit") == 0 && std::strcmp(names[1], "sceSdRemote") == 0);
    CpuContext ctx{};
    LibsdrBindings::RemoteInit(ctx);
    CHECK(ctx.cpuRegs.GPR.r[2].UD[0] == 0 && LibsdrBindings::InitCalls() == 1);
    ctx.cpuRegs.GPR.r[4].UL[0] = 1; ctx.cpuRegs.GPR.r[5].UL[0] = SdrRemote::kSetParam;
    ctx.cpuRegs.GPR.r[6].UL[0] = 0x0F80; ctx.cpuRegs.GPR.r[7].UL[0] = 0x1234;   // BVOLL core 0
    LibsdrBindings::Remote(ctx);
    CHECK(core.GetParam(0x0F80) == 0x1234);
    ctx.cpuRegs.GPR.r[5].UL[0] = SdrRemote::kGetParam; ctx.cpuRegs.GPR.r[6].UL[0] = 0x0F80;
    LibsdrBindings::Remote(ctx);
    CHECK(ctx.cpuRegs.GPR.r[2].UD[0] == 0);   // the EE row does not forward rSdGetParam (reference scope)
    CHECK(LibsdrBindings::RemoteCalls() == 2);
    CHECK(LibsdrBindings::CallsCensus().find("0x8010:1") != std::string::npos);
    std::printf("libsdr: done (%d failures so far)\n", g_failures);
}

int main() {
    TestLibsd();
    TestLibsdr();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all vectors passed\n");
    return 0;
}
