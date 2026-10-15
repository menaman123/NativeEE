#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/host/session_controls.h"
#include "engine/core/savestate/section_registry.h"
#include "engine/core/savestate/savestate_writer.h"
#include "engine/ee/savestate/thread_section.h"

namespace ps2r::app {

// Captures owned service bytes on the game owner, then writes them off-thread.
// The continuation source must harvest CURRENT logical contexts/chains. This
// class never falls back to ThreadSection's historical restored records.
class SessionCheckpoint {
public:
    using Records = std::vector<ThreadSection::Record>;
    using EntryCheck = std::function<bool(uint32_t function, uint32_t entry)>;
    struct Sources {
        std::function<bool()> on_game_owner;
        std::function<bool(std::string&)> quiescent;
        // Same capture boundary/epoch for every record. The provider proves
        // thread incarnation, peer stability, nontruncated owning-thread TLS,
        // complete deferred resume tails and reproducible wait completion.
        // SnapshotTable's shared pointers alone provide none of that proof.
        std::function<bool(Records&, std::string&)> fresh_threads;
        // Actual installed body's exact continuation support, not containment
        // alone or DispatchTable::IsHooked (classic generated bodies use it).
        EntryCheck can_resume;
    };
    struct Result {
        uint64_t request = 0;
        SessionControls::Phase phase = SessionControls::Phase::Failed;
        std::string path;
        std::string detail;
        uint32_t frame = 0;
    };

    SessionCheckpoint(const savestate::SectionRegistry& sections, Sources sources);
    // Game owner, after the boundary service's return registers are finalized.
    // Working means owned bytes were queued; only Poll can report success.
    Result Capture(uint64_t request, std::string path, savestate::FileMeta meta);
    // Any host owner. A completion is returned exactly once, after atomic file
    // publication succeeds or fails. Never waits for the disk or game owner.
    std::optional<Result> Poll();
    // Supervisor, after capture admission has stopped; covered by root's exit
    // deadline. Drain, then Poll before cancelling outstanding UI requests.
    void Join();
    bool Busy() const;

    // Structural and generated-entry proof, also usable before restore
    // teardown. The application's strict section-owner preflight is separate.
    static bool ValidateContinuations(const Records& records, const EntryCheck& can_resume,
                                      std::string& error);
    // A resumed call's generated wrapper records an internal resume RA. Restore
    // its original caller RA and append the outer frames still in WalkChain.
    static bool ExpandResumeChain(const std::vector<ThreadSection::ChainFrame>& active,
                                  const std::vector<ThreadSection::ChainFrame>& saved,
                                  size_t level, std::vector<ThreadSection::ChainFrame>& complete,
                                  std::string& error);

private:
    Result Refuse(Result result, SessionControls::Phase phase, std::string detail);

    const savestate::SectionRegistry& sections_;
    Sources sources_;
    savestate::SavestateWriter writer_;
    mutable std::mutex mutex_;
    std::optional<Result> active_;
    uint64_t expected_completion_ = 0;
};

}  // namespace ps2r::app
