#include "engine/vu/tiers/mtvu_sync.h"

#include "engine/ee/timing/tick_profile.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

namespace {
size_t Idx(SyncSite site) {
    size_t i = static_cast<size_t>(site);
    if (i >= static_cast<size_t>(SyncSite::Count)) i = static_cast<size_t>(SyncSite::Other);
    return i;
}
}  // namespace

MtvuSync::MtvuSync(MtvuWorker& worker, GifArbiter& arbiter) : worker_(worker), arbiter_(arbiter) {
    for (auto& c : sync_count_) c.store(0, std::memory_order_relaxed);
    for (auto& c : skipped_count_) c.store(0, std::memory_order_relaxed);
}

void MtvuSync::Sync(SyncSite site) {
    if (!worker_.IsRunning() || worker_.OnWorkerThread()) return;
    sync_count_[Idx(site)].fetch_add(1, std::memory_order_relaxed);
    if (worker_.WorkerIdle()) {
        // Lock-free idle fast path: every worker write is visible already.
        if (arbiter_.DrainThreadRunning() && !worker_.InEagerDrain() && arbiter_.hasPending())
            arbiter_.DrainFence();
        if (after_sync_ && !worker_.InEagerDrain()) after_sync_(after_sync_user_);
        return;
    }
    // The wait is the game thread parked on the worker: booked as `vu1wait`.
    ps2r::ee::TickProfile::Scope scope(ps2r::ee::TickProfile::Vu1Wait);
    if (!worker_.InEagerDrain()) {
        // Eager loop: drain the sealed prefix between waits so the parked EE
        // keeps delivering boundaries while the worker finishes.
        worker_.EnterSyncWait();
        for (;;) {
            const uint64_t seen = worker_.SealCount();
            worker_.TryDrainGuarded();
            if (worker_.WorkerIdle()) break;
            worker_.WaitIdleOrSeal(kPollNs, seen);
        }
        worker_.LeaveSyncWait();
        // The worker seals its last marker BEFORE it publishes idle (inflight_
        // released after the seal): a drain that ran between the two saw the
        // marker unsealed and the idle check then ended the loop with that
        // marker still in the inbox. Idle observed = the seal is visible; what
        // the arbiter still holds is ready and goes now, on this thread.
        if (arbiter_.hasPending()) worker_.TryDrainGuarded();
    } else {
        // Nested inside a drain: a plain wait, no second drain.
        worker_.WaitIdle();
    }
    if (arbiter_.DrainThreadRunning() && !worker_.InEagerDrain() && arbiter_.hasPending())
        arbiter_.DrainFence();
    if (after_sync_ && !worker_.InEagerDrain()) after_sync_(after_sync_user_);
}

void MtvuSync::NoteSiteSkipped(SyncSite site) {
    if (!worker_.IsRunning()) return;
    skipped_count_[Idx(site)].fetch_add(1, std::memory_order_relaxed);
}

bool MtvuSync::TryVif1StatBusyView(uint32_t* stat) const {
    if (!worker_.IsRunning() || worker_.OnWorkerThread()) return false;
    if (worker_.WorkerIdle()) return false;
    *stat = kVif1StatBusyView;
    return true;
}

void MtvuSync::ParkVif1StatPoll(bool park) {
    vif1_stat_polls_.fetch_add(1, std::memory_order_relaxed);
    if (!park || !worker_.IsRunning() || worker_.OnWorkerThread() || worker_.InEagerDrain()) return;
    if (worker_.WorkerIdle()) return;
    vif1_stat_parks_.fetch_add(1, std::memory_order_relaxed);
    ps2r::ee::TickProfile::Scope scope(ps2r::ee::TickProfile::Vu1Wait);
    const uint64_t seen = worker_.SealCount();
    worker_.EnterSyncWait();
    worker_.WaitIdleOrSeal(kPollNs, seen);
    worker_.LeaveSyncWait();
}

void MtvuSync::TryEagerDrain() {
    if (!worker_.IsRunning() || worker_.OnWorkerThread() || worker_.InEagerDrain()) return;
    if (!arbiter_.hasSealedReady()) return;
    worker_.TryDrainGuarded();
}

void MtvuSync::TryDrainPending() {
    if (!worker_.IsRunning() || worker_.OnWorkerThread() || worker_.InEagerDrain()) return;
    if (!arbiter_.hasPending()) return;
    worker_.TryDrainGuarded();
}

void MtvuSync::TryParkDrain() {
    if (!DeferOn()) return;
    TryEagerDrain();
}

bool MtvuSync::WaitProgress() {
    if (!worker_.IsRunning() || worker_.OnWorkerThread()) return true;
    if (worker_.InEagerDrain()) return worker_.WorkerIdle();
    // Whatever is sealed goes first; an idle worker after that owes nothing.
    // Idle is published AFTER the last seal (see Sync): an idle worker observed
    // after a drain may have sealed since -- drain once more before answering
    // "nothing more will arrive", or that marker's packets wait for ever.
    const auto drained_idle = [this] {
        if (!worker_.WorkerIdle()) return false;
        if (arbiter_.hasPending()) worker_.TryDrainGuarded();   // inline: delivered here; thread: kicked
        if (arbiter_.DrainInFlight()) return false;             // the drain thread still owes deliveries
        progress_idle_.fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    const uint64_t seen = worker_.SealCount();
    const uint64_t seen_pass = arbiter_.DrainPassGeneration();
    worker_.TryDrainGuarded();
    if (drained_idle()) return true;
    progress_waits_.fetch_add(1, std::memory_order_relaxed);
    {
        ps2r::ee::TickProfile::Scope scope(ps2r::ee::TickProfile::Vu1Wait);
        if (!worker_.WorkerIdle()) {
            worker_.EnterSyncWait();
            worker_.WaitIdleOrSeal(kPollNs, seen);
            worker_.LeaveSyncWait();
        } else {
            // The worker owes nothing; the drain thread does.
            arbiter_.WaitDrainPassAfter(seen_pass, kPollNs);
        }
    }
    worker_.TryDrainGuarded();
    return drained_idle();
}

bool MtvuSync::DeferOn() const { return worker_.IsRunning(); }

uint64_t MtvuSync::SyncCount(SyncSite site) const {
    return sync_count_[Idx(site)].load(std::memory_order_relaxed);
}

uint64_t MtvuSync::SkippedCount(SyncSite site) const {
    return skipped_count_[Idx(site)].load(std::memory_order_relaxed);
}

bool MtvuSync::Redirect(void* user, VuUnit& unit, uint32_t start_pc) {
    auto* self = static_cast<MtvuSync*>(user);
    MtvuWorker& w = self->worker_;
    if (!w.IsRunning() || w.OnWorkerThread() || &unit == &w.Vu1Unit()) return false;
    // The worker owes nothing before the EE's program runs; then the program
    // runs on the ONE live VU1 register file.
    self->Sync(SyncSite::Vu1EeKick);
    w.Vu1Unit().Execute(start_pc);
    return true;
}

void MtvuSync::InstallInto(VuTierDispatch& inline_vu1_dispatch) {
    inline_vu1_dispatch.SetMtvuHook(&MtvuSync::Redirect, this);
}

void MtvuSync::UninstallFrom(VuTierDispatch& inline_vu1_dispatch) {
    inline_vu1_dispatch.SetMtvuHook(nullptr, nullptr);
}
