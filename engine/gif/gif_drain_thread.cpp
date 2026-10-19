#include "engine/gif/gif_drain_thread.h"

#include <chrono>

#include "engine/gif/gif_arbiter.h"

GifDrainThread::GifDrainThread(GifArbiter& arbiter) : arbiter_(arbiter) {}

GifDrainThread::~GifDrainThread() { Stop(); }

void GifDrainThread::Start(void (*on_start)(void* user), void* user) {
    if (running_.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = false;
        kick_ = false;
    }
    arbiter_.AttachDrainThread(this);
    thread_ = std::thread([this, on_start, user] {
        if (on_start) on_start(user);
        Loop();
    });
    tid_ = thread_.get_id();
    running_.store(true, std::memory_order_release);
}

void GifDrainThread::Stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
        kick_ = true;
    }
    cv_.notify_one();
    thread_.join();
    running_.store(false, std::memory_order_release);
    arbiter_.AttachDrainThread(nullptr);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = false;
        kick_ = false;
    }
    done_cv_.notify_all();
}

bool GifDrainThread::Running() const { return running_.load(std::memory_order_acquire); }

bool GifDrainThread::OnDrainThread() const {
    return running_.load(std::memory_order_acquire) && std::this_thread::get_id() == tid_;
}

bool GifDrainThread::Working() const { return working_.load(std::memory_order_acquire); }

void GifDrainThread::Notify() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        kick_ = true;
    }
    cv_.notify_one();
}

uint64_t GifDrainThread::PassGeneration() const { return pass_gen_.load(std::memory_order_acquire); }

void GifDrainThread::DrainFence() {
    if (!running_.load(std::memory_order_acquire)) return;
    if (OnDrainThread()) return;  // a self-fence would deadlock
    std::unique_lock<std::mutex> lock(mtx_);
    const uint64_t mark = pass_gen_.load(std::memory_order_relaxed);
    while (pass_gen_.load(std::memory_order_relaxed) < mark + 2) {
        if (stop_ || !running_.load(std::memory_order_acquire)) return;
        kick_ = true;
        cv_.notify_one();
        done_cv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

void GifDrainThread::WaitPassAfter(uint64_t seen, uint64_t timeout_ns) {
    if (!running_.load(std::memory_order_acquire) || OnDrainThread()) return;
    std::unique_lock<std::mutex> lock(mtx_);
    done_cv_.wait_for(lock, std::chrono::nanoseconds(timeout_ns), [this, seen] {
        return pass_gen_.load(std::memory_order_relaxed) != seen || stop_ || !running_.load(std::memory_order_acquire);
    });
}

uint64_t GifDrainThread::DrainIdleGeneration() const {
    if (!running_.load(std::memory_order_acquire)) return 1;
    if (working_.load(std::memory_order_acquire)) return 0;
    if (arbiter_.PendingSlots() != 0 || arbiter_.PendingBytes() != 0) return 0;
    return pass_gen_.load(std::memory_order_acquire) + 1;
}

std::exception_ptr GifDrainThread::TakeFault() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::exception_ptr e = fault_;
    fault_ = nullptr;
    faulted_.store(false, std::memory_order_release);
    return e;
}

// The pass body with its ledger and the fault catch: a throw out of the parser
// or the DrawIR chain on this thread is kept for the producer (the inline drain
// would have thrown on the EE); the arbiter's queue keeps what the pass had not
// delivered, and no further pass runs until the fault was taken.
void GifDrainThread::RunPass() {
    if (faulted_.load(std::memory_order_acquire)) return;
    const auto t0 = std::chrono::steady_clock::now();
    bool delivered = false;
    try {
        delivered = arbiter_.DrainPassDelivered();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mtx_);
        fault_ = std::current_exception();
        faulted_.store(true, std::memory_order_release);
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (delivered) {
        busy_ns_.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()),
                           std::memory_order_relaxed);
        passes_.fetch_add(1, std::memory_order_relaxed);
    }
}

// One pass per kick; the stop request runs one final pass and exits.
void GifDrainThread::Loop() {
    for (;;) {
        bool stopping;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return kick_ || stop_; });
            kick_ = false;
            stopping = stop_;
        }
        working_.store(true, std::memory_order_release);
        RunPass();
        working_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mtx_);
            pass_gen_.fetch_add(1, std::memory_order_acq_rel);
        }
        done_cv_.notify_all();
        if (stopping) break;
    }
}
