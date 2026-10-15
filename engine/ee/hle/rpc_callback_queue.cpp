// RpcCallbackQueue implementation.
#include "engine/ee/hle/rpc_callback_queue.h"

#include <chrono>

RpcCallbackQueue& RpcCallbackQueue::Instance() {
    static RpcCallbackQueue q;
    return q;
}

void RpcCallbackQueue::Push(uint32_t client_data_addr) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_.push_back(client_data_addr);
    }
    cv_.notify_one();
}

std::vector<uint32_t> RpcCallbackQueue::Drain() {
    std::vector<uint32_t> out;
    std::lock_guard<std::mutex> lk(mu_);
    out.swap(pending_);
    return out;
}

bool RpcCallbackQueue::Empty() {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_.empty();
}

bool RpcCallbackQueue::WaitUntilNonEmpty(uint64_t deadline_ns) {
    std::unique_lock<std::mutex> lk(mu_);
    const auto deadline = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline_ns));
    return cv_.wait_until(lk, deadline, [&] { return !pending_.empty(); });
}
