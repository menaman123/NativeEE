// engine/iop/iop_host_thread.cpp -- IopHostThread: dispatch, then condvar-sleep
// to the next kernel deadline or an EE poke (REALTIME policy only).
#include "engine/iop/iop_host_thread.h"

#include <chrono>

#include "engine/iop/iop_kernel.h"

namespace ps2r::iop {

void IopHostThread::Start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    thread_ = std::thread([this] { Loop(); });
}

void IopHostThread::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        if (thread_.joinable()) thread_.join();
        return;
    }
    Notify();
    if (thread_.joinable()) thread_.join();
}

void IopHostThread::Notify() {
    pokes_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lk(m_);
    cv_.notify_all();
}

void IopHostThread::Loop() {
    uint64_t seen_pokes = 0;
    while (running_.load(std::memory_order_acquire) && !kernel_.IsShutdown()) {
        // A pass is counted when the host thread ENTERS the kernel, not after
        // it returns: an EE submitter is released by its RPC ticket from inside
        // Dispatch(), so an after-the-fact increment would let an EE-side
        // observer see a completed command with Passes() still 0.
        passes_.fetch_add(1, std::memory_order_acq_rel);
        const IopKernel::DispatchResult r = kernel_.Dispatch();
        std::unique_lock<std::mutex> lk(m_);
        auto pred = [&] {
            return pokes_.load(std::memory_order_acquire) != seen_pokes ||
                   !running_.load(std::memory_order_acquire) || kernel_.IsShutdown();
        };
        if (r.next_due_us) {
            // Sleep until the kernel's next deadline or an EE poke.
            cv_.wait_until(lk, kernel_.Clock().DueToSteady(*r.next_due_us), pred);
        } else {
            // Nothing scheduled: sleep until poked (a 50 ms recheck bound
            // covers a wakeup lost during shutdown).
            cv_.wait_for(lk, std::chrono::milliseconds(50), pred);
        }
        seen_pokes = pokes_.load(std::memory_order_acquire);
    }
}

}  // namespace ps2r::iop
