#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "app/host/session_checkpoint.h"
#include "engine/ee/savestate/thread_relauncher.h"

class ThreadManager;

namespace ps2r::app {

// Preflight on the game owner; restart on the supervisor after guest unwind.
// The root owns stopping, deadlines, SDL, provider selection and all resets.
// This object and its relauncher must outlive every worker they start.
class SessionRestart {
public:
    enum class Missing { Preserve, Reconstruct };
    enum class Clock { Unspecified, RefuseRewind, Rebase };
    enum class Recording { Unspecified, Disabled, NewSegment };
    struct Plan {
        // Exact root-selected provider/version identity, stable across restart.
        std::string providers;
        std::vector<uint32_t> required_sections;
        // Explicit legacy compatibility only. Selected service state belongs
        // in required_sections and cannot be waived by either missing rule.
        std::vector<std::pair<uint32_t, Missing>> optional_missing;
        // Subset of the root's five frozen unowned tags: FILE/CNTR/MISC/STRM/SIFS.
        std::vector<uint32_t> legacy_unowned;
        Clock clock = Clock::Unspecified;
        Recording recording = Recording::Unspecified;
    };
    struct Current {
        Plan plan;
        // The completed game key must remain frozen from accepted preflight
        // through join. Wall time is not this logical frame.
        uint64_t frame = 0;
        bool recording = false;
    };
    struct Epoch {
        uint64_t previous_frame = 0;
        uint32_t restored_frame = 0;
        Clock clock = Clock::Unspecified;
        Recording recording = Recording::Unspecified;
    };
    struct Sources {
        std::function<bool()> on_game_owner;
        std::function<bool()> on_supervisor;
        // Root's atomic, monotonic window/session close latch. Read on both
        // owners; close wins over restart, including during joins/restores.
        std::function<bool()> closing;
        std::function<Current()> current;
        SessionCheckpoint::EntryCheck can_resume;
        std::function<bool(uint32_t function, int wait_type)> can_restart_wait;
        // Pure checks: no stop, join, drain, reset, file write or guest calls.
        std::function<bool(const Epoch&, std::string&)> validate_epoch;
        std::function<bool(std::string&)> current_quiescent;
        // Root metadata/clock/producer checks over the original retained
        // image. Required; must not mutate any live owner.
        std::function<bool(const savestate::FileMeta&, const std::vector<savestate::Blob>&,
                           std::string&)> validate_image;
    };
    struct Hooks {
        // Supervisor only, after the root stops admission and requests unwind.
        // Flush/join workers AND writers; publish any preceding save result.
        std::function<bool(std::string&)> join_workers_and_writers;
        // Rebase counters/clocks and, when selected, start a NEW recorder
        // segment. Never append backward frames to the previous segment.
        std::function<bool(const Epoch&, std::string&)> begin_epoch;
        // Bind StateRestorer::RestoreImage to these OWNED bytes; never reread
        // the submitted path. Its strict owner policy must match Plan.
        std::function<bool(const std::vector<uint8_t>&, std::string&)> apply_image;
        // GameLibraries, MovieStream, GameBoundary, input edges and derived
        // caches before relaunch. Must not roll back persistent game saves.
        std::function<bool(std::string&)> reset_after_restore;
        // Fatal restart failure: nonblocking cancel/wake, including object
        // waits. Root keeps the SDL deadline active and joins before teardown.
        std::function<void()> cancel_workers;
        // Stage unpublished selected resources AFTER pure validation but
        // BEFORE root unwind. Bind with discard as a pair. apply_image must
        // consume this exact preparation without repeating its allocations.
        std::function<bool(const std::vector<uint8_t>&, std::string&)> prepare_image;
        std::function<void()> discard_prepared;
    };
    struct Result {
        uint64_t request = 0;
        SessionControls::Phase phase = SessionControls::Phase::Refused;
        std::string detail;
        uint32_t frame = 0;
        bool requires_close = false;
    };
    static constexpr size_t kMaxImageBytes = (256u << 20) + (1u << 20);

    SessionRestart(ThreadManager&, const savestate::SectionRegistry&, Sources,
                   Hooks, ThreadRelauncher::EnterFn);
    SessionRestart(const SessionRestart&) = delete;
    SessionRestart& operator=(const SessionRestart&) = delete;

    // Working means a complete immutable image is ready; it is not a restored
    // session. Root may now stop/wake owners and unwind the boundary caller.
    // Any refusal/failure here leaves all live owners and game saves alone.
    Result PrepareFile(uint64_t request, const std::string& path);
    Result PrepareImage(uint64_t request, std::vector<uint8_t> image);
    bool Pending() const;
    // Game owner, only before stopping. A prepared request cannot be replaced.
    bool DiscardPrepared(uint64_t request);
    // No game owner may call this. Root must have requested stop/unwind; this
    // joins, checks the actual ThreadManager restart contract, then restores
    // and relaunches once. Failure requires session close, never partial play.
    Result RestartPrepared();

private:
    struct Image;
    bool BeginPrepare(uint64_t request, Result& result);
    Result Prepare(uint64_t request, std::vector<uint8_t> bytes);
    Result Reject(Result result, SessionControls::Phase phase, std::string detail);
    bool Validate(Image&, std::string&) const;
    bool ValidateThreads(const SessionCheckpoint::Records&, std::string&) const;
    bool SameOwners(const Image&) const;
    bool SameCurrent(const Image&, bool restored = false) const;
    static bool SamePlan(const Plan&, const Plan&);
    Result RestartFailure(Result, std::string);

    ThreadManager& threads_;
    const savestate::SectionRegistry& sections_;
    Sources sources_;
    Hooks hooks_;
    const bool enter_bound_;
    ThreadRelauncher relauncher_;
    mutable std::mutex mutex_;
    bool preparing_ = false, restarting_ = false;
    std::shared_ptr<const Image> prepared_;
    // ThreadRelauncher keeps references to records. Retain the preceding image
    // until its workers have actually joined, including a partial launch.
    std::shared_ptr<const Image> active_;
};

}  // namespace ps2r::app
