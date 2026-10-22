#include "engine/ee/sched/ee_yield_point.h"

#include "engine/ee/hle/alarms.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/timing/emu_domain.h"

// The back-edge budget the generated code decrements (recomp_abi/host_app/
// syscalls.h declares it; engine/ee/cpu/seams.cpp forwards the zero to Trip). One
// plain counter: only the run-token holder executes guest code and the token
// changes hands under a mutex; every trip reloads it.
uint32_t g_ee_backedge_budget = EeYieldPoint::kDefaultBudget;

EeYieldPoint& EeYieldPoint::Instance() {
    static EeYieldPoint point;
    return point;
}

std::string EeYieldPoint::Census::Format() const {
    return "trips=" + std::to_string(trips) + ":yields=" + std::to_string(yields) + ":isrs=" + std::to_string(isrs) +
           ":contended=" + std::to_string(contended) + ":nested=" + std::to_string(nested) +
           ":inhibited=" + std::to_string(inhibited) + ":non_holder=" + std::to_string(non_holder) +
           ":idle_calls=" + std::to_string(idle_calls) + ":idle_fields=" + std::to_string(idle_fields) +
           ":idle_isrs=" + std::to_string(idle_isrs) + ":idle_pipeline_waits=" + std::to_string(idle_pipeline_waits) +
           ":idle_wakes=" + std::to_string(idle_wakes) + ":wall_wakes=" + std::to_string(wall_wakes) +
           ":deferred_collects=" + std::to_string(deferred_collects) + ":idle_first=" + std::to_string(idle_first_tick) +
           "@tid" + std::to_string(idle_first_tid) + ":reloads=" + std::to_string(reloads);
}

void EeYieldPoint::Bind(Deps deps) {
    deps_ = std::move(deps);
    budget_ = deps_.budget ? deps_.budget : kDefaultBudget;
    bound_ = true;
    g_ee_backedge_budget = budget_;
}

void EeYieldPoint::Unbind() {
    deps_ = Deps{};
    bound_ = false;
    budget_ = kDefaultBudget;
    g_ee_backedge_budget = budget_;
}

void EeYieldPoint::NoteKernelCall() {
    if (!bound_ || !deps_.reload_on_kernel_call) return;
    g_ee_backedge_budget = budget_;
    ++census_.reloads;
}

void EeYieldPoint::Trip(CpuContext& ctx) {
    g_ee_backedge_budget = budget_;
    ++census_.trips;
    if (!bound_) return;
    if (in_trip_) {
        ++census_.nested;
        return;
    }
    if ((deps_.inhibit && deps_.inhibit()) || (deps_.intc && deps_.intc->InDispatch())) {
        ++census_.inhibited;
        return;
    }
    EeRunToken& token = EeRunToken::Instance();
    const int tid = token.CurrentTid();
    if (!token.CurrentThreadHoldsToken() || tid < 0) {
        ++census_.non_holder;
        return;
    }
    in_trip_ = true;
    ++census_.yields;
    RunField(ctx);
    if (token.AnyReadyBesides(tid)) ++census_.contended;
    // Cleared BEFORE the preemption point: the thread that runs next may trip on
    // its own loops while this one is parked inside PreemptPoint.
    in_trip_ = false;
    token.PreemptPoint();
}

void EeYieldPoint::RunField(CpuContext& ctx) {
    EeRunToken& token = EeRunToken::Instance();
    if (deps_.advance_field) {
        deps_.advance_field();
        token.NoteFieldElapsed();  // a thread sleeping on the vblank (WaitVblank) wakes
    }
    if (deps_.alarms && deps_.domain) deps_.alarms->FireDue(deps_.domain->DomainTick());
    if (deps_.timers) deps_.timers->PumpAtBoundary(ctx);
    // A field passed: a scheduling point for the device pipeline's latched
    // causes (Intc::RaiseDeferred) -- what is latched by now enters STAT and is
    // taken with the field's own interrupts, as an in-place raise would have
    // been pending here. (The idle path arrives with the pipeline quiescent;
    // a yield trip takes what the pipeline has reached, as the inline drain's
    // sealed prefix would have been.)
    if (deps_.intc) {
        if (deps_.intc->CollectDeferred()) ++census_.deferred_collects;
        census_.isrs += static_cast<uint64_t>(deps_.intc->DispatchPending(ctx, Intc::kSiteYield));
    }
    if (deps_.dispatch_dmac) deps_.dispatch_dmac(ctx);
    if (deps_.queue) deps_.queue->DrainIfPendingOnHolder();
    if (deps_.pump_devices) deps_.pump_devices(ctx);
}

bool EeYieldPoint::WakeVblankIfWallFieldDue() {
    if (!bound_ || in_trip_ || !deps_.wall_field_due) return false;
    if ((deps_.inhibit && deps_.inhibit()) || (deps_.intc && deps_.intc->InDispatch())) return false;
    EeRunToken& token = EeRunToken::Instance();
    if (!token.CurrentThreadHoldsToken() || token.CurrentThreadIdling() || token.InIsr() || token.CurrentTid() < 0) return false;
    if (!deps_.wall_field_due()) return false;
    if (!token.WakeVblankWaitersForWallTick()) return false;
    ++census_.wall_wakes;
    token.PreemptPoint();
    return true;
}

uint64_t EeYieldPoint::IdleWhileBlocked(CpuContext& ctx) {
    if (!bound_ || in_trip_) return 0;
    if ((deps_.inhibit && deps_.inhibit()) || (deps_.intc && deps_.intc->InDispatch())) return 0;
    EeRunToken& token = EeRunToken::Instance();
    const int tid = token.CurrentTid();
    if (tid < 0 || token.CurrentThreadHoldsToken()) return 0;   // not a blocked guest thread
    uint64_t fields = 0;
    // One field per grant: the release between fields runs the election over
    // what the field readied; a refused grant (something READY, a vblank waiter,
    // a relaunch hold, shutdown) ends the idle.
    while (token.TryIdleAcquire(tid)) {
        in_trip_ = true;
        // A cause already pending (a queued foreign raise applied here) is taken
        // now, with no field: the handler's wakes run the election below.
        if (deps_.queue) deps_.queue->DrainIfPendingOnHolder();
        if (!(deps_.intc && deps_.intc->Pending())) {
            // Deliveries in flight: park on their progress (bounded), then
            // re-evaluate -- what they delivered may be pending or may have
            // readied a thread. The latched device causes are collected only
            // once the pipeline owes nothing (the observation point's rule:
            // the set collected is then what the guest's own submissions
            // produce, not what the pipeline happened to reach by now).
            if (deps_.pipeline_wait && deps_.pipeline_wait()) {
                ++census_.idle_pipeline_waits;
                in_trip_ = false;
                token.IdleRelease(tid);
                continue;
            }
            if (deps_.intc && deps_.intc->CollectDeferred()) ++census_.deferred_collects;   // the idle loop's observation point
        }
        if (deps_.intc && deps_.intc->Pending()) {
            ++census_.idle_isrs;
            census_.isrs += static_cast<uint64_t>(deps_.intc->DispatchPending(ctx, Intc::kSiteYield));
            if (deps_.dispatch_dmac) deps_.dispatch_dmac(ctx);
            in_trip_ = false;
            token.IdleRelease(tid);
            break;
        }
        // The field park may end early on a foreign wake (no tick fired): the
        // domain tick tells a field from a wake.
        const uint64_t tick0 = deps_.domain ? deps_.domain->DomainTick() : 0;
        RunField(ctx);
        if (!deps_.domain || deps_.domain->DomainTick() != tick0) {
            if (fields == 0) ++census_.idle_calls;   // counted as they happen: a park dump mid-idle sees them
            if (census_.idle_first_tick == 0) {
                census_.idle_first_tick = tick0 + 1;
                census_.idle_first_tid = tid;
            }
            // The schedule-change triage line (optional): the composition prints the guest
            // thread table at the field -- what every thread waited on while the EE had
            // nothing to run and the pipeline owed nothing.
            if (deps_.on_idle_field) deps_.on_idle_field(tick0 + 1, tid);
            ++census_.idle_fields;
            ++fields;
        } else {
            ++census_.idle_wakes;
        }
        in_trip_ = false;
        token.IdleRelease(tid);
    }
    return fields;
}

void EeYieldPoint::ResetForTest() {
    Unbind();
    in_trip_ = false;
    census_ = Census{};
}
