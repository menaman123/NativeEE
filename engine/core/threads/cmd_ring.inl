// core/threads/cmd_ring.inl -- CmdRing template definitions (included at the
// end of cmd_ring.h).
#pragma once

namespace ps2r {

template <typename T, size_t N>
void CmdRing<T, N>::Push(const T& v) {
    std::lock_guard<std::mutex> lock(producer_mu_);
    uint64_t tail = tail_.load(std::memory_order_relaxed);
    uint64_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= N) {
        // Full: claim the head slot away from the consumer FIRST, then it is
        // safe to overwrite. A concurrent Pop() copying that slot fails its CAS
        // and discards the (possibly torn) copy. A failed CAS here means the
        // consumer just popped it -- room exists.
        if (head_.compare_exchange_strong(head, head + 1, std::memory_order_acq_rel)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    buf_[tail & (N - 1)] = v;
    tail_.store(tail + 1, std::memory_order_release);
}

template <typename T, size_t N>
bool CmdRing<T, N>::Pop(T& out) {
    for (;;) {
        uint64_t head = head_.load(std::memory_order_acquire);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = buf_[head & (N - 1)];
        // Claim the slot. Failure means a producer dropped this element
        // (overflow) while we copied it -- discard and retry.
        if (head_.compare_exchange_strong(head, head + 1, std::memory_order_acq_rel)) {
            return true;
        }
    }
}

template <typename T, size_t N>
uint64_t CmdRing<T, N>::SizeApprox() const {
    const uint64_t h = head_.load(std::memory_order_relaxed);
    const uint64_t t = tail_.load(std::memory_order_relaxed);
    return t > h ? t - h : 0;
}

}  // namespace ps2r
