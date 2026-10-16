// engine/iop/sif_rpc_bridge.cpp -- SifRpcBridge implementation.
#include "engine/iop/sif_rpc_bridge.h"

#include <chrono>
#include <cstdio>

#include "engine/iop/iop_kernel.h"

namespace ps2r::iop {

uint64_t SifRpcBridge::Submitted() const {
    std::lock_guard<std::mutex> lk(m_);
    return submitted_;
}

uint64_t SifRpcBridge::Completed() const {
    std::lock_guard<std::mutex> lk(m_);
    return completed_;
}

SifRpcBridge::Queue& SifRpcBridge::QueueFor(int tid) { return queues_[tid]; }

bool SifRpcBridge::RunOnRpcThread(std::function<void()> body) { return RunOnRpcThread(std::move(body), RpcTid()); }

bool SifRpcBridge::RunOnRpcThread(std::function<void()> body, int tid) {
    if (kernel_.OnKernelThread()) {
        body();   // a kernel-context caller runs inline
        return true;
    }
    uint64_t ticket = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        Queue& q = QueueFor(tid);
        q.bodies.push_back(std::move(body));
        ticket = ++q.submitted;
        ++submitted_;
    }
    if (tid > 0) kernel_.iWakeupThread(tid);
    if (host_wake_) host_wake_();

    auto done = [&] {
        std::lock_guard<std::mutex> lk(m_);
        return QueueFor(tid).completed >= ticket;
    };
    if (frame_drive_) {
        const bool completed = DriveUntil(done);
        if (completed && service_us_ > 0) {
            // The round trip's IOP time: the reply stands at issue + service on
            // the IOP's clock (a forward move only, as every advance).
            IopClock& clock = kernel_.Clock();
            clock.AdvanceToUs(clock.NowUs() + service_us_);
            charged_us_.fetch_add(service_us_, std::memory_order_acq_rel);
        }
        return completed;
    }

    // REALTIME: the IOP host thread executes the command; wait for the
    // completion ticket (bounded -- a wedged module must not hang the EE).
    // Between checks, service any worker marshal parked in Wait::Rpc: the EE
    // cannot reach its boundary slot while it is here.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(m_);
            if (cv_.wait_for(lk, std::chrono::milliseconds(2),
                             [&] { return QueueFor(tid).completed >= ticket || kernel_.IsShutdown(); })) {
                return QueueFor(tid).completed >= ticket;   // false = shutdown
            }
        }
        if (ee_service_poll_) ee_service_poll_();
        if (std::chrono::steady_clock::now() >= deadline) break;
    }
    stalls_.fetch_add(1, std::memory_order_acq_rel);
    std::fprintf(stderr, "iop: RPC completion stall (ticket=%llu done=%llu)\n",
                 (unsigned long long)ticket, (unsigned long long)Completed());
    return false;
}

bool SifRpcBridge::DriveUntil(const std::function<bool()>& pred) {
    // FRAME policy: the EE thread drives the kernel until the predicate holds,
    // force-advancing the virtual clock to the next deadline when every thread
    // is time-parked (discrete-event skip) and servicing any Wait::Rpc marshal
    // itself (it IS the EE). Bounded.
    for (int iter = 0; iter < 20000; ++iter) {
        const IopKernel::DispatchResult r = kernel_.Dispatch();
        if (pred()) return true;
        if (kernel_.IsShutdown()) return false;
        if (ee_service_poll_) ee_service_poll_();
        if (r.next_due_us) {
            kernel_.Clock().AdvanceToUs(*r.next_due_us);
        } else if (!r.ran_something) {
            break;   // idle, deadline-free, predicate false: cannot progress
        }
    }
    stalls_.fetch_add(1, std::memory_order_acq_rel);
    std::fprintf(stderr, "iop: FRAME drive stalled without completion\n");
    return pred();
}

void SifRpcBridge::RpcServiceLoop() {
    // Runs on the module's RPC-dispatch coroutine. Commands arrive as banked
    // wakeups, so a wake posted mid-body is consumed by the next SleepThread --
    // no lost-command window. The queue drained is the running thread's own;
    // a body submitted to the default tid (0: no queue bound yet) is also
    // served by whichever RPC thread wakes.
    const int self = kernel_.CurrentTid();
    for (;;) {
        kernel_.SleepThread();
        for (;;) {
            std::function<void()> body;
            int from = self;
            {
                std::lock_guard<std::mutex> lk(m_);
                Queue* q = &QueueFor(self);
                if (q->bodies.empty() && self != RpcTid()) {
                    q = &QueueFor(RpcTid());
                    from = RpcTid();
                }
                if (q->bodies.empty()) break;
                body = std::move(q->bodies.front());
                q->bodies.pop_front();
            }
            if (body) body();
            {
                std::lock_guard<std::mutex> lk(m_);
                QueueFor(from).completed++;
                completed_++;
            }
            cv_.notify_all();
        }
        if (kernel_.IsShutdown()) return;
    }
}

bool SifRpcBridge::WaitEeReplyUs(uint64_t timeout_us) {
    ee_wait_tid_.store(kernel_.CurrentTid(), std::memory_order_release);
    const bool woken = kernel_.WaitRpcUs(timeout_us);
    ee_wait_tid_.store(0, std::memory_order_release);
    return woken;
}

void SifRpcBridge::NotifyEeReply() {
    const int tid = ee_wait_tid_.load(std::memory_order_acquire);
    if (tid > 0) {
        kernel_.iWakeupIfRpcWait(tid);   // a parkee past its deadline is no longer in Wait::Rpc: dropped
        if (host_wake_) host_wake_();
    }
}

}  // namespace ps2r::iop
