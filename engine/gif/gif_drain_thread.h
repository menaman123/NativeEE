#pragma once
// The GIF drain thread: while it runs it OWNS the arbiter's drain (the
// single-drain-owner envelope moves here), so the parser advances off the EE
// thread and every other thread's drain() becomes a kick. Started only by an
// explicit Start() from the composition root (under MTVU); with MTVU off the
// thread does not exist and every drain stays inline on the EE thread.
//
// Cross-thread contract:
//   kick_, stop_     under mtx_; cv_ wakes the loop, done_cv_ wakes fence waiters
//   pass_gen_        completed passes (empty ones included), bumped under mtx_, read acquire anywhere
//   running_, working_   atomics: thread up / a pass mid-flight
//   tid_             written by Start before running_ is published; read by OnDrainThread
//   busy_ns_, passes_   relaxed ledger of the loop (the thread's own time inside DrainPass)
//   fault_           under mtx_: an exception out of a pass (the parser / the DrawIR chain)
//                    is caught on this thread, which then idles; TakeFault hands it to the
//                    producer side, which rethrows it where the inline drain would have thrown
//   Start before any producer traffic; Stop after the last producer stopped, never from this thread.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>

class GifArbiter;

class GifDrainThread {
public:
    explicit GifDrainThread(GifArbiter& arbiter);
    // Stops and joins if still running.
    ~GifDrainThread();
    GifDrainThread(const GifDrainThread&) = delete;
    GifDrainThread& operator=(const GifDrainThread&) = delete;

    // Spawn the thread and attach it to the arbiter. `on_start` runs on the
    // new thread before the loop (the host names / pins the thread there).
    void Start(void (*on_start)(void* user) = nullptr, void* user = nullptr);
    // Request one final pass (delivers whatever is ready), join, detach from
    // the arbiter. Never hangs on a non-empty inbox.
    void Stop();

    bool Running() const;
    bool OnDrainThread() const;
    // A pass is mid-flight.
    bool Working() const;
    // Request one drain pass.
    void Notify();
    // Completed passes.
    uint64_t PassGeneration() const;
    // Returns once a pass that STARTED after this call has completed (pass
    // mark+1 may be mid-flight at mark time; mark+2 began strictly after).
    // Immediate when the thread is not running or when called from it.
    void DrainFence();
    // Parks until a pass completed after `seen` (a PassGeneration read) or
    // `timeout_ns` elapsed -- the EE's idle wait on the drain thread's progress
    // (the FINISH it will raise). Immediate when not running / from this thread.
    void WaitPassAfter(uint64_t seen, uint64_t timeout_ns);
    // Savestate quiesce predicate: nonzero (gen+1) iff the thread is idle and
    // the arbiter holds nothing; two equal nonzero reads bracket a window in
    // which no pass ran. 1 when the thread is not running.
    uint64_t DrainIdleGeneration() const;

    // The loop's ledger: completed passes that delivered work and the wall time
    // spent inside DrainPass (ns), the drain thread's busy time.
    uint64_t BusyNs() const { return busy_ns_.load(std::memory_order_relaxed); }
    uint64_t Passes() const { return passes_.load(std::memory_order_relaxed); }
    // A pass threw: true once the fault is pending (the loop keeps running; later
    // passes are refused until the fault is taken). TakeFault returns the
    // exception once and clears it (null = none).
    bool Faulted() const { return faulted_.load(std::memory_order_acquire); }
    std::exception_ptr TakeFault();

private:
    void Loop();
    void RunPass();

    GifArbiter& arbiter_;
    std::thread thread_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    bool kick_ = false;
    bool stop_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> working_{false};
    std::atomic<uint64_t> pass_gen_{0};
    std::atomic<uint64_t> busy_ns_{0};
    std::atomic<uint64_t> passes_{0};
    std::atomic<bool> faulted_{false};
    std::exception_ptr fault_;
    std::thread::id tid_;
};
