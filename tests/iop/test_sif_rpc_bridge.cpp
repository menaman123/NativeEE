// tests/iop/test_sif_rpc_bridge.cpp -- SifRpcBridge (ctest SifRpcBridge): an RPC
// is a memcpy plus a wakeup. Pinned: EE->IOP RunOnRpcThread copies the send
// buffer into the module's server buffer ON the module's own RPC-dispatch
// thread (the coroutine parked in RpcServiceLoop) and completes its ticket under
// FRAME (the EE drives Dispatch); the IOP->EE marshal: a worker parked in
// Wait::Rpc is resumed with the reply visible at the slot-6 pump after the EE's
// copy-back + NotifyEeReply; a reply after the deadline is DROPPED (no banked
// token releases the worker's next park); a kernel-context caller runs inline;
// the REALTIME path completes a command through the IopHostThread.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "engine/iop/iop_module_table.h"
#include "engine/platform/host/platform.h"

using ps2r::iop::IopClock;
using ps2r::iop::IopKernel;
using ps2r::iop::IopModuleTable;

static int g_failures = 0;
#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) std::printf("PASS: %s\n", name);               \
        else { std::printf("FAIL: %s (line %d)\n", name, __LINE__); g_failures++; } \
    } while (0)

static void TestMemcpyPlusWakeup() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    uint8_t server_buf[64] = {};
    uint8_t send_buf[64];
    for (int i = 0; i < 64; i++) send_buf[i] = (uint8_t)(0xA0 + i);
    int body_tid = -1;
    uint32_t result = 0;
    k.RegisterThread(1, 30);
    k.StartThread(1, [&] { m.Rpc().RpcServiceLoop(); });
    m.OnModuleLoaded(1);
    CHECK(k.ThreadWaitKind(1) == IopKernel::WaitKind::Sleep, "the RPC thread parks in RpcServiceLoop (SleepThread)");
    const bool ok = m.Rpc().RunOnRpcThread([&] {
        std::memcpy(server_buf, send_buf, sizeof(server_buf));   // the memcpy half
        body_tid = k.CurrentTid();
        result = 0x1234;
    });
    CHECK(ok, "RunOnRpcThread completes under FRAME (the EE drives Dispatch)");
    CHECK(std::memcmp(server_buf, send_buf, 64) == 0, "send buffer copied into the server buffer");
    CHECK(body_tid == 1, "the body ran on the module's own RPC-dispatch thread (tid 1)");
    CHECK(result == 0x1234 && m.Rpc().Completed() == 1 && m.Rpc().Submitted() == 1, "ticket completed");
    CHECK(k.ThreadWaitKind(1) == IopKernel::WaitKind::Sleep, "the RPC thread re-parks after the command");
    // A second command: the banked wakeup path (no lost-command window).
    const bool ok2 = m.Rpc().RunOnRpcThread([&] { result = 0x5678; });
    CHECK(ok2 && result == 0x5678 && m.Rpc().Completed() == 2, "second command completes");
    CHECK(m.Rpc().Stalls() == 0, "no stall recorded");
}

static void TestReplyCopyBackAtSlot() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    uint8_t reply_buf[16] = {};
    uint8_t seen[16] = {};
    int woken = -1;
    k.RegisterThread(2, 20);
    k.StartThread(2, [&] {
        woken = m.Rpc().WaitEeReplyUs(500000) ? 1 : 0;   // the marshal: park with a deadline
        std::memcpy(seen, reply_buf, sizeof(seen));      // the reply is visible when resumed
        for (;;) k.SleepThread();
    });
    m.OnModuleLoaded(0);
    CHECK(k.ThreadWaitKind(2) == IopKernel::WaitKind::Rpc && m.Rpc().EeWaitTid() == 2, "worker parked in Wait::Rpc");
    m.PumpAtBoundary();   // a boundary passes without a reply: still parked (deadline 500 ms)
    CHECK(woken == -1 && k.ThreadWaitKind(2) == IopKernel::WaitKind::Rpc, "still parked after one boundary");
    // Slot 6 (Step::EeStreamRpc): the EE copies the reply back, notifies, pumps.
    for (int i = 0; i < 16; i++) reply_buf[i] = (uint8_t)(0x10 + i);
    m.Rpc().NotifyEeReply();
    m.PumpAfterEeService();
    CHECK(woken == 1, "the parkee resumes with woken = true at the slot-6 pump");
    CHECK(std::memcmp(seen, reply_buf, 16) == 0, "the reply copy-back is visible to the resumed worker");
    CHECK(m.Rpc().EeWaitTid() == 0, "no tid parked after the reply");
}

static void TestDropLateReply() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    int woken = -1;
    int sleeps_released = 0;
    k.RegisterThread(2, 20);
    k.StartThread(2, [&] {
        woken = m.Rpc().WaitEeReplyUs(1000) ? 1 : 0;   // 1 ms deadline
        for (;;) {
            k.SleepThread();
            sleeps_released++;
        }
    });
    m.OnModuleLoaded(0);
    m.PumpAtBoundary();   // the boundary pump advances past the 1 ms deadline
    CHECK(woken == 0, "the worker times out (woken = false) when no reply arrives before the deadline");
    CHECK(k.ThreadWaitKind(2) == IopKernel::WaitKind::Sleep, "the worker went on to its next park (Sleep)");
    m.Rpc().NotifyEeReply();   // late reply: EeWaitTid is 0 -> nothing to wake
    m.PumpAfterEeService();
    CHECK(sleeps_released == 0 && k.ThreadWaitKind(2) == IopKernel::WaitKind::Sleep,
          "a late reply is dropped: no banked token releases the worker's Sleep");
    // The same rule through the kernel directly: iWakeupIfRpcWait on a Sleep parkee is a no-op.
    k.iWakeupIfRpcWait(2);
    m.PumpAfterEeService();
    CHECK(sleeps_released == 0, "iWakeupIfRpcWait never wakes a non-Rpc parkee");
    k.iWakeupThread(2);
    m.PumpAfterEeService();
    CHECK(sleeps_released == 1, "a real WakeupThread does release the Sleep (control)");
}

static void TestInlineOnKernelThread() {
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    bool inline_ok = false;
    k.RegisterThread(1, 30);
    k.StartThread(1, [&] {
        // A kernel-context caller runs the body inline (no queue, no ticket).
        inline_ok = m.Rpc().RunOnRpcThread([&] { inline_ok = k.CurrentTid() == 1; }) && inline_ok;
        for (;;) k.SleepThread();
    });
    m.OnModuleLoaded(1);
    CHECK(inline_ok && m.Rpc().Submitted() == 0, "a kernel-context RunOnRpcThread runs inline");
}

static void TestRealtimeHostThreadCompletes() {
    const auto caller = ps2r::platform::Current().fiber_current().handle;
    // This legacy fixture migrates a booted coroutine to a realtime host.
    // Give its whole lifecycle an owner separate from the FRAME cases:
    // macOS ucontext bookkeeping from that migration must not become the
    // later deterministic fixture's host context. See sif_bridge_fixture.md.
    std::thread owner([] {
        IopModuleTable m(IopClock::Policy::Realtime);
        IopKernel& k = m.Kernel();
        CHECK(k.RegisterThread(1, 30) &&
                  k.StartThread(1, [&] { m.Rpc().RpcServiceLoop(); }),
              "REALTIME fixture creates its RPC thread");
        m.OnModuleLoaded(1);
        CHECK(m.Host().Running(), "REALTIME: the host thread runs after boot");
        int body_tid = -1;
        const bool ok = m.Rpc().RunOnRpcThread([&] { body_tid = k.CurrentTid(); });
        CHECK(ok && body_tid == 1, "REALTIME: the command completes on the RPC thread via the host thread");
        CHECK(m.Host().Pokes() >= 1 && m.Host().Passes() >= 1, "the EE submission poked the host thread");
        m.Shutdown();
        CHECK(!m.Host().Running(), "Shutdown joins the host thread");
    });
    owner.join();
    CHECK(ps2r::platform::Current().fiber_current().handle == caller,
          "REALTIME fixture leaves the FRAME owner's host context unchanged");
}

static void TestRoundTripServiceCharge() {
    // FRAME: a completed round trip advances the IOP's clock by the bound service
    // time after the body ran (the reply's instant); the default (0) charges
    // nothing -- the boundary-mode legs' clock is untouched bit for bit.
    IopModuleTable m(IopClock::Policy::Frame);
    IopKernel& k = m.Kernel();
    CHECK(k.RegisterThread(1, 30) &&
              k.StartThread(1, [&] { m.Rpc().RpcServiceLoop(); }),
          "charge fixture creates its RPC thread");
    m.OnModuleLoaded(1);
    CHECK(k.ThreadWaitKind(1) == IopKernel::WaitKind::Sleep && !k.IsShutdown(),
          "charge fixture boots to the RPC park before measuring time");
    const uint64_t t0 = k.Clock().NowUs();
    CHECK(m.Rpc().ServiceUs() == 0 && m.Rpc().RunOnRpcThread([] {}) && k.Clock().NowUs() == t0 && m.Rpc().ChargedUs() == 0,
          "default: a round trip costs no IOP time");
    m.Rpc().SetServiceUs(160);
    uint64_t at_body = 0;
    CHECK(m.Rpc().RunOnRpcThread([&] { at_body = k.Clock().NowUs(); }) && at_body == t0 && k.Clock().NowUs() == t0 + 160,
          "charged: the body runs at the issue instant, the clock stands at issue + 160 us after the round trip");
    for (int i = 0; i < 9; i++) m.Rpc().RunOnRpcThread([] {});
    CHECK(k.Clock().NowUs() == t0 + 10 * 160 && m.Rpc().ChargedUs() == 1600 && m.Rpc().Completed() == 11,
          "ten round trips: 1,600 us on the clock and in the census; the tickets count the round trips");
    // A module event the charge passes fires at the next dispatch (the EE's poll
    // sees what the IOP did while the EE waited for the replies).
    int fired = 0;
    k.ScheduleEventUs(k.Clock().NowUs() + 300, [&] { fired++; });
    m.Rpc().RunOnRpcThread([] {});
    m.Rpc().RunOnRpcThread([] {});
    CHECK(fired == 0 && m.PumpToInstant() && fired == 1, "a due the charged instant passed fires at PumpToInstant");
    m.Rpc().SetServiceUs(0);
    const uint64_t t1 = k.Clock().NowUs();
    CHECK(m.Rpc().RunOnRpcThread([] {}) && k.Clock().NowUs() == t1, "unbound again: no charge");
}

int main() {
    ps2r::platform::host::RegisterSelf();
    TestMemcpyPlusWakeup();
    TestReplyCopyBackAtSlot();
    TestDropLateReply();
    TestInlineOnKernelThread();
    TestRealtimeHostThreadCompletes();
    TestRoundTripServiceCharge();
    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
