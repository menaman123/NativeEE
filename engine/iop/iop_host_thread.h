// engine/iop/iop_host_thread.h -- IopHostThread: the ONE host thread that runs
// the cooperative IOP kernel under the REALTIME dispatch policy (play). It
// calls IopKernel::Dispatch() and then sleeps on a condition variable until
// either an EE-side wakeup (RPC command, injected sema signal, scheduled
// event) pokes it or the kernel's next deadline (DelayThread / deferred
// completion / heartbeat beat) arrives on the wall clock -- the OpenGOAL
// iop_runner loop shape. Under the FRAME policy this thread is never started:
// the EE thread dispatches at frame boundaries (IopModuleTable).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace ps2r::iop {

class IopKernel;

class IopHostThread {
public:
    explicit IopHostThread(IopKernel& kernel) : kernel_(kernel) {}
    ~IopHostThread() { Stop(); }
    IopHostThread(const IopHostThread&) = delete;
    IopHostThread& operator=(const IopHostThread&) = delete;

    void Start();
    void Stop();
    void Notify();   // an EE wakeup / injection arrived: leave the condvar wait
    bool Running() const { return running_.load(std::memory_order_acquire); }
    uint64_t Pokes() const { return pokes_.load(std::memory_order_acquire); }
    uint64_t Passes() const { return passes_.load(std::memory_order_acquire); }

private:
    void Loop();

    IopKernel& kernel_;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> pokes_{0};
    std::atomic<uint64_t> passes_{0};
};

}  // namespace ps2r::iop
