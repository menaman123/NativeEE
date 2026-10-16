// EeYieldPoint: the back-edge yield seam of --yield-backedges generated code.
//   A synthetic guest: thread 1 (priority 5) spins on a flag in RAM with no kernel
//   call -- the loop body is exactly what the recompiler emits at a back-edge
//   (`if (--g_ee_backedge_budget == 0) ps2r_ee_yield_point(ctx);`). A second guest
//   thread (the setter) waits on a semaphore and sets the flag when released. The
//   "hardware" is the trip's field advance: the third field raises INTC cause GS,
//   whose registered handler does iSignalSema(sid) -- the console's ISR.
//   Checks: the spinner trips every `budget` iterations; the ISR runs ON the
//   spinning thread at a trip; the readied higher-priority setter preempts the
//   spinner there (thbase R2) and the flag flips while the spinner is parked;
//   an equal-priority setter does NOT preempt (R1: no time slicing) until the
//   spinner yields through RotateThreadReadyQueue; a trip inside a handler is
//   refused (the Intc in-dispatch guard); unbound, a trip only reloads the budget.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

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
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"

// The seam TU (engine/ee/cpu/seams.cpp, where ps2r_ee_yield_point lives) pulls in the
// park-site dump's shadow-stack accessors, which the generated common TU defines; this
// test links no title, so they are stubbed here (as tests/ee/test_park_site_dump.cpp does).
extern "C" uint32_t ee_get_call_stack_depth() { return 0; }
extern "C" bool ee_get_call_stack_top(uint32_t, uint32_t*, uint32_t*) { return false; }
extern "C" bool ee_call_stack_push(uint32_t, uint32_t) { return false; }
extern "C" void ee_call_stack_pop() {}

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { ++g_pass; std::printf("  PASS: %s\n", name); }         \
        else { ++g_fail; std::printf("  FAIL: %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

namespace {
constexpr uint32_t kFlagAddr = 0x00100000u;   // the spin flag, in guest RAM
constexpr uint32_t kIsrPc = 0x00200000u;      // the registered GS handler's "address"
constexpr uint32_t kBudget = 64;
constexpr int kMainTid = 1, kMainPrio = 5;

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
// whose guest-call model is `isr`), the yield point bound with a field advance
// `field` and the small budget.
struct TestEeYieldPoint {
    MmioSink sink;
    MmioRouter router{sink};
    std::function<void(CpuContext&)> isr;
    std::function<void()> field;
    int sid = -1;
    uint64_t fields = 0;

    TestEeYieldPoint() {
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
        c.cpuRegs.GPR.r[29].UD[0] = 0x001ff000;   // a stack for the 5th argument read
        SyscallTable::Instance().Dispatch(16, c);
        CpuContext e{};
        e.cpuRegs.GPR.r[4].UD[0] = Intc::kGs;
        SyscallTable::Instance().Dispatch(20, e);
        memory::write<uint32_t>(kFlagAddr, 0);
        sid = Semaphores::Instance().Create({0, 1, 0, 0});
        EeYieldPoint::Deps d;
        d.advance_field = [this] { ++fields; if (field) field(); };
        d.intc = &INTC();
        d.queue = &EeEventQueue::Instance();
        d.budget = kBudget;
        YP().Bind(std::move(d));
    }
    ~TestEeYieldPoint() {
        Semaphores::Instance().Delete(sid);
        TM().SetGuestBody(nullptr);
        YP().ResetForTest();
        TM().ResetForTest();
        TOK().ResetForTest();
    }
    // The setter guest thread: WaitSema(sid); flag = 1; return (DORMANT).
    int CreateSetter(int prio) {
        TM().SetGuestBody([this](int, CpuContext&) {
            Semaphores::Instance().Wait(sid);
            memory::write<uint32_t>(kFlagAddr, 1);
        });
        ThreadParam p{};
        p.entry = 0x00300000u;
        p.priority = prio;
        return TM().CreateThread(p);
    }
    // One back-edge of the generated loop.
    static void BackEdge(CpuContext& ctx) {
        if (--g_ee_backedge_budget == 0) ps2r_ee_yield_point(ctx);
    }
};
}  // namespace

// --- 1: the spinner is preempted at a trip by the ISR-readied higher thread ---------
static void TestPreemptedAtTrip() {
    std::printf("[1] spin on a flag; field 3 raises GS; ISR iSignalSema readies a prio-2 setter\n");
    TestEeYieldPoint rig;
    rig.field = [&rig] { if (rig.fields == 3) INTC().RaiseInPlace(Intc::kGs); };
    int isr_on_tid = -99;
    uint64_t isr_runs = 0;
    rig.isr = [&](CpuContext&) {
        ++isr_runs;
        isr_on_tid = TOK().CurrentTid();
        CpuContext c{};   // the ISR's iSignalSema (67)
        c.cpuRegs.GPR.r[4].UD[0] = static_cast<uint32_t>(rig.sid);
        SyscallTable::Instance().Dispatch(67, c);
    };
    CpuContext ctx{};
    TM().SetStackBytes(static_cast<size_t>(8) * 1024 * 1024);
    TM().AdoptCurrentThread(kMainTid, kMainPrio);
    TOK().GuestEnter(kMainTid, kMainPrio);
    const int setter = rig.CreateSetter(2);
    CHECK(TM().StartThread(setter, 0, 0x1ff000, 0) == KE_OK, "1a: StartThread(setter prio 2) KE_OK");
    // Priority 2 < 5: StartThread's dispatch edge ran the setter at once; it is now
    // WAITING on the sema and the token is back here.
    ThreadStatus st{};
    CHECK(TM().ReferThreadStatus(setter, st) == setter && st.status == THS_WAIT, "1b: the setter waits on the sema (ran at StartThread)");
    CHECK(memory::read<uint32_t>(kFlagAddr) == 0, "1c: the flag is still 0");
    // The spin: no kernel call in the loop.
    uint64_t iterations = 0;
    const uint64_t kMaxIterations = kBudget * 64;
    while (memory::read<uint32_t>(kFlagAddr) == 0 && iterations < kMaxIterations) {
        ++iterations;
        TestEeYieldPoint::BackEdge(ctx);
    }
    const auto& c = YP().Stats();
    CHECK(memory::read<uint32_t>(kFlagAddr) == 1, "1d: the flag flipped -- the spin exited");
    CHECK(iterations == kBudget * 3, "1e: exactly 3 trips (3 x budget back-edges) before the flip");
    CHECK(rig.fields == 3 && c.yields == 3, "1f: each trip advanced one field; 3 yields");
    CHECK(isr_runs == 1 && isr_on_tid == kMainTid && c.isrs == 1,
          "1g: the GS handler ran once, on the spinning thread, at the trip");
    CHECK(c.nested == 0 && c.non_holder == 0, "1h: no refused trips");
    CHECK(TM().ReferThreadStatus(setter, st) == setter && st.status == THS_DORMANT, "1i: the setter finished (DORMANT)");
    std::printf("  census: %s\n", c.Format().c_str());
    TOK().GuestExit(kMainTid);
    TM().JoinAll();
}

// --- 2: equal priority never preempts a spinner; RotateThreadReadyQueue is the yield --
static void TestEqualPriorityNoPreempt() {
    std::printf("[2] the setter at the spinner's own priority: readied, not run, until the spinner rotates\n");
    TestEeYieldPoint rig;
    rig.field = [&rig] { if (rig.fields == 2) INTC().RaiseInPlace(Intc::kGs); };
    rig.isr = [&](CpuContext&) {
        CpuContext c{};
        c.cpuRegs.GPR.r[4].UD[0] = static_cast<uint32_t>(rig.sid);
        SyscallTable::Instance().Dispatch(67, c);
    };
    CpuContext ctx{};
    TM().AdoptCurrentThread(kMainTid, kMainPrio);
    TOK().GuestEnter(kMainTid, kMainPrio);
    const int setter = rig.CreateSetter(kMainPrio);
    CHECK(TM().StartThread(setter, 0, 0x1ff000, 0) == KE_OK, "2a: StartThread(setter prio 5) KE_OK");
    ThreadStatus st{};
    CHECK(TM().ReferThreadStatus(setter, st) == setter && st.status == THS_READY,
          "2b: equal priority: the setter is READY, the spinner kept the CPU");
    // Let the setter reach its WaitSema: the spinner rotates once (its own yield).
    TM().RotateThreadReadyQueue(kMainPrio);
    CHECK(TM().ReferThreadStatus(setter, st) == setter && st.status == THS_WAIT, "2c: after a rotate the setter waits on the sema");
    uint64_t iterations = 0;
    while (iterations < kBudget * 6) {
        ++iterations;
        TestEeYieldPoint::BackEdge(ctx);
    }
    const auto& c = YP().Stats();
    CHECK(memory::read<uint32_t>(kFlagAddr) == 0, "2d: 6 trips later the flag is still 0 -- the readied equal-priority setter did not preempt");
    // The released waiter is READY in the run token's set (contended below); its
    // ThreadInfo status flips from WAIT on its own host thread, which has not run.
    CHECK(c.isrs == 1 && TM().ReferThreadStatus(setter, st) == setter && (st.status == THS_READY || st.status == THS_WAIT),
          "2e: the ISR ran at trip 2 and released the setter (not yet run)");
    CHECK(c.contended >= 4, "2f: the trips after the ISR saw the READY setter (contended)");
    TM().RotateThreadReadyQueue(kMainPrio);   // the guest's own yield
    CHECK(memory::read<uint32_t>(kFlagAddr) == 1, "2g: RotateThreadReadyQueue ran the setter: the flag flipped");
    std::printf("  census: %s\n", c.Format().c_str());
    TOK().GuestExit(kMainTid);
    TM().JoinAll();
}

// --- 3: a trip inside a handler is refused; unbound trips only reload the budget ---------
static void TestGuards() {
    std::printf("[3] guards: a trip inside an ISR is refused (nested / in-dispatch); unbound = reload only\n");
    TestEeYieldPoint rig;
    rig.field = [&rig] { if (rig.fields == 1) INTC().RaiseInPlace(Intc::kGs); };
    uint64_t yields_inside = 0;
    rig.isr = [&](CpuContext& c) {
        const uint64_t before = YP().Stats().yields;
        for (uint32_t i = 0; i < kBudget * 2; ++i) TestEeYieldPoint::BackEdge(c);   // a loop inside the handler
        yields_inside = YP().Stats().yields - before;
    };
    CpuContext ctx{};
    TM().AdoptCurrentThread(kMainTid, kMainPrio);
    TOK().GuestEnter(kMainTid, kMainPrio);
    for (uint32_t i = 0; i < kBudget; ++i) TestEeYieldPoint::BackEdge(ctx);
    const auto& c = YP().Stats();
    CHECK(c.yields == 1 && c.isrs == 1, "3a: one trip, one ISR");
    CHECK(yields_inside == 0 && c.nested == 2 && g_ee_backedge_budget == kBudget,
          "3b: the handler's 2 trips (inside the spinner's trip) were refused as nested; budget reloaded");
    // The same handler dispatched OUTSIDE a trip (the boundary's / the syscall-entry
    // delivery): its trips are refused by the Intc in-dispatch guard.
    INTC().RaiseInPlace(Intc::kGs);
    CHECK(INTC().DispatchPending(ctx) == 1 && yields_inside == 0 && c.inhibited == 2,
          "3c: dispatched outside a trip, the handler's 2 trips were refused as inhibited (Intc::InDispatch)");
    CHECK(INTC().NestedRefusals() == 0, "3d: no nested INTC dispatch was attempted");
    // Unbound: the seam reloads the budget and does nothing else.
    YP().Unbind();
    const uint64_t y = c.yields, t = c.trips;
    g_ee_backedge_budget = 1;
    TestEeYieldPoint::BackEdge(ctx);
    CHECK(g_ee_backedge_budget == EeYieldPoint::kDefaultBudget && c.yields == y && c.trips == t + 1,
          "3e: unbound trip reloaded kDefaultBudget and ran nothing");
    // Not the holder: refused and counted.
    TOK().GuestExit(kMainTid);
    EeYieldPoint::Deps d;
    d.intc = &INTC();
    d.budget = kBudget;
    YP().Bind(std::move(d));
    g_ee_backedge_budget = 1;
    TestEeYieldPoint::BackEdge(ctx);
    CHECK(c.non_holder == 1 && c.yields == y, "3f: a non-holder trip is refused and counted");
    std::printf("  census: %s\n", c.Format().c_str());
    TM().JoinAll();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("EeYieldPoint\n");
    BindMemory();
    TestPreemptedAtTrip();
    TestEqualPriorityNoPreempt();
    TestGuards();
    if (g_fail) std::printf("FAILURES: %d (%d passed)\n", g_fail, g_pass);
    else std::printf("ALL PASS (%d)\n", g_pass);
    return g_fail ? 1 : 0;
}
