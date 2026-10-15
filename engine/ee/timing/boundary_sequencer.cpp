#include "engine/ee/timing/boundary_sequencer.h"

#include "engine/ee/hle/alarms.h"
#include "engine/ee/timing/boundary_protocol.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/frame_rendezvous.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/ee/timing/tick_profile.h"

void BoundarySequencer::InstallPhase3Steps(const Phase3Deps& deps) {
    deps_ = deps;
    // The tick body: the ONE place a field tick happens under the
    // deterministic policy (the pacer calls the same body under Play).
    ticks_.SetBody([this](TickSync::Cause cause) -> uint64_t {
        const uint64_t raw = deps_.source->Tick();
        // A Deferred tick is a guest WAITER's (the pacer's wall vblank, fired at
        // a timer wait or as the wall-locked boundary's field): the pacer release
        // form -- every pacer tick is one elapsed wall field and rides 1:1 (the
        // clock contract, rule 3: a late frame's elapsed fields reach the title's
        // T0, never a quantum absorption). == OnRawTick unless the release is on.
        if (cause == TickSync::Cause::Deferred) deps_.domain->OnDeferredTick(raw);
        else deps_.domain->OnRawTick(raw);
        // The vblank edge on hardware: VBON, the field's timer-2 event the
        // kernel wires to the vblank-callback thread, VBOF.
        deps_.intc->Raise(Intc::kVbOn);
        deps_.intc->Raise(Intc::kTim2);
        deps_.intc->Raise(Intc::kVbOf);
        return raw;
    });
    SetStep(Step::PresentRendezvous, [this](CpuContext&) {
        last_ticket_ = deps_.rendezvous->Issue();
        static const std::atomic<bool> kAlwaysRunning{true};
        return deps_.rendezvous->WaitFor(last_ticket_, deps_.running ? *deps_.running : kAlwaysRunning);
    });
    SetStep(Step::TickAdvance, [this](CpuContext& ctx) {
        deps_.domain->SetBypass(deps_.source->Bypass());
        deps_.domain->NoteArrival(deps_.source->RawCount());
        if (pacer_ && deps_.source->policy() == TickSource::Policy::Play && !deps_.source->Bypass()) {
            // Wall-locked (the clock contract): the boundary fires at the pacer's
            // 60 Hz wall edge, never before it -- an early EE parks until the next
            // noted tick (rule 1). Every noted tick is one elapsed wall field and
            // rides 1:1 into the domain: the first is the boundary's field; the
            // further ticks a LATE frame finds noted are the fields that elapsed
            // while the EE was busy, delivered to the title's T0 as the console
            // did (rule 3: its per-frame delta grows, the simulation keeps wall
            // speed). Nothing is absorbed and nothing is ever burst-repaid as an
            // extra boundary: the next boundary parks for the next edge again.
            pacer_->ParkForPacerTick();
            // The tick's host work from here on belongs to the NEXT tick's sample
            // (the park's return is the frame work's origin): the fields, the
            // settle, the ISRs are the boundary's site.
            ps2r::ee::TickProfile::Scope profile(ps2r::ee::TickProfile::Boundary);
            const int fired = ticks_.FirePending();
            if (fired == 0) ticks_.FireInline(TickSync::Cause::Boundary);   // pacer stopped: keep the field
            else if (fired > 1) pace_late_fields_ += static_cast<uint64_t>(fired - 1);
            // Fast-forward: extra fields on top of the noted ones, delivered the
            // way a late frame's are (Deferred rides 1:1); the boundary count and
            // its wall edge are unchanged.
            if (extra_fields_) {
                uint32_t extra = extra_fields_();
                if (extra > kExtraFieldsMax) extra = kExtraFieldsMax;
                for (uint32_t i = 0; i < extra; i++) ticks_.FireInline(TickSync::Cause::Deferred);
                fast_forward_fields_ += extra;
            }
        } else {
            ticks_.FireInline(TickSync::Cause::Boundary);  // exactly one per boundary
            ticks_.FirePending();                            // demands noted off-thread
        }
        ps2r::ee::TickProfile::Scope profile(ps2r::ee::TickProfile::Boundary);
        deps_.domain->OnBoundary(deps_.source->RawCount());
        if (deps_.alarms) deps_.alarms->FireDue(deps_.domain->DomainTick());
        if (deps_.timers) deps_.timers->PumpAtBoundary(ctx);
        deps_.intc->DispatchPending(ctx);
        return true;
    });
    SetStep(Step::EventDrain, [this](CpuContext&) {
        if (deps_.queue) deps_.queue->DrainIfPendingOnHolder();
        return true;
    });
}

void BoundarySequencer::BindPacer(BoundaryProtocol* protocol) {
    pacer_ = protocol;
    if (deps_.domain) deps_.domain->SetPacerRelease(protocol != nullptr);
}
