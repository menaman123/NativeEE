// The kernel's idle (EeYieldPoint::IdleWhileBlocked over EeRunToken's idle grant):
//   Two guest threads, both blocked on semaphores that only an interrupt handler
//   signals -- the thbase idle state (nothing READY, nobody waiting on the vblank).
//   On the console emulated time keeps passing: the vblank / GS interrupts land
//   and their handlers ready a thread. Here the thread that blocked LAST runs the
//   yield sequence one field at a time under the idle grant: the third field
//   raises INTC cause GS, the registered handler does iSignalSema(main's sema)
//   ON the idle runner, the runner's own slot is READY, the grant is refused and
//   the WaitSema returns. Without the idle path both threads sleep for ever.
//   Checks: exactly 3 idle fields in 1 idle call; the ISR ran on the idle runner;
//   the readied waiter returned KE_OK; the other thread stayed WAITING until its
//   own signal; a READY thread refuses the grant (TryIdleAcquire false) and
//   PreemptPoint is a no-op under the grant; an unbound yield point idles nothing.
#include <cstdio>
#include <memory>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { ++g_pass; std::printf("  PASS: %s\n", name); }         \
        else { ++g_fail; std::printf("  FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

namespace {
constexpr uint32_t kFlagAddr = 0x00100000u;   // the other thread's "I ran" flag, in guest RAM
constexpr uint32_t kIsrPc = 0x00200000u;      // the registered GS handler's "address"
constexpr int kMainTid = 1, kMainPrio = 5, kOtherPrio = 3;

ThreadManager& TM() { return ThreadManager::Instance(); }
EeRunToken& TOK() { return EeRunToken::Instance(); }
Intc& INTC() { return Intc::Instance(); }
EeYieldPoint& YP() { return EeYieldPoint::Instance(); }

void BindMemory() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);
}

// The rig: fresh singletons, the INTC on a router, one GS handler (pc kIsrPc,
// guest-call model `isr`), the yield point bound with a field advance `field`,
// two semaphores (count 0) -- main's and the other thread's.
struct TestEeIdleWait {
    MmioSink sink;
    MmioRouter router{sink};
    std::function<void(CpuContext&)> isr;
    std::function<void()> field;
    int sid_main = -1, sid_other = -1;
    uint64_t fields = 0;

    TestEeIdleWait() {
        TM().ResetForTest();
        TOK().ResetForTest();
        INTC().ResetForTest();
        SyscallTable::Instance().ResetForTest();
        Alarms::Instance().ResetForTest();
        YP().ResetForTest();
        INTC().Register(router);
        TM().InstallEventApplier();
        INTC().SetGuestCall([this](uint32_t addr, CpuContext& ctx) {
            if (addr == kIsrPc && isr) isr(ctx);
            return true;
        });
        // AddIntcHandler(kGs, kIsrPc, next 0, arg) + _EnableIntc(kGs) through the real syscalls.
        CpuContext c{};
        c.cpuRegs.GPR.r[4].UD[0] = Intc::kGs;
        c.cpuRegs.GPR.r[5].UD[0] = kIsrPc;
        c.cpuRegs.GPR.r[6].UD[0] = 0;
        c.cpuRegs.GPR.r[7].UD[0] = 0x55;
        c.cpuRegs.GPR.r[29].UD[0] = 0x001ff000;
        SyscallTable::Instance().Dispatch(16, c);
        CpuContext e{};
        e.cpuRegs.GPR.r[4].UD[0] = Intc::kGs;
        SyscallTable::Instance().Dispatch(20, e);
        memory::write<uint32_t>(kFlagAddr, 0);
        sid_main = Semaphores::Instance().Create({0, 1, 0, 0});
        sid_other = Semaphores::Instance().Create({0, 1, 0, 0});
        EeYieldPoint::Deps d;
        d.advance_field = [this] { ++fields; if (field) field(); };
        d.intc = &INTC();
        d.queue = &EeEventQueue::Instance();
        YP().Bind(std::move(d));
    }
    ~TestEeIdleWait() {
        Semaphores::Instance().Delete(sid_main);
        Semaphores::Instance().Delete(sid_other);
        TM().SetGuestBody(nullptr);
        YP().ResetForTest();
        TM().ResetForTest();
        TOK().ResetForTest();
    }
    // The other guest thread: WaitSema(sid_other); flag = 1; return (DORMANT).
    int CreateOther(int prio) {
        TM().SetGuestBody([this](int, CpuContext&) {
            Semaphores::Instance().Wait(sid_other);
            memory::write<uint32_t>(kFlagAddr, 1);
        });
        ThreadParam p{};
        p.entry = 0x00300000u;
        p.priority = prio;
        return TM().CreateThread(p);
    }
    void IsrSignal(int sid) {   // the ISR's iSignalSema (67)
        CpuContext c{};
        c.cpuRegs.GPR.r[4].UD[0] = static_cast<uint32_t>(sid);
        SyscallTable::Instance().Dispatch(67, c);
    }
};
}  // namespace

// --- 1: both threads blocked; the idle runner's third field readies main through the ISR ---
static void TestIdleReadiesWaiter() {
    std::printf("[1] two threads in WaitSema; idle field 3 raises GS; the ISR iSignalSema readies the runner\n");
    TestEeIdleWait rig;
    rig.field = [&rig] { if (rig.fields == 3) INTC().RaiseInPlace(Intc::kGs); };
    int isr_on_tid = -99;
    bool isr_saw_idle = false;
    uint64_t isr_runs = 0;
    rig.isr = [&](CpuContext&) {
        ++isr_runs;
        isr_on_tid = TOK().CurrentTid();
        isr_saw_idle = TOK().CurrentThreadIdling();
        rig.IsrSignal(rig.sid_main);
    };
    CpuContext ctx{};
    TM().SetStackBytes(static_cast<size_t>(8) * 1024 * 1024);
    std::shared_ptr<ThreadInfo> main_info = TM().AdoptCurrentThread(kMainTid, kMainPrio);
    main_info->ctx = &ctx;   // the idle runner's context (the boot path binds the main thread's)
    TOK().GuestEnter(kMainTid, kMainPrio);
    const int other = rig.CreateOther(kOtherPrio);
    CHECK(TM().StartThread(other, 0, 0x1ff000, 0) == KE_OK, "1a: StartThread(other prio 3) KE_OK");
    ThreadStatus st{};
    CHECK(TM().ReferThreadStatus(other, st) == other && st.status == THS_WAIT, "1b: the other thread waits on its sema (ran at StartThread)");
    // Main blocks too: the EE is idle. Without the idle path this call never returns.
    const int ret = Semaphores::Instance().Wait(rig.sid_main);
    const auto& c = YP().Stats();
    CHECK(ret == KE_OK, "1c: WaitSema(main) returned KE_OK -- the ISR's iSignalSema reached it");
    CHECK(rig.fields == 3 && c.idle_fields == 3 && c.idle_calls == 1,
          "1d: exactly 3 idle fields in one idle call (field 3 raised the cause; the grant was refused once READY)");
    CHECK(isr_runs == 1 && isr_on_tid == kMainTid && isr_saw_idle && c.isrs == 1,
          "1e: the GS handler ran once, on the blocked main thread under the idle grant");
    CHECK(c.yields == 0 && c.trips == 0, "1f: no back-edge trip was involved");
    CHECK(TOK().CurrentThreadHoldsToken() && !TOK().CurrentThreadIdling() && TOK().IdleGrants() == 3,
          "1g: main holds the token as a scheduled thread again; 3 grants were taken");
    CHECK(TM().ReferThreadStatus(other, st) == other && st.status == THS_WAIT && memory::read<uint32_t>(kFlagAddr) == 0,
          "1h: the other thread is still waiting (its sema was never signalled)");
    // Release the other thread the ordinary way (SignalSema's dispatch edge runs it: prio 3 < 5).
    CHECK(Semaphores::Instance().Signal(rig.sid_other) == KE_OK && memory::read<uint32_t>(kFlagAddr) == 1,
          "1i: SignalSema(other) ran it to completion -- the flag flipped");
    std::printf("  census: %s\n", c.Format().c_str());
    TOK().GuestExit(kMainTid);
    TM().JoinAll();
}

// --- 2: the grant's refusals; PreemptPoint under the grant; unbound idles nothing ---
static void TestGrantGuards() {
    std::printf("[2] grant refused with a READY thread / a holder; PreemptPoint no-op under the grant; unbound = nothing\n");
    TestEeIdleWait rig;
    CpuContext ctx{};
    std::shared_ptr<ThreadInfo> main_info = TM().AdoptCurrentThread(kMainTid, kMainPrio);
    main_info->ctx = &ctx;
    TOK().GuestEnter(kMainTid, kMainPrio);
    CHECK(!TOK().TryIdleAcquire(kMainTid), "2a: a thread that holds the token gets no idle grant");
    TOK().BlockBegin(kMainTid);   // leave the ready set (a blocking wait's first half)
    TOK().MakeReady(77, 2);       // some other thread is READY
    CHECK(!TOK().TryIdleAcquire(kMainTid), "2b: with a READY thread the EE is not idle: grant refused");
    TOK().Drop(77);
    CHECK(TOK().TryIdleAcquire(kMainTid) && TOK().CurrentThreadIdling() && TOK().CurrentThreadHoldsToken(),
          "2c: nothing READY, nobody on the vblank: the grant is taken; the runner holds the token");
    TOK().MakeReady(77, 2);       // an ISR readied a HIGHER-priority thread during the field
    TOK().PreemptPoint();
    CHECK(TOK().CurrentThreadIdling() && TOK().CurrentThreadHoldsToken(),
          "2d: PreemptPoint under the grant is a no-op (the runner hands over at IdleRelease)");
    TOK().IdleRelease(kMainTid);
    CHECK(!TOK().CurrentThreadIdling() && !TOK().CurrentThreadHoldsToken() && TOK().OwnerForTest() == 77,
          "2e: IdleRelease elected the readied thread");
    CHECK(!TOK().TryIdleAcquire(kMainTid), "2f: with the readied thread elected, no further grant");
    TOK().Drop(77);
    // Unbound yield point: a blocked thread idles nothing (the pre-E220 behaviour, counted as nothing).
    YP().Unbind();
    CHECK(YP().IdleWhileBlocked(ctx) == 0 && YP().Stats().idle_calls == 0, "2g: unbound: IdleWhileBlocked runs no field");
    TOK().BlockEnd(kMainTid);
    CHECK(TOK().CurrentThreadHoldsToken(), "2h: BlockEnd re-acquired the token for main");
    TOK().GuestExit(kMainTid);
    TM().JoinAll();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("EeIdleWait\n");
    BindMemory();
    TestIdleReadiesWaiter();
    TestGrantGuards();
    if (g_fail) std::printf("FAILURES: %d (%d passed)\n", g_fail, g_pass);
    else std::printf("ALL PASS (%d)\n", g_pass);
    return g_fail ? 1 : 0;
}
