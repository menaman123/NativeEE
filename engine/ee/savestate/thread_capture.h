#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/ee/savestate/thread_section.h"

class ThreadManager;
struct ThreadInfo;

// Product continuation capture. No host stacks, foreign TLS reads or restored
// context fallback. Observe parks on their owner; publish immutable copies.
class ThreadCapture {
public:
    using Records = std::vector<ThreadSection::Record>;
    using Chain = std::vector<ThreadSection::ChainFrame>;
    struct Proofs {
        std::function<bool(uint32_t function, uint32_t entry)> can_resume;
        // Calling thread only: includes generated overflow/omitted-frame proof.
        std::function<bool()> stack_complete;
        // A parked body may be restarted only if doing so repeats no completed
        // side effects. Exact entry support alone does not establish this.
        std::function<bool(uint32_t function, int wait_type)> can_restart_wait;
    };

    ThreadCapture(ThreadManager& threads, Proofs proofs);
    ~ThreadCapture();
    bool Attached() const { return attached_; }
    ThreadCapture(const ThreadCapture&) = delete;
    ThreadCapture& operator=(const ThreadCapture&) = delete;

    // Game token owner AFTER boundary return registers/accounting are final.
    // All returned bytes are owned. False clears out and names the missing
    // proof; it never produces a partial state.
    bool CaptureBoundary(int tid, const CpuContext& completed_context, Records& out, std::string& error);
    // After old workers joined, before new workers start.
    void ResetAfterRestore();

    // WalkChain installs this on its own thread for each InvokeAt. Carries
    // outer frames that are logically live but absent from generated TLS.
    class ResumeScope {
    public:
        ResumeScope(const Chain& saved, size_t level);
        ~ResumeScope();
        ResumeScope(const ResumeScope&) = delete;
        ResumeScope& operator=(const ResumeScope&) = delete;
    private:
        friend class ThreadCapture;
        const Chain& saved_;
        size_t level_;
        ResumeScope* previous_;
    };
    static bool ExpandResumeChain(const Chain& active, const Chain& saved, size_t level,
                                  Chain& complete, std::string& error);

private:
    struct Parked {
        std::weak_ptr<ThreadInfo> identity;
        uint64_t incarnation = 0, revision = 0;
        ThreadSection::Record record;
        std::string error;
    };
    void Observe(int tid, const std::shared_ptr<ThreadInfo>& info, bool parked);
    bool ReadLocalChain(Chain& out, std::string& error) const;
    bool ValidateEntries(const ThreadSection::Record& record, std::string& error) const;
    bool CollectBoundary(int tid, const CpuContext& context, Records& out, std::string& error);

    ThreadManager& threads_;
    Proofs proofs_;
    bool attached_ = false;
    std::mutex mutex_;
    std::map<int, std::shared_ptr<const Parked>> parked_;
};
