#pragma once

#include <functional>
#include <string>

#include "engine/ee/savestate/thread_capture.h"

class BoundarySequencer;
class ThreadManager;

// Game-state reproduction owner. Knows no graphics/service hardware model.
// Construct before workers, destroy after join. Completion keeps the game
// token and caller chain live while the product consumes queued actions.
class GameBoundary {
public:
    using Consumer = std::function<void(uint64_t frame)>;
    GameBoundary(ThreadManager& threads, BoundarySequencer& sequence);
    ~GameBoundary();
    GameBoundary(const GameBoundary&) = delete;
    GameBoundary& operator=(const GameBoundary&) = delete;
    bool Attached() const;
    void SetConsumer(Consumer consumer);
    // Used directly by SessionCheckpoint::Sources::fresh_threads.
    // Only the completed callback's owning thread can see its context.
    bool FreshThreads(ThreadCapture::Records& out, std::string& error);
    // Supervisor after old workers join, before any restored guest resumes.
    void ResetAfterRestore();

private:
    void Completed(CpuContext& ctx, uint64_t frame);
    ThreadManager& threads_;
    BoundarySequencer& sequence_;
    ThreadCapture capture_;
    Consumer consumer_;
    bool attached_ = false;
};
