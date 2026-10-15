#include "app/host/session_workers.h"

#include <mutex>
#include <utility>

#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/event_flags.h"

namespace ps2r::app {
SessionWorkers::SessionWorkers(ThreadManager& threads, std::atomic<bool>& running,
                               std::function<void()> stop_clock, std::function<void()> wake_clock)
    : threads_(threads), running_(running), stop_clock_(std::move(stop_clock)), wake_clock_(std::move(wake_clock)) {}

void SessionWorkers::Cancel() {
    running_.store(false);
    threads_.TerminateAll();
    // These waits have their own condition variables. Token shutdown alone
    // cannot wake them to run their existing termination/unwind checks.
    for (const auto& item : Semaphores::Instance().SnapshotTable()) item.second->cv.notify_all();
    for (const auto& item : EventFlags::Instance().SnapshotTable()) item.second->cv.notify_all();
    if (wake_clock_) wake_clock_();
}

void SessionWorkers::Join() {
    if (stop_clock_) stop_clock_();
    Cancel();
    threads_.JoinAll();
}

size_t SessionWorkers::Join(uint64_t deadline_ms, std::string* report) {
    if (stop_clock_) stop_clock_();
    Cancel();
    return threads_.JoinAll(deadline_ms, report);
}

bool SessionWorkers::BoundaryAlive() const {
    const auto info = threads_.Lookup(boundary_tid_);
    if (!info) return false;
    std::lock_guard<std::mutex> lock(info->m);
    return info->started;
}
} // namespace ps2r::app
