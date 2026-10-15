// test_boundary.cpp -- Stage 3D2: the EE frame boundary, the interrupt
// controller and the EE timers.
//   sequence -- TickSyncBoundaryOrder: the 8 fixed slots in the documented
//               order (4/5/6 empty), a stop at 7 skips 8, exactly one tick
//               body per boundary, deferred demands fire at the boundary,
//               the rendezvous (immediate ack; ticket barrier with a presenter),
//               a foreign-thread INTC raise is queued and applied at the next
//               boundary in FIFO order, two identical boundary sequences give
//               identical T0..T3 / INTC streams, GsSync returns the field.
//   protocol -- BoundaryProtocolPace: ShouldPark predicate at lag 1/2/4, the
//               dead-runahead case, depth clamp, runahead clamp, the demand
//               wait advances the emu clock and terminates a timer wait.
//   timers   -- EeTimersTimr: the MMIO register file on the router (displacing
//               the sink), free-run wrap + OVFF/ISR, ZRET period, CMPE
//               crossings, CUE hold, MODE W1C acks, COUNT rebase, COUNT-read
//               pump, the TIMR codec round trip / invariants, ComposeClock.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/kernl_timer.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/intc/timer_codec.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/timing/boundary_protocol.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/frame_rendezvous.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/ee/recomp_abi/host_app/gs_hle.h"
#include "engine/gs/hle/sce/gs_sync.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) ++g_pass;                                                \
        else { ++g_fail; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, name); } \
    } while (0)

using Step = BoundarySequencer::Step;

namespace {
// One complete boundary rig over fresh engine objects (the Intc/queue/token
// singletons are reset per rig).
constexpr uint32_t kHandlerPcBase = 0x1000;

struct TestBoundary {  // the boundary rig (primary type of this file)
    std::atomic<bool> running{true};
    TickSource source;
    EmuDomain domain;
    DerivedClocks clocks{domain};
    Intc& intc = Intc::Instance();
    MmioSink sink;
    MmioRouter router{sink};
    EeTimers timers{clocks, intc};
    FrameRendezvous rdv;
    BoundarySequencer seq;
    CpuContext ctx{};
    std::vector<int> dispatched;  // handler causes as they ran

    TestBoundary() {
        intc.ResetForTest();
        SyscallTable::Instance().ResetForTest();
        Alarms::Instance().ResetForTest();
        intc.Register(router);
        timers.Register(router);
        intc.SetGuestCall([this](uint32_t addr, CpuContext&) {
            dispatched.push_back(static_cast<int>(addr - kHandlerPcBase));  // handler pc encodes the cause
            return true;
        });
        BoundarySequencer::Phase3Deps d;
        d.source = &source; d.domain = &domain; d.rendezvous = &rdv; d.intc = &intc;
        d.alarms = &Alarms::Instance(); d.timers = &timers; d.queue = &EeEventQueue::Instance();
        d.running = &running;
        seq.InstallPhase3Steps(d);
        timers.SetCtxProvider([this] { return &ctx; });
        domain.SetRegimePin(1);
    }
    // AddIntcHandler(cause, pc=cause, gp, arg) + _EnableIntc(cause).
    void Handler(int cause) {
        CpuContext c{};
        c.cpuRegs.GPR.r[4].UD[0] = static_cast<uint32_t>(cause);
        c.cpuRegs.GPR.r[5].UD[0] = kHandlerPcBase + static_cast<uint32_t>(cause);  // pc (0 is rejected)
        c.cpuRegs.GPR.r[6].UD[0] = 0;  // next
        c.cpuRegs.GPR.r[7].UD[0] = 0x1234;
        SyscallTable::Instance().Dispatch(16, c);  // AddIntcHandler
        CpuContext e{};
        e.cpuRegs.GPR.r[4].UD[0] = static_cast<uint32_t>(cause);
        SyscallTable::Instance().Dispatch(20, e);  // _EnableIntc
    }
    uint32_t Count(int i) { return static_cast<uint32_t>(router.Read(EeTimers::kBase + i * 0x800u, 4)); }
    void W(int i, uint32_t reg, uint32_t v) { router.Write(EeTimers::kBase + i * 0x800u + reg, 4, v); }
};

// The syscall bodies read guest memory through the generated-code ABI.
void BindMemory() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);
}

// The test thread models the guest run-token holder.
void HoldToken() { EeRunToken::Instance().ResetForTest(); EeRunToken::Instance().GuestEnter(1, 32); }
void ReleaseToken() { EeRunToken::Instance().GuestExit(1); }
}  // namespace

static void Sequence() {
    (void)ThreadManager::Instance();  // installs the queue applier (kIntcRaise -> Intc)
    HoldToken();
    CHECK(EeRunToken::Instance().CurrentThreadHoldsToken(), "seq: test thread holds the run token");
    // --- the fixed order ---
    size_t n = 0;
    const Step* order = BoundarySequencer::Order(&n);
    CHECK(n == 8, "seq: eight steps");
    const Step expect[8] = {Step::PresentRendezvous, Step::TickAdvance, Step::EventDrain, Step::MaintDuties,
                            Step::IopDispatch, Step::EeStreamRpc, Step::Savestate, Step::Verify};
    bool same = true;
    for (size_t i = 0; i < 8; ++i) same = same && order[i] == expect[i];
    CHECK(same, "seq: documented order rendezvous, tick, drain, maint, iop, rpc, savestate, verify");
    CHECK(std::string(BoundarySequencer::StepName(Step::TickAdvance)) == "tick-advance" &&
          std::string(BoundarySequencer::StepName(Step::Savestate)) == "savestate", "seq: step names");
    {
        TestBoundary r;
        CHECK(r.seq.HasStep(Step::PresentRendezvous) && r.seq.HasStep(Step::TickAdvance) && r.seq.HasStep(Step::EventDrain),
              "seq: Phase 3 fills slots 1-3");
        CHECK(!r.seq.HasStep(Step::MaintDuties) && !r.seq.HasStep(Step::IopDispatch) && !r.seq.HasStep(Step::EeStreamRpc),
              "seq: slots 4/5/6 exist and are EMPTY in Phase 3");
        CHECK(!r.seq.HasStep(Step::Savestate) && !r.seq.HasStep(Step::Verify), "seq: slots 7/8 empty until 3E/3F");
        // Visit recorder over all 8 slots + a stop at 7.
        std::vector<Step> visited;
        BoundarySequencer rec;
        int stop_at = -1;
        for (size_t i = 0; i < 8; ++i)
            rec.SetStep(order[i], [&, i](CpuContext&) { visited.push_back(order[i]); return static_cast<int>(i) != stop_at; });
        CHECK(rec.RunBoundary(r.ctx) && visited.size() == 8 && rec.Completed() == 1, "seq: all eight run in order");
        for (size_t i = 0; i < 8; ++i) same = same && visited[i] == order[i];
        CHECK(same, "seq: visited == documented order");
        visited.clear();
        stop_at = 6;
        CHECK(!rec.RunBoundary(r.ctx) && visited.size() == 7 && visited.back() == Step::Savestate && rec.Stopped() == 1,
              "seq: a savestate LOAD stops the sequence before verify");
        CHECK(rec.InFlight() == 0, "seq: in-flight returns to 0");
        // --- exactly one tick body per boundary; deferred demands ---
        r.seq.RunBoundary(r.ctx);
        CHECK(r.source.RawCount() == 1 && r.domain.DomainTick() == 1 && r.seq.Ticks().Fired() == 1 &&
              r.seq.Ticks().FiredByCause(TickSync::Cause::Boundary) == 1, "seq: one tick body per boundary");
        for (int i = 0; i < 100; ++i) r.seq.RunBoundary(r.ctx);
        CHECK(r.source.RawCount() == 101 && r.domain.DomainTick() == 101 && r.domain.BoundaryCount() == 101,
              "seq: 101 boundaries -> 101 raw ticks, 101 emu ticks (regime 1)");
        r.seq.Ticks().NotePending(TickSync::Cause::Demand);
        r.seq.Ticks().NotePending(TickSync::Cause::Demand);
        r.seq.RunBoundary(r.ctx);
        CHECK(r.seq.Ticks().FiredByCause(TickSync::Cause::Deferred) == 2 && r.source.RawCount() == 104 &&
              r.seq.Ticks().Pending() == 0, "seq: two deferred demands fire at the next boundary's tick step");
        CHECK(r.domain.DomainTick() == 102, "seq: deferred ticks stay under the pinned quantum (1 emu tick per boundary)");
        // --- the tick raises VBON/TIM2/VBOF in place (holder) and STAT holds them ---
        const uint32_t vb = (1u << Intc::kVbOn) | (1u << Intc::kTim2) | (1u << Intc::kVbOf);
        CHECK((r.intc.Stat() & vb) == vb && r.intc.QueuedRaiseCount() == 0, "seq: holder raises land in STAT directly");
        CHECK(!r.intc.Pending(), "seq: nothing enabled -> INT0 not asserted");
        // --- rendezvous ---
        CHECK(r.rdv.ImmediateAck() && r.rdv.Issued() == r.rdv.Acked() && r.rdv.Waits() == 0,
              "rdv: Phase 3 tickets self-ack, no wait ever blocks");
        CHECK(r.seq.LastTicket() == r.rdv.Issued(), "rdv: the boundary's ticket is the last issued");
    }
    {
        FrameRendezvous rdv;
        rdv.ArmForPresenter();
        std::atomic<bool> running{true};
        CHECK(rdv.Issued() == 0 && rdv.Acked() == 0, "rdv: initial zero");
        const uint32_t t1 = rdv.Issue(), t2 = rdv.Issue();
        CHECK(t1 == 1 && t2 == 2 && rdv.Acked() == 0, "rdv: tickets monotonic from 1, unacked with a presenter");
        CHECK(rdv.WaitFor(0, running), "rdv: ticket 0 is a no-op");
        rdv.Ack(0);
        CHECK(rdv.Acked() == 0, "rdv: ack 0 ignored");
        std::atomic<bool> got{false}, ret{false};
        std::thread w([&] { ret = rdv.WaitFor(t1, running); got = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(!got.load(), "rdv: waiter blocks until the ack");
        rdv.Ack(t1);
        w.join();
        CHECK(got.load() && ret.load() && rdv.Acked() == 1, "rdv: ack releases the waiter");
        rdv.Ack(3);
        CHECK(rdv.WaitFor(t2, running) && rdv.Acked() == 3, "rdv: a later ack releases an earlier ticket");
        rdv.Ack(2);
        CHECK(rdv.Acked() == 3, "rdv: ack never regresses");
        std::atomic<bool> ret3{true};
        std::thread w3([&] { ret3 = rdv.WaitFor(9, running); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        running = false;
        rdv.Wake();
        w3.join();
        CHECK(!ret3.load() && rdv.Stalls() == 0 && rdv.Abandoned() == 0, "rdv: shutdown releases false, no stall reports");
    }
    // --- foreign INTC raise: queued, applied at the next boundary, in order ---
    {
        TestBoundary r;
        std::vector<int> applied;
        EeEventQueue::Instance().SetApplier([&](const EeEventQueue::Event& ev) {
            if (ev.kind == EeEventQueue::Event::kIntcRaise) { applied.push_back(ev.a); r.intc.RaiseInPlace(ev.a); }
        });
        r.Handler(Intc::kVif1);
        r.Handler(Intc::kGs);
        r.Handler(Intc::kVbOn);
        std::thread foreign([&] {
            CHECK(!EeRunToken::Instance().CurrentThreadHoldsToken(), "intc: the producer is a foreign thread");
            r.intc.Raise(Intc::kVif1);
            r.intc.Raise(Intc::kGs);
        });
        foreign.join();
        CHECK(r.intc.QueuedRaiseCount() == 2 && (r.intc.Stat() & ((1u << Intc::kVif1) | (1u << Intc::kGs))) == 0,
              "intc: foreign raises are QUEUED, STAT untouched on the producer's thread");
        CHECK(EeEventQueue::Instance().HasPending(), "intc: the queue holds the two raises");
        r.seq.RunBoundary(r.ctx);
        CHECK(applied.size() == 2 && applied[0] == Intc::kVif1 && applied[1] == Intc::kGs,
              "intc: applied at the boundary's drain step in FIFO order");
        CHECK((r.intc.Stat() & ((1u << Intc::kVif1) | (1u << Intc::kGs))) == ((1u << Intc::kVif1) | (1u << Intc::kGs)),
              "intc: both bits set after the drain (dispatch is the NEXT boundary's tick step)");
        CHECK(r.dispatched.size() == 1 && r.dispatched[0] == Intc::kVbOn, "intc: the tick's own VBON handler ran this boundary");
        r.dispatched.clear();
        r.seq.RunBoundary(r.ctx);
        if (r.dispatched.size() != 3) for (int c : r.dispatched) std::fprintf(stderr, "  dispatched cause %d\n", c);
        CHECK(r.dispatched.size() == 3 && r.dispatched[0] == Intc::kGs && r.dispatched[1] == Intc::kVbOn &&
              r.dispatched[2] == Intc::kVif1, "intc: pending causes dispatch in ascending cause order, each acked");
        CHECK((r.intc.Stat() & ((1u << Intc::kGs) | (1u << Intc::kVif1) | (1u << Intc::kVbOn))) == 0 &&
              (r.intc.Stat() & (1u << Intc::kVbOf)) != 0, "intc: dispatched causes acked; unmasked VBOF stays pending");
        // MMIO register semantics.
        r.router.Write(Intc::kStatAddr, 4, (1u << Intc::kVbOf) | (1u << Intc::kTim2));
        CHECK(r.router.Read(Intc::kStatAddr, 4) == 0, "intc: STAT write-1-to-clear (VBOF + the unmasked TIM2)");
        r.router.Write(Intc::kMaskAddr, 4, 1u << Intc::kVbOn);
        CHECK((r.router.Read(Intc::kMaskAddr, 4) & (1u << Intc::kVbOn)) == 0 &&
              (SyscallTable::Instance().IntcMask() & (1u << Intc::kVbOn)) == 0, "intc: MASK write toggles; syscall view is the same word");
        CHECK(r.router.UnhandledPageCount() == 0, "intc/timers: no unhandled pages -- INTC and timers displaced the sink");
        // Restore the production applier (ThreadManager -> SetIntcRaise -> the
        // registered Intc) and prove the end-to-end path once.
        ThreadManager::Instance().InstallEventApplier();
        r.intc.ResetForTest();
        std::thread foreign2([&] { r.intc.Raise(Intc::kVif0); });
        foreign2.join();
        CHECK(r.intc.QueuedRaiseCount() == 1 && (r.intc.Stat() & (1u << Intc::kVif0)) == 0,
              "intc: production applier -- foreign raise queued");
        r.seq.RunBoundary(r.ctx);
        CHECK((r.intc.Stat() & (1u << Intc::kVif0)) != 0 && r.intc.RaiseCount() >= 1,
              "intc: ThreadManager's applier -> SetIntcRaise -> RaiseInPlace on the registered controller");
    }
    // --- determinism: two identical boundary sequences -> identical streams ---
    {
        auto run = [&](std::vector<uint64_t>& stream) {
            TestBoundary r;
            r.Handler(Intc::kTim0);
            r.Handler(Intc::kTim1);
            r.W(0, 0x10, 0x0082 | TimerCodec::kModeOvfe);                       // T0: CLKS=2, CUE, OVFE
            r.W(1, 0x20, 500); r.W(1, 0x10, 0x0083 | TimerCodec::kModeZret | TimerCodec::kModeCmpe);  // T1 HBLANK ZRET
            r.W(2, 0x10, 0x0081); r.W(3, 0x10, 0x0080 | TimerCodec::kModeCmpe); r.W(3, 0x20, 0x4000);
            for (int b = 0; b < 60; ++b) {
                if (b == 17) r.W(0, 0x00, 0x1234);  // a mid-run COUNT rebase
                r.seq.RunBoundary(r.ctx);
                for (int i = 0; i < 4; ++i) stream.push_back(r.Count(i));
                stream.push_back(r.intc.Stat());
                stream.push_back(r.dispatched.size());
                stream.push_back(r.domain.DomainTick());
            }
            stream.push_back(r.timers.DispatchCount());
        };
        std::vector<uint64_t> a, b;
        run(a);
        run(b);
        CHECK(a == b && a.size() == 60 * 7 + 1, "determinism: two identical boundary sequences -> identical T0..T3/INTC streams");
        CHECK(a[0] == (9600u & 0xFFFFu) && a[1] == 262u, "determinism: T0 counts 9600/tick, T1 (HBLANK) 262/tick");
        CHECK(a.back() > 0, "determinism: timer ISRs fired during the run");
    }
    // --- GsSync: the owner returns the field parity and counts boundaries ---
    {
        TestBoundary r;
        GsSync::ResetForTest();
        GsSync::Bind(&r.seq, &r.clocks);
        GsSync::SyncV(r.ctx);
        CHECK(GsSync::SyncVCount() == 1 && r.domain.BoundaryCount() == 1 && r.ctx.cpuRegs.GPR.r[2].UD[0] == 1,
              "gs_sync: sceGsSyncV runs one boundary, v0 = field parity (tick 1 -> odd)");
        GsSync::WaitForVblank(r.ctx);
        CHECK(GsSync::SyncVCount() == 2 && r.ctx.cpuRegs.GPR.r[2].UD[0] == 0 && g_vsync_frame.load() == 2,
              "gs_sync: WaitForVblank mirror; presented-frame counter follows the boundary count");
        GsSync::SyncPath(r.ctx);
        CHECK(r.ctx.cpuRegs.GPR.r[2].UD[0] == 0, "gs_sync: sceGsSyncPath is idle in Phase 3");
        GsSync::ResetForTest();
    }
    ReleaseToken();
}

static void Protocol() {
    // Pure predicate (OLD ee_pace P1): park exactly when declared > processed + lag.
    for (uint32_t lag : {1u, 2u, 4u}) {
        bool ok = true;
        for (uint32_t p = 0; p < 50; ++p)
            for (uint32_t d = 0; d < 60; ++d) ok = ok && BoundaryProtocol::ShouldPark(d, p, lag) == (d > p + lag);
        CHECK(ok, "pace: ShouldPark == declared > processed + lag");
    }
    // Dead-runahead regression: injected (non-EE) processing never releases a park.
    {
        uint32_t processed = 3, declared = 6;
        bool parked = BoundaryProtocol::ShouldPark(declared, processed, 2);
        for (int i = 0; i < 1000; ++i) { /* injected presents: processed unchanged */ }
        CHECK(parked && BoundaryProtocol::ShouldPark(declared, processed, 2), "pace: a flood of injected presents never releases");
        ++processed;
        CHECK(!BoundaryProtocol::ShouldPark(declared, processed, 2), "pace: one EE frame processed releases");
    }
    CHECK(BoundaryProtocol::Depth(5, 7) == 0 && BoundaryProtocol::Depth(7, 5) == 2, "pace: depth clamps at 0, never wraps");
    TickSource src;
    TickSync ticks;
    EmuDomain dom;
    dom.SetRegimePin(1);
    ticks.SetBody([&](TickSync::Cause) { const uint64_t r = src.Tick(); dom.OnRawTick(r); return r; });
    BoundaryProtocol proto(src, ticks);
    CHECK(proto.Runahead() == 2, "pace: default runahead 2");
    proto.SetRunahead(0); CHECK(proto.Runahead() == 1, "pace: runahead clamps up to 1");
    proto.SetRunahead(9); CHECK(proto.Runahead() == 4, "pace: runahead clamps down to 4");
    CHECK(proto.ShouldPark(7, 2) && !proto.ShouldPark(6, 2), "pace: member predicate uses the configured lag");
    // The demand wait: each Wait demands one field in-line (deterministic).
    proto.Wait(0);
    CHECK(src.RawCount() == 1 && dom.DomainTick() == 1 && ticks.FiredByCause(TickSync::Cause::Demand) == 1,
          "protocol: a timer wait demands exactly one field tick");
    // Under an exhausted quantum the domain absorbs at most kIdleReleaseTicks demands, then rides 1:1.
    dom.OnBoundary(src.RawCount());
    dom.OnRawTick(src.Tick());  // the frame's one field
    const uint64_t before = dom.DomainTick();
    int demands = 0;
    while (dom.DomainTick() == before && demands < 100) { proto.Wait(0); ++demands; }
    CHECK(demands >= 1 && demands <= static_cast<int>(EmuDomain::kIdleReleaseTicks) + 1,
          "protocol: an exhausted quantum absorbs at most the idle window, then the wait terminates");
    // KernlTimer's busy-wait terminates through the hook (T0 domain).
    DerivedClocks clocks(dom);
    KernlTimer::ResetForTest();
    clocks.InstallKernelClocks(SyscallTable::Instance(), proto.WaitHook());
    const uint64_t t0 = clocks.T0Counts();
    uint64_t now = t0;
    int loops = 0;
    while (now < t0 + 5 * 9600 && loops < 1000) { proto.Wait(t0 + 5 * 9600); now = clocks.T0Counts(); ++loops; }
    CHECK(now >= t0 + 5 * 9600 && loops <= 5 + static_cast<int>(EmuDomain::kIdleReleaseTicks), "protocol: a 5-field T0 wait completes in <= 5 + idle-window demands");
    // A demand from a non-guest thread is deferred, never fired there.
    proto.SetGuestThreadPredicate([] { return false; });
    const uint64_t raw = src.RawCount();
    proto.Wait(0);
    CHECK(src.RawCount() == raw && ticks.Pending() == 1 && proto.DeferredDemands() == 1, "protocol: non-guest demand is deferred");
    CHECK(ticks.FirePending() == 1 && src.RawCount() == raw + 1, "protocol: the deferred demand fires at the next fixed point");
    // Play policy: the pacer (a non-guest thread) NOTES a tick and wakes the
    // waiter; the waiter -- the guest fixed point -- fires it, so the clock it
    // re-reads has advanced. No tick: the wait returns after one period with
    // nothing fired (a bounded deadline, never a burst).
    {
        TickSource psrc;
        psrc.SetPolicy(TickSource::Policy::Play);
        psrc.SetMode(TickSource::Mode::Turbo, TickSource::kTurboKMax);  // 260 us period: a fast test, same rule
        TickSync pticks;
        EmuDomain pdom;
        pticks.SetBody([&](TickSync::Cause) { const uint64_t r = psrc.Tick(); pdom.OnRawTick(r); return r; });
        BoundaryProtocol pproto(psrc, pticks);
        pproto.Wait(0);
        CHECK(psrc.RawCount() == 0 && pticks.Fired() == 0 && pproto.Waits() == 1,
              "protocol[play]: a wait with no pacer tick times out without firing anything");
        std::atomic<bool> noted{false};
        std::thread pacer([&] {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            pticks.NotePending(TickSync::Cause::Demand);
            pproto.NotifyTick();
            noted.store(true);
        });
        int waits = 0;
        while (psrc.RawCount() == 0 && waits < 200) { pproto.Wait(0); ++waits; }
        pacer.join();
        CHECK(noted.load() && psrc.RawCount() == 1 && pdom.DomainTick() == 1 && pticks.Pending() == 0 &&
                  pticks.FiredByCause(TickSync::Cause::Deferred) == 1 && pticks.FiredByCause(TickSync::Cause::Demand) == 0,
              "protocol[play]: the pacer's noted tick fires on the waiter, once, as Deferred");
        // A non-guest waiter leaves the pacer's ticks for the next boundary.
        pproto.SetGuestThreadPredicate([] { return false; });
        pticks.NotePending(TickSync::Cause::Demand);
        pproto.NotifyTick();
        pproto.Wait(0);
        CHECK(psrc.RawCount() == 1 && pticks.Pending() == 1, "protocol[play]: a non-guest waiter fires nothing (pending stays for the boundary)");
        CHECK(pticks.FirePending() == 1 && psrc.RawCount() == 2, "protocol[play]: the boundary fires what the pacer noted");
    }
    KernlTimer::ResetForTest();
    SyscallTable::Instance().SetTickClock(nullptr);
    (void)std::string(TickSync::CauseName(TickSync::Cause::Demand));
}

static void Timers() {
    (void)ThreadManager::Instance();
    HoldToken();
    TestBoundary r;
    r.Handler(Intc::kTim0);
    r.Handler(Intc::kTim1);
    auto tick = [&](int n) { for (int i = 0; i < n; ++i) { r.domain.OnRawTick(r.source.Tick()); r.domain.OnBoundary(r.source.RawCount()); } };
    // Registers idle.
    CHECK(r.Count(0) == 0 && r.router.Read(EeTimers::kBase + 0x10, 4) == 0, "timers: idle registers read 0");
    // Free-running T0 at CLKS=2 (9600/tick): 6 ticks = 57600 -> wraps 0x10000 once (57600-65536 <0) -> no; 7 ticks = 67200 -> wrap.
    r.W(0, 0x10, 0x0082 | TimerCodec::kModeOvfe);
    tick(6);
    CHECK(r.Count(0) == 57600u && !(r.router.Read(EeTimers::kBase + 0x10, 4) & TimerCodec::kModeOvff), "timers: 6 ticks -> 57600, no wrap");
    r.dispatched.clear();
    tick(1);
    const uint32_t c7 = r.Count(0);
    CHECK(c7 == (67200u & 0xFFFFu), "timers: 7 ticks -> 67200 & 0xFFFF");
    CHECK((r.router.Read(EeTimers::kBase + 0x10, 4) & TimerCodec::kModeOvff) != 0, "timers: OVFF latched at the wrap");
    CHECK(r.dispatched.size() == 1 && r.dispatched[0] == Intc::kTim0 && r.timers.DispatchCount() == 1,
          "timers: the COUNT read pumps the pending overflow ISR (TIM0) before returning the count");
    CHECK((r.intc.Stat() & (1u << Intc::kTim0)) == 0, "timers: TIM0 acked after its handler");
    r.W(0, 0x10, 0x0082 | TimerCodec::kModeOvfe | TimerCodec::kModeOvff);  // ack OVFF (W1C)
    CHECK((r.router.Read(EeTimers::kBase + 0x10, 4) & TimerCodec::kModeOvff) == 0 &&
          (r.router.Read(EeTimers::kBase + 0x10, 4) & TimerCodec::kModeOvfe) != 0, "timers: MODE write-1-to-clear acks OVFF and keeps OVFE");
    // CUE clear holds the count.
    r.W(0, 0x10, 0x0002);
    const uint32_t held = r.Count(0);
    tick(3);
    CHECK(r.Count(0) == held, "timers: CUE clear holds the count");
    // COUNT write rebases.
    r.W(0, 0x00, 0x1234);
    CHECK(r.Count(0) == 0x1234, "timers: COUNT write rebases");
    // ZRET with COMP: period = COMP; T1 at CLKS=3 (262/tick), COMP=500: 2 ticks = 524 -> 1 cycle, count 24.
    r.W(1, 0x20, 500);
    r.W(1, 0x10, 0x0083 | TimerCodec::kModeZret | TimerCodec::kModeCmpe);
    r.dispatched.clear();
    tick(2);
    CHECK(r.Count(1) == 24u, "timers: ZRET returns to 0 at COMP (524 mod 500 = 24)");
    CHECK((r.router.Read(EeTimers::kBase + 0x800 + 0x10, 4) & TimerCodec::kModeEquf) != 0, "timers: EQUF latched");
    CHECK(r.dispatched.size() == 1 && r.dispatched[0] == Intc::kTim1, "timers: CMPE compare event dispatched TIM1");
    // Free-run compare crossing: T3 CLKS=0 (2457600/tick), COMP=0x4000: one tick = 37 wraps + crossing.
    r.W(3, 0x20, 0x4000);
    r.W(3, 0x10, 0x0080 | TimerCodec::kModeCmpe);
    tick(1);
    CHECK((r.router.Read(EeTimers::kBase + 3 * 0x800 + 0x10, 4) & (TimerCodec::kModeEquf | TimerCodec::kModeOvff)) ==
              (TimerCodec::kModeEquf | TimerCodec::kModeOvff), "timers: free-run pass over COMP latches EQUF, wrap latches OVFF");
    CHECK(r.Count(3) == (2457600u & 0xFFFFu), "timers: CLKS=0 counts 2457600 per tick");
    // Boundary pump with events pending but a masked cause: raised, not dispatched, STAT keeps it.
    r.W(2, 0x10, 0x0082 | TimerCodec::kModeOvfe);
    tick(7);
    r.dispatched.clear();
    r.timers.PumpAtBoundary(r.ctx);
    bool tim2_ran = false;
    for (int c : r.dispatched) tim2_ran = tim2_ran || c == Intc::kTim2;
    CHECK((r.intc.Stat() & (1u << Intc::kTim2)) != 0 && !tim2_ran, "timers: masked TIM2 is raised into STAT but not dispatched");
    // Snapshot / restore rebases to now.
    TimerCodec::Regs snap[4];
    r.timers.Snapshot(snap);
    CHECK(snap[0].count_base == r.Count(0), "timers: snapshot settles first");
    tick(5);
    r.timers.Restore(snap);
    CHECK(r.Count(0) == snap[0].count_base && r.timers.Regs(1).baseline_tick == r.domain.DomainTick(), "timers: restore rebases baselines to now");
    // --- codec ---
    TimerCodec::Regs in[4];
    in[0].mode = 0x0e82; in[0].comp = 0x1234; in[0].hold = 0x0042; in[0].count_base = 0xBEEF;
    in[0].baseline_tick = 0x123456789ABCull; in[0].pending_ovf = 3;
    in[1].mode = 0x0180; in[1].comp = 0xFFFF; in[1].pending_cmp = 7;
    in[3].mode = 0x0ac3; in[3].comp = 1; in[3].hold = 0xFFFF; in[3].count_base = 0xFFFF;
    in[3].baseline_tick = ~0ull >> 1; in[3].pending_ovf = ~0ull >> 8; in[3].pending_cmp = 1;
    std::vector<uint8_t> buf;
    TimerCodec::Encode(in, buf);
    CHECK(buf.size() == TimerCodec::kTimrPayloadLen && TimerCodec::kTimrPayloadLen == 136, "codec: payload length 4 x 34");
    TimerCodec::Regs out[4];
    CHECK(TimerCodec::Decode(buf.data(), buf.size(), out), "codec: decode succeeds");
    bool rt = true;
    for (int i = 0; i < 4; ++i)
        rt = rt && out[i].mode == in[i].mode && out[i].comp == in[i].comp && out[i].hold == in[i].hold &&
             out[i].count_base == in[i].count_base && out[i].baseline_tick == in[i].baseline_tick &&
             out[i].pending_ovf == in[i].pending_ovf && out[i].pending_cmp == in[i].pending_cmp;
    CHECK(rt, "codec: every field round-trips");
    CHECK(!TimerCodec::Decode(buf.data(), buf.size() - 1, out), "codec: truncated rejects");
    { auto big = buf; big.push_back(0); CHECK(!TimerCodec::Decode(big.data(), big.size(), out), "codec: oversize rejects"); }
    CHECK(!TimerCodec::Decode(buf.data(), 0, out), "codec: empty rejects");
    { auto bad = buf; uint32_t huge = 0x12340000u; std::memcpy(&bad[6], &huge, 4);
      CHECK(!TimerCodec::Decode(bad.data(), bad.size(), out), "codec: count_base > 0xFFFF rejects"); }
    // MODE bit facts (an SDK boot mode 0xe82: CLKS=2 BUSCLK/256, CUE, OVFE, acks both flags).
    CHECK(TimerCodec::Clks(0x0e82) == 2 && (0x0e82 & TimerCodec::kModeCue) && (0x0e82 & TimerCodec::kModeOvfe) &&
          !(0x0e82 & TimerCodec::kModeCmpe) && !(0x0e82 & TimerCodec::kModeZret), "codec: 0xe82 = CLKS 2, CUE, OVFE");
    CHECK(TimerCodec::ApplyModeWrite(0x0e82 | TimerCodec::kModeOvff, 0x0e82) == (0x0e82 & ~TimerCodec::kModeFlags),
          "codec: writing 0xe82 acks both flags, stores the rest");
    CHECK(TimerCodec::ApplyModeWrite(TimerCodec::kModeEquf | TimerCodec::kModeOvff, 0x0082 | TimerCodec::kModeOvff) ==
              (0x0082 | TimerCodec::kModeEquf), "codec: un-acked flag survives a MODE write");
    CHECK(TimerCodec::ComposeClock(0x1234, 0xBEEF) == 0x1234BEEFull && TimerCodec::ComposeClock(1, 0) == 0x10000ull,
          "codec: (acc << 16) | COUNT");
    CHECK(DerivedClocks::kCountsPerTick[2] == 9600, "codec: CLKS=2 -> 9600 counts per field");
    ReleaseToken();
}

int main(int argc, char** argv) {
    const std::string section = argc > 1 ? argv[1] : "all";
    if (section != "all" && section != "sequence" && section != "protocol" && section != "timers") {
        std::fprintf(stderr, "test_boundary: unknown section '%s' (sequence|protocol|timers|all)\n", section.c_str());
        return 2;
    }
    BindMemory();
    if (section == "sequence" || section == "all") Sequence();
    if (section == "protocol" || section == "all") Protocol();
    if (section == "timers" || section == "all") Timers();
    std::printf("test_boundary[%s]: %d checks, %d failures\n", section.c_str(), g_pass + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
