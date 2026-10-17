// Intc refusal and edge census: a cause raised WHILE a handler runs is refused at any
// dispatch point reached from inside the handler (the console's EXL: INTC handlers do not
// nest), stays pending in STAT, and is serviced at the next dispatch point after the
// handler returns -- no edge is lost. The census names the refusal by (site, cause in
// dispatch, cause refused) and the per-cause edge accounting (raised / dispatched /
// guest-acked / still-pending) balances.
//   Scenario (the title's): the GS FINISH handler runs at a syscall entry; during it the
//   vblank lands (VBON raised) and the handler's own kernel call reaches the syscall-entry
//   dispatch point -> refused, keyed syscall/GS>VBON. The handler returns; the boundary's
//   DispatchPending runs the VBON handler. A guest W1C ack of a raised cause without a
//   handler is counted as acked, not lost.
#include <cstdio>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_run_token.h"

// The seam TU pulls in the park-site dump's shadow-stack accessors the generated common TU
// defines; this test links no title (as tests/ee/test_ee_yield_point.cpp).
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
constexpr uint32_t kGsIsrPc = 0x00200000u;
constexpr uint32_t kVbIsrPc = 0x00200100u;

Intc& INTC() { return Intc::Instance(); }

// AddIntcHandler reads its 5th argument from the guest stack: bind guest memory.
void BindMemory() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);
}

// AddIntcHandler(cause, pc, next 0, arg) + _EnableIntc(cause) through the real syscalls.
void AddHandler(int cause, uint32_t pc) {
    CpuContext c{};
    c.cpuRegs.GPR.r[4].UD[0] = static_cast<uint64_t>(cause);
    c.cpuRegs.GPR.r[5].UD[0] = pc;
    c.cpuRegs.GPR.r[6].UD[0] = 0;
    c.cpuRegs.GPR.r[7].UD[0] = 0x55;
    c.cpuRegs.GPR.r[29].UD[0] = 0x001ff000;
    SyscallTable::Instance().Dispatch(16, c);
    CpuContext e{};
    e.cpuRegs.GPR.r[4].UD[0] = static_cast<uint64_t>(cause);
    SyscallTable::Instance().Dispatch(20, e);
}

uint64_t Unaccounted(int cause) {
    return INTC().RaisedCount(cause) -
           (INTC().DispatchedCount(cause) + INTC().GuestAckedCount(cause) + ((INTC().Stat() >> cause) & 1u));
}
}  // namespace

int main() {
    BindMemory();
    MmioSink sink;
    MmioRouter router{sink};
    ThreadManager::Instance().ResetForTest();
    EeRunToken::Instance().ResetForTest();
    INTC().ResetForTest();
    SyscallTable::Instance().ResetForTest();
    INTC().Register(router);
    ThreadManager::Instance().InstallEventApplier();

    int gs_runs = 0, vb_runs = 0, nested_ran = -1;
    INTC().SetGuestCall([&](uint32_t addr, CpuContext& ctx) {
        if (addr == kGsIsrPc) {
            ++gs_runs;
            INTC().RaiseInPlace(Intc::kVbOn);                                   // the vblank lands mid-handler
            nested_ran = INTC().DispatchPending(ctx, Intc::kSiteSyscallEntry);  // iSignalSema's entry point
            CHECK(INTC().InDispatch() && INTC().Pending(Intc::kVbOn), "VBON stays pending inside the GS handler");
        } else if (addr == kVbIsrPc) {
            ++vb_runs;
        }
        return true;
    });
    AddHandler(Intc::kGs, kGsIsrPc);
    AddHandler(Intc::kVbOn, kVbIsrPc);

    std::printf("[1] a cause raised inside a handler is refused, pending, then serviced at the next point\n");
    CpuContext ctx{};
    INTC().RaiseInPlace(Intc::kGs);
    const int ran = INTC().DispatchPending(ctx, Intc::kSiteSyscallEntry);
    CHECK(ran == 1 && gs_runs == 1 && nested_ran == 0, "GS handler ran once; the nested attempt ran nothing");
    CHECK(INTC().NestedRefusals() == 1, "one nested refusal counted");
    CHECK(INTC().RefusalCount(Intc::kSiteSyscallEntry, Intc::kGs, Intc::kVbOn) == 1, "keyed syscall/GS>VBON");
    CHECK(INTC().RefusalCensus() == "syscall/GS>VBON=1", INTC().RefusalCensus().c_str());
    CHECK(vb_runs == 0 && INTC().Pending(Intc::kVbOn) && !INTC().InDispatch(),
          "after the GS handler: VBON still pending, nothing in dispatch");
    const int ran2 = INTC().DispatchPending(ctx, Intc::kSiteBoundary);
    CHECK(ran2 == 1 && vb_runs == 1 && !INTC().Pending(Intc::kVbOn), "the boundary serviced the refused VBON");
    CHECK(Unaccounted(Intc::kGs) == 0 && Unaccounted(Intc::kVbOn) == 0, "no lost edge: raised == dispatched");
    CHECK(INTC().EdgeCensus() == "GS:1/1/0/0,VBON:1/1/0/0", INTC().EdgeCensus().c_str());

    std::printf("[2] a Dispatch(cause) of a single cause inside a handler is keyed by its site\n");
    INTC().SetGuestCall([&](uint32_t addr, CpuContext& c) {
        if (addr == kGsIsrPc) {
            INTC().RaiseInPlace(Intc::kTim1);
            INTC().Dispatch(c, Intc::kTim1, Intc::kSiteTimerPump);   // the timer pump reached from the handler
            INTC().RaiseInPlace(Intc::kVbOn);
            INTC().DispatchPending(c, Intc::kSiteYield);              // a back-edge trip inside the handler
        }
        return true;
    });
    // TIM1 has no handler and is masked: Dispatch refuses by the gate, not the nesting.
    INTC().RaiseInPlace(Intc::kGs);
    INTC().DispatchPending(ctx, Intc::kSiteBoundary);
    CHECK(INTC().RefusalCount(Intc::kSiteTimerPump, Intc::kGs, Intc::kTim1) == 0, "a masked cause is not a nested refusal");
    CHECK(INTC().RefusalCount(Intc::kSiteYield, Intc::kGs, Intc::kVbOn) == 1, "the yield-site refusal is keyed yield/GS>VBON");
    CHECK(INTC().NestedRefusals() == 2, "two refusals in total");
    // The guest acknowledges TIM1 itself (W1C): counted as acked, the edge is not lost.
    INTC().Write(Intc::kStatAddr, 4, 1u << Intc::kTim1);
    CHECK(INTC().GuestAckedCount(Intc::kTim1) == 1 && Unaccounted(Intc::kTim1) == 0, "guest W1C ack balances TIM1");
    INTC().DispatchPending(ctx, Intc::kSiteBoundary);
    CHECK(Unaccounted(Intc::kVbOn) == 0 && Unaccounted(Intc::kGs) == 0, "all edges serviced or acked");
    CHECK(INTC().RefusalCensus() == "yield/GS>VBON=1,syscall/GS>VBON=1", INTC().RefusalCensus().c_str());   // ties: site order

    std::printf("[3] a re-raise of a set bit is not a new edge\n");
    const uint64_t before = INTC().RaisedCount(Intc::kVbOn);
    INTC().RaiseInPlace(Intc::kVbOn);
    INTC().RaiseInPlace(Intc::kVbOn);
    CHECK(INTC().RaisedCount(Intc::kVbOn) == before + 1 && INTC().RaiseCount() > before, "one edge, two Raise calls");
    INTC().DispatchPending(ctx, Intc::kSiteBoundary);
    CHECK(Unaccounted(Intc::kVbOn) == 0, "balanced");

    std::printf("test_intc_census: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
