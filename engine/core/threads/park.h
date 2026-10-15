// core/threads/park.h -- the ONLY blocking-wait primitive: a condition variable
// with absolute-deadline waits. Waiters park until notified or until the
// steady-clock deadline passes; there is no poll+sleep arm anywhere.
//
// Typical use: the waiter checks its predicate, calls WaitUntil(deadline), and
// re-checks. Notify() wakes every waiter (a generation counter makes a Notify
// that races ahead of the wait visible to the next WaitUntil).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace ps2r {

class Park {
public:
    // Steady clock in nanoseconds (the deadline domain of WaitUntil).
    static uint64_t NowNs();

    // Wake every parked waiter and advance the generation.
    void Notify();

    // Park until a Notify() newer than `seen_generation` arrives or the absolute
    // steady-clock deadline passes. Returns true when woken by a notification,
    // false on deadline. Pass Generation() taken BEFORE checking the predicate
    // to avoid the lost-wakeup window.
    bool WaitUntil(uint64_t deadline_ns, uint64_t seen_generation);
    // Convenience: relative timeout from now.
    bool WaitFor(uint64_t timeout_ns, uint64_t seen_generation);
    // Park with no deadline until a newer generation arrives.
    void Wait(uint64_t seen_generation);

    uint64_t Generation() const;

private:
    mutable std::mutex      mu_;
    std::condition_variable cv_;
    uint64_t                generation_ = 0;
};

}  // namespace ps2r
