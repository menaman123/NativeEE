// app/host/shutdown_census.h -- the session's shutdown, step by step: how long
// the close took from the request to the last join, and each named stop/join
// (the game thread's unwind, the guest workers' JoinAll, the savestate writer,
// the MTVU worker, Graphics' quiescence, the GS consumer, the recorder) in
// milliseconds. run.meta `shutdown=` / `shutdown_ms=` (the user's "shutdown
// exceeded 10 seconds" is attributed by the step that was IN PROGRESS when the
// watchdog fired: `Current()` names it; the watchdog prints it). Thread-safe:
// the steps run on the game worker, the watchdog reads on main.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::app {

class ShutdownCensus {
public:
    using Clock = std::chrono::steady_clock;

    // The close request (main: the first iteration that saw closing_).
    void Begin();
    bool Begun() const { return begun_.load(std::memory_order_acquire); }
    // One timed step: Enter names it as current; Leave books its duration.
    void Enter(const char* step);
    void Leave();
    // RAII form for the stopped() ladder.
    class Step {
    public:
        Step(ShutdownCensus& census, const char* name) : census_(census) { census_.Enter(name); }
        ~Step() { census_.Leave(); }
        Step(const Step&) = delete;
        Step& operator=(const Step&) = delete;
    private:
        ShutdownCensus& census_;
    };
    // The last join finished (main: the worker joined).
    void End();
    // A named fact about the close (rendered as ",<key>=<value>"): the abandoned
    // guest workers of a bounded join, the exit route taken.
    void Note(const std::string& key, const std::string& value);
    bool HasNote(const std::string& key) const;

    // The step in progress ("" when none) and its elapsed ms -- the watchdog's attribution.
    std::string Current(uint64_t* elapsed_ms) const;
    // Total close -> end in ms (0 until End; the watchdog reads ElapsedMs instead).
    uint64_t TotalMs() const { return total_ms_.load(std::memory_order_acquire); }
    uint64_t ElapsedMs() const;
    // "total=<ms>,<step>=<ms>,...[,stuck=<step>]" -- the run.meta shutdown= body.
    std::string Render() const;

private:
    Clock::time_point StepStart() const;   // under mutex_
    std::atomic<bool> begun_{false};
    std::atomic<uint64_t> total_ms_{0};
    Clock::time_point began_{};
    mutable std::mutex mutex_;
    std::string current_;
    Clock::time_point current_started_{};
    std::vector<std::pair<std::string, uint64_t>> steps_;
    std::vector<std::pair<std::string, std::string>> notes_;
};

}  // namespace ps2r::app
