#include "engine/ee/timing/frame_rendezvous.h"

#include <chrono>
#include <cstdio>

uint32_t FrameRendezvous::Issue() {
    const uint32_t t = issued_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (immediate_ack_) Ack(t);  // Phase 3: no presenter, the ticket self-acks
    return t;
}

void FrameRendezvous::Ack(uint32_t ticket) {
    if (ticket == 0) return;
    {
        // Publish under the mutex: a waiter is either before its predicate
        // check (sees the value under the lock) or blocked (gets the notify).
        std::lock_guard<std::mutex> lk(mu_);
        uint32_t cur = acked_.load(std::memory_order_relaxed);
        while (cur < ticket && !acked_.compare_exchange_weak(cur, ticket, std::memory_order_acq_rel)) {
        }
    }
    cv_.notify_all();
}

void FrameRendezvous::Wake() {
    { std::lock_guard<std::mutex> lk(mu_); }
    cv_.notify_all();
}

bool FrameRendezvous::WaitFor(uint32_t ticket, const std::atomic<bool>& running) {
    if (ticket == 0) return true;
    auto pred = [&] {
        return acked_.load(std::memory_order_acquire) >= ticket || !running.load(std::memory_order_relaxed);
    };
    if (pred()) return acked_.load(std::memory_order_acquire) >= ticket;
    waits_.fetch_add(1, std::memory_order_relaxed);
    unsigned waited_s = 0;
    std::unique_lock<std::mutex> lk(mu_);
    for (;;) {
        if (cv_.wait_for(lk, std::chrono::seconds(kStallReportSeconds), pred))
            return acked_.load(std::memory_order_acquire) >= ticket;
        waited_s += kStallReportSeconds;
        stalls_.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[rendezvous] stalled: ticket %u acked %u (%u s) -- a pushed frame was not processed\n",
                     ticket, acked_.load(std::memory_order_acquire), waited_s);
        if (waited_s >= kAbandonSeconds) {
            abandoned_.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[rendezvous] ABANDONED ticket %u after %u s (defect: unmatched frame)\n", ticket,
                         waited_s);
            return false;
        }
    }
}
