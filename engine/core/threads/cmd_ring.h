// core/threads/cmd_ring.h -- bounded command ring, multiple producers / one
// consumer, drop-OLDEST on overflow.
//
// Contract (pinned by tests/core/test_threads_log.cpp):
//   * The CONSUMER never takes a lock and never waits: Pop() is a bounded CAS
//     loop that only retries when a producer concurrently drops the oldest
//     element (rare, bounded by the producer count).
//   * PRODUCERS serialise among themselves with a producer-only mutex the
//     consumer never touches, so a real-time consumer (audio callback) is never
//     blocked by a stalled producer and producers never wait on the consumer.
//   * Overflow: drop-oldest (the ring claims the head slot before overwriting
//     it, so a concurrent Pop of that slot fails its CAS and retries -- no torn
//     read is ever observable) plus a dropped counter.
// Elements must be trivially copyable PODs; N is a power of two.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>

namespace ps2r {

template <typename T, size_t N>
class CmdRing {
    static_assert(std::is_trivially_copyable<T>::value, "ring elements must be trivially copyable");
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    // Producer side. Never fails; when full, drops the oldest element.
    void Push(const T& v);
    // Consumer side (single consumer). Returns false when empty.
    bool Pop(T& out);

    uint64_t Dropped() const { return dropped_.load(std::memory_order_relaxed); }
    uint64_t SizeApprox() const;

private:
    std::array<T, N>      buf_{};
    std::atomic<uint64_t> head_{0};   // next slot to read
    std::atomic<uint64_t> tail_{0};   // next slot to write
    std::atomic<uint64_t> dropped_{0};
    std::mutex            producer_mu_;
};

}  // namespace ps2r

#include "engine/core/threads/cmd_ring.inl"
