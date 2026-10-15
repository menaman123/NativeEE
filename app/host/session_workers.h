#pragma once

#include <atomic>
#include <functional>
#include <string>

class ThreadManager;
namespace ps2r::app {

// Root lifecycle for the one game-owner family. Cancellation never joins;
// supervisor joins keep SDL/main available for input and deadlines.
class SessionWorkers {
public:
    SessionWorkers(ThreadManager& threads, std::atomic<bool>& running,
                   std::function<void()> stop_clock, std::function<void()> wake_clock);
    void Cancel();
    void Join();
    // The close's bounded join: Cancel, then ThreadManager::JoinAll(deadline_ms)
    // -- a guest worker still parked past the deadline is abandoned and named in
    // *report (the census `guest_join_abandoned=`). Returns the count abandoned.
    size_t Join(uint64_t deadline_ms, std::string* report);
    void SetBoundary(int tid) { boundary_tid_ = tid; }
    bool BoundaryAlive() const;

private:
    ThreadManager& threads_;
    std::atomic<bool>& running_;
    std::function<void()> stop_clock_, wake_clock_;
    int boundary_tid_ = 1; // changed on supervisor only, before relaunch
};
} // namespace ps2r::app
