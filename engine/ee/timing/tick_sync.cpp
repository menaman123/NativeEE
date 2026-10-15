#include "engine/ee/timing/tick_sync.h"

#include <limits>

const char* TickSync::CauseName(Cause c) {
    switch (c) {
        case Cause::Boundary: return "boundary";
        case Cause::CdvdDelivery: return "cdvd";
        case Cause::Demand: return "demand";
        case Cause::Deferred: return "deferred";
        default: return "?";
    }
}

uint64_t TickSync::FireInline(Cause cause) {
    if (!body_) return 0;
    // Single-EE-thread contract: a body already running on another thread is
    // counted, never waited for (a lock here could deadlock against the run-
    // token handover inside the body).
    if (in_body_.exchange(true, std::memory_order_acq_rel)) concurrent_.fetch_add(1, std::memory_order_relaxed);
    const uint64_t idx = body_(cause);
    in_body_.store(false, std::memory_order_release);
    fired_.fetch_add(1, std::memory_order_relaxed);
    const int ci = static_cast<int>(cause);
    if (ci >= 0 && ci < static_cast<int>(Cause::Count)) fired_by_cause_[ci].fetch_add(1, std::memory_order_relaxed);
    return idx;
}

void TickSync::NotePending(Cause) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    noted_.fetch_add(1, std::memory_order_relaxed);
}

int TickSync::FirePending() {
    int n = 0;
    while (FirePendingOne()) ++n;
    return n;
}

int TickSync::FirePendingOne() {
    for (;;) {
        uint64_t p = pending_.load(std::memory_order_acquire);
        if (p == 0) return 0;
        if (!pending_.compare_exchange_weak(p, p - 1, std::memory_order_acq_rel)) continue;
        FireInline(Cause::Deferred);
        return 1;
    }
}

uint64_t TickSync::FiredByCause(Cause c) const {
    const int ci = static_cast<int>(c);
    if (ci < 0 || ci >= static_cast<int>(Cause::Count)) return 0;
    return fired_by_cause_[ci].load(std::memory_order_relaxed);
}

bool TickSync::RestorePending(uint64_t pending) {
    // FirePending's result is an int; a file must not overflow that count.
    if (BodyActive() || pending > static_cast<uint64_t>(std::numeric_limits<int>::max())) return false;
    pending_.store(pending, std::memory_order_release);
    return true;
}

void TickSync::ResetCounters() {
    fired_.store(0);
    for (auto& f : fired_by_cause_) f.store(0);
    pending_.store(0);
    noted_.store(0);
    in_body_.store(false);
    concurrent_.store(0);
}
