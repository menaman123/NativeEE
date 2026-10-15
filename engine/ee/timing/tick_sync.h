#pragma once
// TickSync: EE-synchronous field ticks. Under the deterministic policy there
// is no ticker thread: a tick BODY executes only on a guest EE thread at a
// fixed point of the EE instruction stream, so the queue position of every
// tick-driven event (INTC VBON/TIM2/VBOF, alarms, timer settles) is a
// function of the guest's own execution, never of host wall latency.
//
// Causes:
//  * Boundary     -- exactly ONE tick per EE frame boundary (sequencer step 2);
//  * CdvdDelivery -- one tick per delivering disc pump pass (Phase 4 caller);
//  * Demand       -- one tick per non-consuming tick waiter (a timer busy-wait
//                    that polls the clock without reaching a boundary);
//  * Deferred     -- a demand noted from a NON-guest thread is never fired
//                    there: it is counted and fired at the next boundary.
// The body (what a tick does) is installed by the owner; this class is the
// policy object: one body per fire, single-EE-thread contract diagnostic
// (a concurrent body is counted, never waited for).
#include <atomic>
#include <cstdint>
#include <functional>

class TickSync {
public:
    enum class Cause : uint8_t { Boundary = 0, CdvdDelivery, Demand, Deferred, Count };
    using Body = std::function<uint64_t(Cause)>;  // fires one tick, returns its index

    static const char* CauseName(Cause c);

    void SetBody(Body body) { body_ = std::move(body); }
    bool HasBody() const { return static_cast<bool>(body_); }

    // Guest EE thread only: exactly one tick body now (0 when no body).
    uint64_t FireInline(Cause cause);
    // Any thread: note a demand that must become a tick at the next fixed point.
    void NotePending(Cause cause);
    // Guest EE thread: fire every noted tick (as Deferred). Returns the count.
    int FirePending();
    // Guest EE thread: fire at most ONE noted tick (as Deferred). Returns 0/1.
    int FirePendingOne();

    uint64_t Fired() const { return fired_.load(std::memory_order_relaxed); }
    uint64_t FiredByCause(Cause c) const;
    uint64_t Pending() const { return pending_.load(std::memory_order_acquire); }
    bool BodyActive() const { return in_body_.load(std::memory_order_acquire); }
    // Supervisor only, after all note producers and tick consumers stop.
    // Restore logical requests without resetting lifetime diagnostics.
    bool RestorePending(uint64_t pending);
    uint64_t Noted() const { return noted_.load(std::memory_order_relaxed); }
    uint64_t ConcurrentBodies() const { return concurrent_.load(std::memory_order_relaxed); }
    void ResetCounters();

private:
    Body body_;
    std::atomic<uint64_t> fired_{0};
    std::atomic<uint64_t> fired_by_cause_[static_cast<int>(Cause::Count)] = {};
    std::atomic<uint64_t> pending_{0};
    std::atomic<uint64_t> noted_{0};
    std::atomic<bool> in_body_{false};
    std::atomic<uint64_t> concurrent_{0};
};
