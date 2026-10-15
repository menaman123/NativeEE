#pragma once
// BoundarySequencer: THE fixed EE frame-boundary order (ARCHITECTURE 7.2,
// PHASE0_IMPL 11). One EE, one instruction stream: on hardware every event
// below lands at a defined point of the EE's own execution, so both boundary
// sites (sceGsSyncV and its WaitForVblank mirror) run exactly this sequence,
// on the boundary thread, after declaring the frame complete:
//
//   1 PresentRendezvous  park until the presenter processed this frame's
//                        ticket (FrameRendezvous; Phase 3: immediate ack)
//   2 TickAdvance        NoteArrival; exactly ONE field tick in-line (TickSync
//                        Boundary body: TickSource::Tick -> EmuDomain::
//                        OnRawTick -> INTC VBON/TIM2/VBOF), then the Deferred
//                        ticks noted off-thread (wall-locked Play: park for the
//                        pacer's 60 Hz edge, fire every noted tick as one
//                        elapsed field 1:1, no own Boundary tick -- BindPacer),
//                        EmuDomain::OnBoundary settle,
//                        Alarms::FireDue, EeTimers::PumpAtBoundary (TIMn ISRs),
//                        then Intc::DispatchPending (the tick's own ISRs)
//   3 EventDrain         EeEventQueue drain on the holder (foreign wakes land
//                        HERE, in FIFO order)
//   4 MaintDuties        EE-paced maintenance + pad stamp        (Phase 4)
//   5 IopDispatch        IOP kernel FRAME-policy dispatch        (Phase 4)
//   6 EeStreamRpc        STREAM.IRX EE-side RPC service          (Phase 4/7)
//   7 Savestate          probe / pending save / load -- a LOAD unwinds the
//                        boundary thread: the sequence STOPS      (3E)
//   8 Verify             harness boundary hooks                  (3F)
//
// The order is a compile-time constant; slots are filled by owners through
// SetStep (an empty slot is a no-op). InstallPhase3Steps wires 1-3 from the
// engine pieces; Phase 4/5/6/7 register INTO slots, never re-order them.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/timing/tick_sync.h"

class Alarms;
class BoundaryProtocol;
class EeEventQueue;
class EeTimers;
class EmuDomain;
class FrameRendezvous;
class Intc;
class TickSource;

class BoundarySequencer {
public:
    enum class Step : uint8_t {
        PresentRendezvous = 0, TickAdvance, EventDrain, MaintDuties, IopDispatch, EeStreamRpc, Savestate, Verify, Count
    };
    static constexpr size_t kStepCount = static_cast<size_t>(Step::Count);
    // Step body: return false to STOP the sequence (boundary thread unwinding).
    using StepFn = std::function<bool(CpuContext&)>;

    struct Phase3Deps {
        TickSource* source = nullptr;
        EmuDomain* domain = nullptr;
        FrameRendezvous* rendezvous = nullptr;
        Intc* intc = nullptr;
        Alarms* alarms = nullptr;      // optional
        EeTimers* timers = nullptr;    // optional
        EeEventQueue* queue = nullptr; // optional
        const std::atomic<bool>* running = nullptr;
    };

    static const char* StepName(Step s);
    static const Step* Order(size_t* count);

    void SetStep(Step s, StepFn fn);
    bool HasStep(Step s) const { return static_cast<bool>(steps_[static_cast<size_t>(s)]); }
    // Slots 1-3 from the engine pieces (2 installs the TickSync Boundary body).
    void InstallPhase3Steps(const Phase3Deps& deps);
    // The wall-locked boundary (Play policy only; the composition root binds the
    // protocol the host pacer wakes, iff it started a pacer): slot 2 parks for the
    // pacer's noted tick and fires the noted ticks instead of its own Boundary
    // tick. EVERY noted tick is one elapsed wall field and rides 1:1 (EmuDomain
    // pacer release): the first is the boundary's field, the rest -- found noted
    // by a LATE boundary -- are the fields that elapsed during the frame's work,
    // delivered to the title's clock (the clock contract, rule 3). Unbound
    // (deterministic / headless): the slot is unchanged.
    void BindPacer(BoundaryProtocol* protocol);
    bool WallLocked() const { return pacer_ != nullptr; }
    // Fields beyond the first that late wall-locked boundaries delivered (rule 3).
    uint64_t PaceLateFields() const { return pace_late_fields_; }
    // Fast-forward (CHARTER 2 A5, play mode): a provider the wall-locked slot
    // asks ONCE per boundary for the number of EXTRA fields to deliver on top
    // of the noted ones (0 = none; capped at kExtraFieldsMax). Each extra field
    // rides 1:1 like a late frame's second field (rule 3: the title's clock sees
    // N fields of elapsed time per boundary), while the boundary itself still
    // fires at the pacer's 60 Hz wall edge (rule 1: game_hz stays 60). Never
    // consulted unbound (deterministic / headless); never counted as late.
    static constexpr uint32_t kExtraFieldsMax = 15;
    using ExtraFields = std::function<uint32_t()>;
    void SetExtraFields(ExtraFields provider) { extra_fields_ = std::move(provider); }
    uint64_t FastForwardFields() const { return fast_forward_fields_; }

    // Runs every slot in order; false when a step stopped the sequence.
    bool RunBoundary(CpuContext& ctx);
    using CompletedService = std::function<void(CpuContext&, uint64_t frame)>;
    using ServiceReturn = std::function<void(CpuContext&)>;
    // Optional adapter-owned scheduler. Must retain the callback and invoke it
    // only after this service's enclosing callable returns, never inline.
    // Generic sequence users can use direct completion without any dispatcher.
    using ReturnScheduler = std::function<bool(CpuContext&, ServiceReturn)>;
    // Bind/unbind before workers start/after they join. One game-state owner.
    bool AttachCompletedService(const void* owner, CompletedService callback,
                                ReturnScheduler schedule = {});
    void DetachCompletedService(const void* owner);
    // Host boundary calls CompleteServiceReturn after finalizing its return.
    // The temporary SDK adapter defers to the enclosing installed callable's
    // return, so wrapper PC/register writes are included. Never notify from a slot.
    bool CompleteServiceReturn(CpuContext& ctx, uint64_t frame);
    bool DeferServiceReturn(CpuContext& ctx, uint64_t frame);
    uint64_t RejectedServiceReturns() const { return rejected_returns_; }
    void ResetServiceCompletion();

    TickSync& Ticks() { return ticks_; }
    uint64_t Completed() const { return completed_; }
    uint64_t Stopped() const { return stopped_; }
    uint32_t InFlight() const { return in_flight_.load(std::memory_order_acquire); }
    uint32_t LastTicket() const { return last_ticket_; }

private:
    StepFn steps_[kStepCount];
    TickSync ticks_;
    Phase3Deps deps_;
    BoundaryProtocol* pacer_ = nullptr;
    uint64_t pace_late_fields_ = 0;
    ExtraFields extra_fields_;
    uint64_t fast_forward_fields_ = 0;
    uint64_t completed_ = 0;
    uint64_t stopped_ = 0;
    uint32_t last_ticket_ = 0;
    std::atomic<uint32_t> in_flight_{0};
    const void* completion_owner_ = nullptr;
    CompletedService completed_service_;
    ReturnScheduler schedule_return_;
    CpuContext* completed_context_ = nullptr;
    uint64_t completion_ordinal_ = 0;
    uint64_t rejected_returns_ = 0;
    bool completion_scheduled_ = false;
};
