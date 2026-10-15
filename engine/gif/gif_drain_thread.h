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
//   Start before any producer traffic; Stop after the last producer stopped, never from this thread.
#include <atomic>
#include <condition_variable>
#include <cstdint>
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
    // Savestate quiesce predicate: nonzero (gen+1) iff the thread is idle and
    // the arbiter holds nothing; two equal nonzero reads bracket a window in
    // which no pass ran. 1 when the thread is not running.
    uint64_t DrainIdleGeneration() const;

private:
    void Loop();

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
    std::thread::id tid_;
};
