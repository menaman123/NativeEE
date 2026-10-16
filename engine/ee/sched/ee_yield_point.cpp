#include "engine/ee/sched/ee_yield_point.h"

#include "engine/ee/hle/alarms.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/timing/emu_domain.h"

EeYieldPoint& EeYieldPoint::Instance() {
    static EeYieldPoint point;
    return point;
}

std::string EeYieldPoint::Census::Format() const {
    return "trips=" + std::to_string(trips) + ":yields=" + std::to_string(yields) + ":isrs=" + std::to_string(isrs) +
           ":contended=" + std::to_string(contended) + ":nested=" + std::to_string(nested) +
           ":inhibited=" + std::to_string(inhibited) + ":non_holder=" + std::to_string(non_holder);
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
    if (deps_.advance_field) {
        deps_.advance_field();
        token.NoteFieldElapsed();  // a thread sleeping on the vblank (WaitVblank) wakes
    }
    if (deps_.alarms && deps_.domain) deps_.alarms->FireDue(deps_.domain->DomainTick());
    if (deps_.timers) deps_.timers->PumpAtBoundary(ctx);
    if (deps_.intc) census_.isrs += static_cast<uint64_t>(deps_.intc->DispatchPending(ctx));
    if (deps_.dispatch_dmac) deps_.dispatch_dmac(ctx);
    if (deps_.queue) deps_.queue->DrainIfPendingOnHolder();
    if (deps_.pump_devices) deps_.pump_devices(ctx);
    if (token.AnyReadyBesides(tid)) ++census_.contended;
    // Cleared BEFORE the preemption point: the thread that runs next may trip on
    // its own loops while this one is parked inside PreemptPoint.
    in_trip_ = false;
    token.PreemptPoint();
}

void EeYieldPoint::ResetForTest() {
    Unbind();
    in_trip_ = false;
    census_ = Census{};
}
