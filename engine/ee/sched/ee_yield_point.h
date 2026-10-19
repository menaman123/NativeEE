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
// sleeping threads wait for; and the field park itself ends on a queued
// foreign wake (BoundaryProtocol::Wait), in which case no field passed
// (counted idle_wakes) and the runner re-evaluates. Never a PreemptPoint (the grant makes it a
// no-op) and never a frame. Refused like a trip: unbound, nested, inhibited,
// not a guest thread; a grant refused by the token (something READY / a vblank
// waiter) ends the idle.
//
// Budget: one dec+branch per loop iteration. ~295 MHz / 60 Hz is ~4.9 M cycles
// per field and a loop body is a few to a few dozen instructions, so 2^18
// back-edges per trip is about one field of a tight spin -- kDefaultBudget.
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
        std::function<bool()> inhibit;         // true = refuse this trip (boundary in flight ...)
        uint32_t budget = kDefaultBudget;      // back-edges per trip (0 = kDefaultBudget)
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
        uint64_t contended = 0;    // trips that found another READY thread before PreemptPoint
        uint64_t nested = 0;       // refused: inside a trip
        uint64_t inhibited = 0;    // refused: inhibit() or an INTC dispatch in progress
        uint64_t non_holder = 0;   // refused: not the run-token holder / not a guest thread
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

private:
    EeYieldPoint() = default;
    // Steps 1-4b on the token holder `ctx` runs on (Trip's body before the preemption point).
    void RunField(CpuContext& ctx);
    Deps deps_;
    bool bound_ = false;
    bool in_trip_ = false;
    uint32_t budget_ = kDefaultBudget;
    Census census_;
};
