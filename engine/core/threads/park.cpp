// core/threads/park.cpp -- see park.h.
#include "engine/core/threads/park.h"

#include <chrono>

namespace ps2r {

uint64_t Park::NowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Park::Notify() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        generation_++;
    }
    cv_.notify_all();
}

bool Park::WaitUntil(uint64_t deadline_ns, uint64_t seen_generation) {
    std::unique_lock<std::mutex> lk(mu_);
    // duration_cast keeps this portable to clocks whose period is not 1 ns.
    const auto deadline = std::chrono::steady_clock::time_point(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(deadline_ns)));
    return cv_.wait_until(lk, deadline, [&] { return generation_ != seen_generation; });
}

bool Park::WaitFor(uint64_t timeout_ns, uint64_t seen_generation) {
    return WaitUntil(NowNs() + timeout_ns, seen_generation);
}

void Park::Wait(uint64_t seen_generation) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return generation_ != seen_generation; });
}

uint64_t Park::Generation() const {
    std::lock_guard<std::mutex> lk(mu_);
    return generation_;
}

}  // namespace ps2r
