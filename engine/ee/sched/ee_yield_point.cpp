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
           ":idle_calls=" + std::to_string(idle_calls) + ":idle_fields=" + std::to_string(idle_fields);
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
    if (deps_.intc) census_.isrs += static_cast<uint64_t>(deps_.intc->DispatchPending(ctx, Intc::kSiteYield));
    if (deps_.dispatch_dmac) deps_.dispatch_dmac(ctx);
    if (deps_.queue) deps_.queue->DrainIfPendingOnHolder();
    if (deps_.pump_devices) deps_.pump_devices(ctx);
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
        if (fields == 0) ++census_.idle_calls;   // counted as they happen: a park dump mid-idle sees them
        ++census_.idle_fields;
        in_trip_ = true;
        RunField(ctx);
        in_trip_ = false;
        ++fields;
        token.IdleRelease(tid);
    }
    return fields;
}

void EeYieldPoint::ResetForTest() {
    Unbind();
    in_trip_ = false;
    census_ = Census{};
}
