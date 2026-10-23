#include "engine/ee/timing/boundary_sequencer.h"

#include "engine/ee/sched/ee_yield_point.h"
#include "engine/ee/timing/tick_profile.h"

#include <utility>

namespace {
constexpr BoundarySequencer::Step kOrder[] = {
    BoundarySequencer::Step::PresentRendezvous, BoundarySequencer::Step::TickAdvance,
    BoundarySequencer::Step::EventDrain,        BoundarySequencer::Step::MaintDuties,
    BoundarySequencer::Step::IopDispatch,       BoundarySequencer::Step::EeStreamRpc,
    BoundarySequencer::Step::Savestate,         BoundarySequencer::Step::Verify,
};
static_assert(sizeof(kOrder) / sizeof(kOrder[0]) == BoundarySequencer::kStepCount,
              "every step appears in the fixed order exactly once");
}  // namespace

const char* BoundarySequencer::StepName(Step s) {
    switch (s) {
        case Step::PresentRendezvous: return "present-rendezvous";
        case Step::TickAdvance: return "tick-advance";
        case Step::EventDrain: return "event-drain";
        case Step::MaintDuties: return "maint-duties";
        case Step::IopDispatch: return "iop-dispatch";
        case Step::EeStreamRpc: return "ee-stream-rpc";
        case Step::Savestate: return "savestate";
        case Step::Verify: return "verify";
        default: return "?";
    }
}

const BoundarySequencer::Step* BoundarySequencer::Order(size_t* count) {
    if (count) *count = kStepCount;
    return kOrder;
}

void BoundarySequencer::SetStep(Step s, StepFn fn) {
    const size_t i = static_cast<size_t>(s);
    if (i < kStepCount) steps_[i] = std::move(fn);
}

bool BoundarySequencer::RunBoundary(CpuContext& ctx) {
    EeYieldPoint::Instance().NoteSchedulingPoint();   // E432: the boundary is the title's scheduling point
    ResetServiceCompletion();
    in_flight_.fetch_add(1, std::memory_order_acq_rel);
    bool completed = true;
    try {
        for (Step s : kOrder) {
            const StepFn& fn = steps_[static_cast<size_t>(s)];
            if (!fn) continue;
            // The tick profile (Charter 3 item 3): the Savestate slot is its own
            // site; TickAdvance scopes its post-park work itself (the park is the
            // frame work's origin, never booked); every other slot is `boundary`.
            if (s == Step::TickAdvance) {
                if (!fn(ctx)) { completed = false; break; }
                continue;
            }
            ps2r::ee::TickProfile::Scope profile(s == Step::Savestate ? ps2r::ee::TickProfile::Savestate
                                                                      : ps2r::ee::TickProfile::Boundary);
            if (!fn(ctx)) {
                completed = false;
                break;
            }
        }
    } catch (...) {
        in_flight_.fetch_sub(1, std::memory_order_acq_rel);  // a load unwinds the boundary thread
        throw;
    }
    in_flight_.fetch_sub(1, std::memory_order_acq_rel);
    if (completed) ++completed_; else ++stopped_;
    if (completed && InFlight() == 0) completed_context_ = &ctx;
    return completed;
}

bool BoundarySequencer::AttachCompletedService(const void* owner, CompletedService callback,
                                               ReturnScheduler schedule) {
    if (!owner || !callback || completion_owner_ || InFlight()) return false;
    completion_owner_ = owner;
    completed_service_ = std::move(callback);
    schedule_return_ = std::move(schedule);
    ResetServiceCompletion();
    return true;
}

void BoundarySequencer::DetachCompletedService(const void* owner) {
    if (completion_owner_ != owner) return;
    completion_owner_ = nullptr;
    completed_service_ = {};
    schedule_return_ = {};
    ResetServiceCompletion();
}

void BoundarySequencer::ResetServiceCompletion() {
    completed_context_ = nullptr;
    completion_scheduled_ = false;
    ++completion_ordinal_; // Invalidates a notification from before restore/unwind.
}

bool BoundarySequencer::CompleteServiceReturn(CpuContext& ctx, uint64_t frame) {
    if (!completed_service_ || InFlight() || completed_context_ != &ctx || completion_scheduled_)
        return false;
    ResetServiceCompletion(); // Consume before invoking: recursive/throwing consumers cannot replay it.
    completed_service_(ctx, frame);
    return true;
}

bool BoundarySequencer::DeferServiceReturn(CpuContext& ctx, uint64_t frame) {
    if (!completed_service_) return false; // Product is not configured.
    if (!schedule_return_ || InFlight() || completed_context_ != &ctx || completion_scheduled_) {
        ++rejected_returns_;
        return false;
    }
    const auto ordinal = completion_ordinal_;
    completion_scheduled_ = schedule_return_(ctx,
        [this, ordinal, frame](CpuContext& completed) {
            if (ordinal != completion_ordinal_) return;
            completion_scheduled_ = false;
            CompleteServiceReturn(completed, frame);
        });
    if (!completion_scheduled_) ++rejected_returns_;
    return completion_scheduled_;
}
