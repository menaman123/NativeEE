// tests/iop/test_kernel_facing_modules.cpp -- IopKernelFacingModules: the thbase /
// thsemap / sifcmd / timrman / ioman routing cases ported from OLD iop_stubs.cpp's
// kernel arm: CreateThread's param layout and tid sequence, StartThread's entry
// context ($a0/$sp/$gp), Sleep/Wakeup through the kernel, USec2SysClock x37,
// CreateSema's param layout and round-robin sid, Wait/Signal handoff, BindRpc's
// server word, RegisterRpc's id/func words, the heartbeat period, ioman with no
// reader. All under the FRAME policy: deterministic, no host thread.
#include <cstdio>
#include <functional>

#include "engine/iop/hle/ioman/ioman_module.h"
#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/hle/timrman/timrman_module.h"
#include "engine/iop/iop_module_table.h"
#include "engine/iop/iop_error.h"
#include "engine/platform/host/platform.h"

using namespace ps2r::iop;
using namespace ps2r::iop::hle;

static int g_fail = 0;
#define CHECK(c, msg)                                                          \
    do {                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; } \
    } while (0)

static void TestThbase() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopMemory mem;
    uint32_t seen_a0 = 0, seen_sp = 0, seen_gp = 0;
    int seen_tid = 0, wakes = 0;
    ThbaseModule::ThreadLayout layout{0x100000, 0x4000, 0x13328};
    ThbaseModule th(m, mem, [&](uint32_t addr) -> ThbaseModule::GuestFunction {
        if (addr != 0x8D28) return {};
        return [&](IopContext& c) {
            seen_a0 = c.gpr[4]; seen_sp = c.gpr[29]; seen_gp = c.gpr[28];
            IopContext k;
            th.GetThreadId(k);
            seen_tid = static_cast<int>(k.gpr[2]);
            for (;;) { th.SleepThread(k); ++wakes; }
        };
    }, layout);

    IopContext ctx;
    mem.Write<uint32_t>(0x3000 + 0x08, 0x8D28);   // entry
    mem.Write<uint32_t>(0x3000 + 0x0C, 0x1000);   // stack size
    mem.Write<uint32_t>(0x3000 + 0x10, 30);       // priority
    ctx.gpr[4] = 0x3000;
    th.CreateThread(ctx);
    CHECK(ctx.gpr[2] == 1 && th.NextTid() == 2, "CreateThread: first tid is 1, sequence advances");
    CHECK(th.ThreadEntry(1) == 0x8D28 && m.Kernel().ThreadPriority(1) == 30, "param layout +0x08 entry / +0x10 priority reach the kernel");
    ctx.gpr[4] = 1;
    ctx.gpr[5] = 0xCAFE;
    th.StartThread(ctx);
    m.OnModuleLoaded(1);
    CHECK(seen_a0 == 0xCAFE && seen_sp == 0x100000 - 1 * 0x4000 - 0x10 && seen_gp == 0x13328,
          "StartThread context: $a0 = arg, $sp = stack_top - tid*stride - 0x10, $gp");
    CHECK(seen_tid == 1, "GetThreadId inside the body = the kernel's current tid");
    CHECK(m.Kernel().ThreadWaitKind(1) == IopKernel::WaitKind::Sleep && wakes == 0, "SleepThread parked the body");
    ctx.gpr[4] = 1;
    th.WakeupThread(ctx);   // host context: injection + pump
    m.PumpAtBoundary();
    CHECK(wakes == 1, "WakeupThread from host context releases the Sleep through the injection queue");
    ctx.gpr[4] = 1000;
    ctx.gpr[5] = 0x5000;
    th.USec2SysClock(ctx);
    CHECK(mem.Read<uint32_t>(0x5000) == 37000 && mem.Read<uint32_t>(0x5004) == 0, "USec2SysClock: usec * 37, 64-bit");
    ctx.gpr[4] = 1; ctx.gpr[5] = 99;
    th.ChangeThreadPriority(ctx);
    CHECK(ctx.gpr[2] == 0 && m.Kernel().ThreadPriority(1) == 99, "ChangeThreadPriority updates the sleeping thread");
    ctx.gpr[4] = 40;
    th.WakeupThread(ctx);
    CHECK(static_cast<int32_t>(ctx.gpr[2]) == IopError::UnknownThid, "WakeupThread on an out-of-range tid -> unknown thread");
    m.Shutdown();
}

static void TestThsemap() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopMemory mem;
    ThsemapModule sm(m, mem);
    IopContext ctx;
    mem.Write<uint32_t>(0x3000 + 0x08, 0);   // initial count
    mem.Write<uint32_t>(0x3000 + 0x0C, 1);   // max count
    ctx.gpr[4] = 0x3000;
    sm.CreateSema(ctx);
    const uint32_t sid = ctx.gpr[2];
    CHECK(sid == 0 && sm.NextSid() == 1 && m.Kernel().SemaActive(0), "CreateSema: sid 0 first, registered in the kernel");
    sm.CreateSema(ctx);
    CHECK(ctx.gpr[2] == 1, "round-robin: the next sid is 1");
    ctx.gpr[4] = 1;
    sm.DeleteSema(ctx);
    CHECK(!m.Kernel().SemaActive(1), "DeleteSema from host context reaches the kernel under the dispatch lock");

    int passes = 0;
    m.Kernel().RegisterThread(1, 30);
    m.Kernel().StartThread(1, [&] {
        IopContext k;
        k.gpr[4] = sid;
        for (;;) { sm.WaitSema(k); ++passes; }
    });
    m.OnModuleLoaded(1);
    CHECK(passes == 0 && m.Kernel().ThreadWaitKind(1) == IopKernel::WaitKind::Sema, "WaitSema on an empty sema parks the thread");
    ctx.gpr[4] = sid;
    sm.iSignalSema(ctx);   // host context: queued
    m.PumpAtBoundary();
    CHECK(passes == 1, "iSignalSema from host context releases the waiter through the injection queue");
    sm.SignalSema(ctx);
    m.PumpAtBoundary();
    CHECK(passes == 2, "SignalSema again: the second pass");
    m.Shutdown();
}

static void TestSifcmd() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopMemory mem;
    SifcmdModule sc(m, mem);
    IopContext ctx;
    ctx.gpr[4] = 0x6000; ctx.gpr[5] = 0x12345; ctx.gpr[6] = 0;
    sc.sceSifBindRpc(ctx);
    CHECK(ctx.gpr[2] == 0 && mem.Read<uint32_t>(0x6000 + 0x24) == (0xDEAD0000u | 0x2345u), "BindRpc writes the fake server word at client+0x24");
    ctx.gpr[4] = 0x7000; ctx.gpr[5] = 0x12345; ctx.gpr[6] = 0x8D28; ctx.gpr[7] = 0x9000;
    sc.sceSifRegisterRpc(ctx);
    CHECK(mem.Read<uint32_t>(0x7000) == 0x12345 && mem.Read<uint32_t>(0x7004) == 0x8D28, "RegisterRpc stores id/func at server_data+0/+4");
    ctx.gpr[4] = 0xA000; ctx.gpr[5] = 1;
    sc.sceSifSetRpcQueue(ctx);
    CHECK(sc.RpcQueueTid() == 1, "SetRpcQueue records the queue's thread");
    ctx.gpr[2] = 0x55;
    sc.sceSifCallRpc(ctx);
    CHECK(ctx.gpr[2] == 0 && !sc.HasCdReadRpc(), "CallRpc with no protocol body replies 0");
    int called = 0;
    sc.SetCdReadRpc([&](IopContext& c) { ++called; c.gpr[2] = 0; });
    sc.hle_stream_cd_read_rpc(ctx);
    sc.sceSifCallRpc(ctx);
    CHECK(called == 2, "the thunk and CallRpc share the installed CD-read body");
    sc.sceSifRpcLoop(ctx);   // host context: never blocks
    CHECK(ctx.gpr[2] == 0, "RpcLoop from host context returns");
    m.Shutdown();
}

static void TestTimrman() {
    IopModuleTable m(IopClock::Policy::Frame);
    int beats = 0;
    TimrmanModule tm(m, [&](uint32_t addr) -> TimrmanModule::GuestFunction {
        if (addr != 0x136C) return {};
        return [&](IopContext& c) { ++beats; c.gpr[2] = beats < 3 ? 1 : 0; };
    }, TimrmanModule::TimerLayout{0x1FFC00, 0x13328});
    IopContext ctx;
    tm.AllocHardTimer(ctx);
    CHECK(ctx.gpr[2] == 1, "AllocHardTimer -> id 1");
    ctx.gpr[4] = 1; ctx.gpr[5] = 614400; ctx.gpr[6] = 0x136C; ctx.gpr[7] = 0x14530;
    tm.SetTimerHandler(ctx);
    CHECK(TimrmanModule::PeriodUsec(614400) == 16666, "compare 614400 ticks at 36.864 MHz = the 60 Hz beat (16666 us)");
    CHECK(TimrmanModule::PeriodUsec(1) == 1000 && TimrmanModule::PeriodUsec(0xFFFFFFFFu) == 100000, "period clamps [1 ms, 100 ms]");
    tm.SetupHardTimer(ctx);
    CHECK(ctx.gpr[2] == 0, "SetupHardTimer -> 0");
    ctx.gpr[4] = 1;
    tm.StartHardTimer(ctx);
    CHECK(tm.Running(1) && tm.KernelTimerId(1) >= 0, "StartHardTimer armed a kernel periodic timer");
    m.OnModuleLoaded(1);
    for (int i = 0; i < 12 && beats < 3; ++i) m.PumpAtBoundary();
    CHECK(beats == 3 && !tm.Running(1), "the beat rearms while $v0 != 0 and stops on 0");
    ctx.gpr[4] = 1;
    tm.FreeHardTimer(ctx);
    CHECK(!tm.Running(1) && tm.KernelTimerId(1) == -1, "FreeHardTimer clears the slot");
    m.Shutdown();
}

// DP-20 (c): the first due of a RUNNING hard timer restored from a savestate.
// one_period (today): after the clock's RebaseToFrame + RestoreHardTimer the beat
// is due one period past the restored now -- no Dispatch BEFORE the first boundary
// pump reaches it (the EE's RPC-driven passes of boundary 0 run first); the first
// boundary pump does, through its inclusive ceiling advance (the due lies 60 us
// past the boundary's quantum).
// at_restore: due = now -> the handler fires in the FIRST Dispatch after the restore,
// before any RPC-woken thread of that pass.
static void TestTimrmanRestoredDue(ps2r::IopRestoredTimerDue rule) {
    const bool at_restore = rule == ps2r::IopRestoredTimerDue::AtRestore;
    const char* tag = at_restore ? "at_restore" : "one_period";
    IopModuleTable m(IopClock::Policy::Frame);
    int beats = 0;
    TimrmanModule tm(m, [&](uint32_t addr) -> TimrmanModule::GuestFunction {
        if (addr != 0x136C) return {};
        return [&](IopContext& c) { ++beats; c.gpr[2] = 1; };
    }, TimrmanModule::TimerLayout{0x1FFC00, 0x13328});
    tm.SetRestoredTimerDue(rule);
    m.OnModuleLoaded(0);
    const uint64_t kAnchorFrame = 40304;
    m.Kernel().Clock().RebaseToFrame(kAnchorFrame);
    const uint64_t now = m.Kernel().Clock().NowUs();
    // STREAM.IRX's compare 616642 -> 16727 us: longer than the 16667 us boundary quantum.
    CHECK(tm.RestoreHardTimer(1, 616642, 0x136C, 0x14530), "restore arms the recorded running timer");
    CHECK(tm.Running(1) && beats == 0, "armed, not yet beaten (the beat needs a Dispatch)");
    m.Kernel().Dispatch();   // the first pass after the restore (what the EE's RPC pump or boundary 0 runs)
    std::printf("  [%s] after the first Dispatch: beats=%d (now=%llu, period=%llu)\n", tag, beats,
                (unsigned long long)now, (unsigned long long)TimrmanModule::PeriodUsec(616642));
    if (at_restore) {
        CHECK(beats == 1, "at_restore: the handler fires in the first Dispatch after the restore");
    } else {
        CHECK(beats == 0, "one_period: the first Dispatch does not reach the due");
        m.Kernel().Dispatch();
        CHECK(beats == 0, "one_period: no pre-boundary Dispatch reaches the due (the RPC-woken pass precedes the beat)");
        m.PumpAtBoundary();   // boundary 0's pump: quantum 16667 us, ceiling advance reaches the due at +16727
        CHECK(beats == 1, "one_period: the first boundary pump beats (inclusive ceiling)");
    }
    m.PumpAtBoundary();
    m.PumpAtBoundary();
    std::printf("  [%s] after two more boundary pumps: beats=%d\n", tag, beats);
    CHECK(beats >= 2, "the restored timer keeps its period after the first beat");
    m.Shutdown();
}

static void TestIomanNoReader() {
    IopMemory mem;
    IomanModule io(mem, nullptr);
    IopContext ctx;
    ctx.gpr[4] = 0x1000;
    io.iop_open(ctx);
    CHECK(ctx.gpr[2] == 0xFFFFFFFFu && !io.HasReader(), "ioman with no CdvdReader: open -> -1 (bound in 4D)");
    io.iop_read(ctx);
    CHECK(ctx.gpr[2] == 0xFFFFFFFFu, "read -> -1");
}

int main() {
    ps2r::platform::host::RegisterSelf();
    TestThbase();
    TestThsemap();
    TestSifcmd();
    TestTimrman();
    TestTimrmanRestoredDue(ps2r::IopRestoredTimerDue::OnePeriod);
    TestTimrmanRestoredDue(ps2r::IopRestoredTimerDue::AtRestore);
    TestIomanNoReader();
    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
