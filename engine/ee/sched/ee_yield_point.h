#pragma once
// EeYieldPoint: the in-line scheduling point of a guest EE loop that reaches no
// kernel call. The recompiler's --yield-backedges emission decrements one plain
// budget at every loop back-edge and calls ps2r_ee_yield_point(ctx) when it
// reaches zero (engine/ee/cpu/seams.cpp forwards here); Trip() reloads the
// budget and, when the composition bound it, does what the console does while
// a thread spins: time passes and interrupts land.
//
// What a trip does, in the boundary's own order but WITHOUT the sequencer (the
// frame boundary is the title's vsync site; a spin is not a frame):
//   1 advance_field  -- BoundaryProtocol::WaitHook: one field of emulated time
//                       (a Demand tick under the deterministic policy; a park for
//                       the pacer's tick under Play). The tick body raises
//                       VBON / TIM2 / VBOF as every field does.
//   2 Alarms::FireDue, EeTimers::PumpAtBoundary -- the kernel alarms and TIMn ISRs
//   3 Intc::DispatchPending -- every pending enabled INTC cause's handlers run on
//                       the spinning thread, as an interrupt would
//   4 EeEventQueue::DrainIfPendingOnHolder -- foreign wakes land
//   4b pump_devices -- the composition's other time-driven devices get the field
//                       too (the IOP kernel's PumpFromParkLoop: a spinner polling an
//                       IOP module through RPC needs the module's threads to run;
//                       the SIF commands the IOP sent are delivered). Optional.
//   5 EeRunToken::PreemptPoint -- thbase R1/R2: a strictly-higher-priority READY
//                       thread (one an ISR's iSignalSema / iWakeupThread readied)
//                       runs now; an equal-priority thread never preempts a
//                       spinner (RotateThreadReadyQueue is the guest's own yield).
// Never RunBoundary: the mirror site is the boundary (present ticket, frame
// count, EmuDomain quantum) and a spin must not count as a frame.
//
// Refused (counted, budget still reloaded): unbound; a nested trip (a loop
// inside an ISR or inside the tick body); a trip while the boundary sequence is
// in flight or an INTC dispatch is running (the composition's `inhibit`); a
// caller that is not the run-token holder or not a guest thread.
//
// The kernel's idle (IdleWhileBlocked): the same sequence, steps 1-4b, run by
// the guest thread that has JUST blocked on a kernel object when nothing is
// READY and nobody waits on the vblank -- the thbase idle state. On the console
// an idle EE still takes its vblank and timer interrupts and the IOP runs on its
// own clock; here nothing else advances emulated time once every guest thread
// sleeps (no boundary: the vsync site is a guest thread; no trip: a trip needs
// a spinning holder). Under EeRunToken's idle grant the runner advances one
// field at a time, releasing after each so the election runs over what the
// field readied (an ISR's iSignalSema, an IOP completion's SIF notify, an
// alarm), and stops at the first READY thread -- its own included. An INTC
// cause already pending when the grant is taken (a foreign thread's raise --
// the GS FINISH the GIF drain thread delivers, an IOP wake -- applied from the
// event queue) is taken FIRST and no field passes for it: the console's idle
// loop takes a pending interrupt at once, it does not sleep to the next vblank
// over it (counted idle_isrs). While the GS pipeline still owes deliveries
// (`pipeline_wait`) the runner parks on their progress instead of passing a
// field (counted idle_pipeline_waits): the FINISH in flight is the event the
// sleeping threads wait for -- and only once the pipeline owes nothing are the
// device causes it LATCHED (Intc::RaiseDeferred) collected and taken (the idle
// grant is an observation point: the set it collects is what the guest's own
// submissions produce, never the prefix the pipeline happened to reach, so the
// in-line and the drain-thread compositions take the interrupt on the same
// thread at the same point); and the field park itself ends on a queued
// foreign wake (BoundaryProtocol::Wait), in which case no field passed
// (counted idle_wakes) and the runner re-evaluates. Never a PreemptPoint (the grant makes it a
// no-op) and never a frame. Refused like a trip: unbound, nested, inhibited,
// not a guest thread; a grant refused by the token (something READY / a vblank
// waiter) ends the idle.
//
// Budget: one dec+branch per loop iteration. ~295 MHz / 60 Hz is ~4.9 M cycles
// per field and a loop body is a few to a few dozen instructions, so 2^18
// back-edges per trip is about one field of a tight spin -- kDefaultBudget.
//
// The budget is one global counter, reloaded by a trip only: it runs across
// every scheduling point the guest passes, so ordinary code trips once per
// 2^18 back-edges anyway (Paris: every ~9 frames) and the trip's field is a
// WALL field under Play -- the thread parks to the next pacer tick (16.7 ms of
// `other` in the frame work, one extra VBON/TIM2 per trip: 670 VBON per 600
// boundaries, 54 pictures/s from a 60 Hz logic with 60 % slack). The SPIN GATE
// (E432, Deps::spin_gate, Config engine.ee_yield_spin_gate) tells the two
// apart: a trip whose thread passed NO scheduling point since the previous trip
// -- no token acquire (EeRunToken::AcquireSlow: a block ended, a preemption, a
// vblank wait) and no boundary (BoundarySequencer::RunBoundary) -- spun, and
// passes its field as before (the console's time moving under a spinner: the
// boot's IOP polls). Any other trip is the 2^18th back-edge of code that
// schedules: it runs the same sequence but fires only the wall fields the pacer
// has ALREADY noted (Deps::fire_due_fields: no park, the console's vblank
// landing mid-frame; nothing under the deterministic / freerun policies), so no
// field is invented and no thread parks. A kernel call is NOT a scheduling
// point here: the boot spins on polling syscalls (iReferEventFlagStatus,
// FlushCache) -- a kernel-call reload hung the cold boot (gs_draws 3, VBON 1).
// Census trips / spin_trips / sched_trips / due_fields.
//
// The PROGRESS GATE (Deps::progress_gate, under the spin gate only) refines
// "spun". The scheduling-point test cannot tell a spin from a long computation:
// a movie picture's decode, a loader's byte loop or a checksum runs 2^18
// back-edges many times over without blocking, and every one of those trips
// parked the thread to the pacer's NEXT edge (up to 16.7 ms lost per ~1 ms of
// work: the boot FMV at 36 pictures/s, its audio time-stretched 1.6x). Two tests
// decide a trip, both the console's own behaviour:
//   * an interrupt is already deliverable (an INTC cause pending, a DMAC INT1
//     pending): it is taken NOW with no field -- a spinner waiting for the
//     handler's side effect sees it a few instructions later, as on the console
//     (counted irq_trips); the DMAC's raise asks for this trip at the next
//     back-edge (RequestPromptTrip, counted prompt_trips) instead of 2^18 later;
//   * the thread's register file (GPR 1..31) is byte-identical to what it was at
//     the previous trip with no scheduling point between: nothing moved, a true
//     spin -> the field passes as before. A register that changed -- a pointer,
//     a counter, a loaded value -- is progress: the trip fires only the due wall
//     fields (counted progress_trips). A spin that updates a register (a timeout
//     count, a Count read) is classified progress and burns until a wall field
//     is due; kProgressTripsMax consecutive progress trips with no field fired
//     and no scheduling point fall back to the park (counted progress_parks), so
//     a tight register-changing spin burns at most that many trips' worth.
// Deterministic / freerun lanes: the spin gate is pinned off there, so every
// trip is a field as before and no hash moves.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/ee/cpu/cpu_state.h"

class Alarms;
class EeEventQueue;
class EeTimers;
class EmuDomain;
class Intc;

class EeYieldPoint {
public:
    static constexpr uint32_t kDefaultBudget = 1u << 18;

    struct Deps {
        std::function<void()> advance_field;   // one field of emulated time (BoundaryProtocol::WaitHook bound)
        // A WALL field is due: the pacer noted a tick no boundary has consumed yet
        // (BoundaryProtocol::WallFieldDue under Play; nullptr / false = never).
        std::function<bool()> wall_field_due;
        EmuDomain* domain = nullptr;           // with alarms: FireDue(domain->DomainTick())
        Alarms* alarms = nullptr;
        EeTimers* timers = nullptr;
        Intc* intc = nullptr;
        // Step 4a': the DMAC's INT1 handlers (D_STAT CIS & CIM) dispatched on this thread
        // right after the INTC causes; optional (a spinning guest loop that waits for a DMA
        // completion handler to run has no kernel call to deliver it at).
        std::function<void(CpuContext&)> dispatch_dmac;
        EeEventQueue* queue = nullptr;
        std::function<void(CpuContext&)> pump_devices;   // step 4b (IOP pump, SIF command delivery); optional
        // The idle runner's wait on the GS pipeline (optional): returns true when deliveries
        // were still in flight (the MTVU worker's spans, the GIF drain thread's parser) and it
        // parked on their progress for a bounded time -- no field passes over an in-flight
        // FINISH; the runner re-evaluates (a pending cause, a readied thread, the next wait).
        // False = the pipeline owes nothing: the field may pass. MtvuSync::WaitProgress bound.
        std::function<bool()> pipeline_wait;
        // An idle field passed (optional, triage): the domain tick it landed on and the
        // blocked guest thread that ran the idle. Census idle_first= records the first.
        std::function<void(uint64_t tick, int tid)> on_idle_field;
        std::function<bool()> inhibit;         // true = refuse this trip (boundary in flight ...)
        uint32_t budget = kDefaultBudget;      // back-edges per trip (0 = kDefaultBudget)
        bool spin_gate = false;                // E432 (header): a non-spin trip passes no field of its own
        // The progress gate (header): under the spin gate, a trip whose register file moved since
        // the previous trip, or that finds an interrupt deliverable, passes no field of its own.
        bool progress_gate = false;
        // A DMAC INT1 is pending (D_STAT CIS & CIM): Dmac::CheckInterrupt bound; optional.
        std::function<bool()> dmac_pending;
        // E432: fire the wall fields the pacer has already noted, without a park
        // (BoundaryProtocol::FireDueFields); nothing when none is due. Optional.
        std::function<void()> fire_due_fields;
    };

    struct Census {
        uint64_t trips = 0;        // ps2r_ee_yield_point calls
        uint64_t yields = 0;       // trips that ran the sequence
        uint64_t isrs = 0;         // INTC handlers dispatched by trips (and idle fields)
        uint64_t idle_calls = 0;   // IdleWhileBlocked calls that found the EE idle (>= 1 field)
        uint64_t idle_fields = 0;  // fields advanced by the idle runner
        uint64_t idle_isrs = 0;    // idle grants that took a pending INTC cause instead of a field
        uint64_t idle_pipeline_waits = 0;   // idle grants that parked on the GS pipeline instead of a field
        uint64_t idle_wakes = 0;   // idle grants whose field park ended on a foreign wake before the tick (no field passed)
        uint64_t wall_wakes = 0;   // WakeVblankIfWallFieldDue: a vblank waiter readied by a due wall field
        uint64_t deferred_collects = 0;   // RunField / idle grants that collected a latched device cause (Intc::CollectDeferred)
        uint64_t idle_first_tick = 0;     // the domain tick the first idle field passed at (0 = none)
        int idle_first_tid = -1;          // ... and the blocked guest thread that ran the idle
        uint64_t contended = 0;    // trips that found another READY thread before PreemptPoint
        uint64_t nested = 0;       // refused: inside a trip
        uint64_t inhibited = 0;    // refused: inhibit() or an INTC dispatch in progress
        uint64_t non_holder = 0;   // refused: not the run-token holder / not a guest thread
        uint64_t spin_trips = 0;   // E432: trips that passed a field (spun: no scheduling point since the previous trip)
        uint64_t sched_trips = 0;  // E432: trips of scheduling code (no field of their own)
        uint64_t due_fields = 0;   // E432: sched trips that found a wall field due and fired it
        uint64_t progress_trips = 0;   // progress gate: the register file moved since the previous trip (no field of its own)
        uint64_t progress_parks = 0;   // progress gate: kProgressTripsMax progress trips without a field fired -> parked anyway
        uint64_t irq_trips = 0;        // progress gate: an INTC / DMAC interrupt was deliverable -> taken, no field
        uint64_t prompt_requests = 0;  // RequestPromptTrip calls (a device raise asked for the next back-edge)
        uint64_t prompt_trips = 0;     // trips that arrived on a prompt request
        uint64_t budget_restores = 0;   // savestate restores that re-seated the back-edge budget (YLDP section)
        uint64_t budget_absent = 0;     // restores whose image carried no YLDP section (the budget kept the process's value)
        uint32_t last_restored_remaining = 0;   // the remaining back-edges the last restore seated
        std::string Format() const;
    };

    static EeYieldPoint& Instance();

    // The composition binds once; Unbind returns to the reload-only behaviour.
    void Bind(Deps deps);
    void Unbind();
    bool Bound() const { return bound_; }
    uint32_t Budget() const { return budget_; }

    // The seam body (seams.cpp: ps2r_ee_yield_point). Reloads g_ee_backedge_budget first.
    void Trip(CpuContext& ctx);
    // A scheduling point passed (E432): a guest thread gained the run token
    // (EeRunToken::AcquireSlow) or a boundary ran (BoundarySequencer::RunBoundary).
    // One relaxed increment; the spin gate reads it at the trip.
    void NoteSchedulingPoint() { sched_gen_.fetch_add(1, std::memory_order_relaxed); }
    bool SpinGate() const { return bound_ && deps_.spin_gate; }
    bool ProgressGate() const { return SpinGate() && deps_.progress_gate; }
    // A device raised an interrupt a spinning holder may be waiting for (the DMAC's
    // channel completion): the next back-edge trips (the budget is set to 1) and, under
    // the progress gate, delivers it without a field. Holder thread only (the DMAC runs
    // on the guest's own kick); a no-op when the progress gate is off. Counted.
    void RequestPromptTrip();
    static constexpr uint32_t kProgressTripsMax = 16;
    // The wall vblank landing on a RUNNING thread (a scheduling point of the
    // holder: the title's syscall entry). When `wall_field_due` holds and a
    // thread sleeps in WaitVblank, that thread is readied (no field passes
    // here: the boundary it then runs finds the tick noted and fires it, as a
    // late boundary does) and the holder takes its preemption point -- a
    // higher-priority vsync thread runs its boundary and kicks the next frame
    // while this thread finishes the game's logic, as on the console, instead
    // of the vblank waiting for every other thread to block (the idle-skip
    // model serialises the frame's VU1 work after the whole logic tick).
    // Counted (wall_wakes). Refused like a trip (unbound, nested, inhibited,
    // in an INTC dispatch, not the holder, the idle runner, inside an ISR).
    bool WakeVblankIfWallFieldDue();
    // The kernel's idle (header comment): called by a kernel-object wait right
    // after EeRunToken::BlockBegin with no lock held; returns the fields advanced
    // (0 = the EE was not idle or the call was refused).
    uint64_t IdleWhileBlocked(CpuContext& ctx);

    const Census& Stats() const { return census_; }
    void ResetForTest();

    // SAVESTATE (the YLDP section, engine/ee/savestate/yield_section.*). The budget
    // is one global counter reloaded only by a trip, so the back-edges remaining at
    // the boundary decide WHEN the next trip's field passes -- which point of the
    // main loop the next vblank lands on (a vblank sleeper readied mid-frame). A
    // restore that leaves the process's own residual in place lands the next
    // trip elsewhere than the saved run's: a one-frame main-loop phase slip a few
    // frames after the load. Capture reads the residual on the boundary owner
    // (the only holder; no guest code runs between the boundary and the capture);
    // the restore STAGES it, and ThreadRelauncher seats it right before the
    // boundary thread resumes -- after the waiters' re-executed wait bodies have
    // parked (their back-edges were never in the saved run).
    uint32_t BudgetRemaining() const;
    void ReloadBudget();   // the live counter back to the full budget (no guest code may be running)
    void StageRestoredBudget(uint32_t remaining);
    // Seats the staged value into the live counter (once); false when nothing was staged.
    bool ApplyRestoredBudget();
    void NoteBudgetAbsent() { ++census_.budget_absent; }

private:
    EeYieldPoint() = default;
    // Steps 1-4b on the token holder `ctx` runs on (Trip's body before the preemption point).
    // advance = false (E432, a scheduling trip): step 1 fires only the due wall fields.
    void RunField(CpuContext& ctx, bool advance = true);
    Deps deps_;
    bool bound_ = false;
    bool in_trip_ = false;
    uint32_t budget_ = kDefaultBudget;
    Census census_;
    std::atomic<uint64_t> sched_gen_{0};   // NoteSchedulingPoint count
    uint64_t sched_gen_at_trip_ = 0;       // ... as read by the previous trip (holder only)
    // Progress gate state (holder only): the GPR 1..31 low words at the previous trip, whether
    // that snapshot is valid, the run of progress trips since a field / scheduling point, and
    // whether a prompt trip was requested since the previous trip.
    uint64_t gpr_at_trip_[31] = {};
    bool gpr_at_trip_valid_ = false;
    uint32_t progress_run_ = 0;
    bool prompt_pending_ = false;
    bool restore_staged_ = false;          // YLDP restore pending its seat (ApplyRestoredBudget)
    uint32_t restore_remaining_ = 0;
};
