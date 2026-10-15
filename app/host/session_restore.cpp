#include "app/host/session_restore.h"

#include <stdexcept>
#include <utility>

#include "app/host/session_files.h"
#include "app/host/session_workers.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/savestate/state_restorer.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/timing/game_boundary.h"

namespace ps2r::app {
SessionRestore::SessionRestore(SessionRuntime& session, SessionWorkers& workers,
    StateRestorer& restorer, SessionClock& clock, GameBoundary& boundary, verify::VerifyHost& output,
    SessionFiles& files, Lifecycle lifecycle)
    : session_(session), workers_(workers), restorer_(restorer), clock_(clock),
      boundary_(boundary), output_(output), files_(files), lifecycle_(std::move(lifecycle)),
      restart_(ThreadManager::Instance(), restorer.Registry(), Sources(), RestartHooks(),
          [this](uint32_t function, uint32_t entry, CpuContext& context) {
              // A paused restore stays paused before its first instruction.
              // This gate has no freshly completed caller stack: an immediate
              // F5 explicitly refuses until a real completed boundary occurs.
              if (ThreadManager::Instance().CurrentTid() == incoming_boundary_ && resume_gate_.exchange(false))
                  if (!session_.Boundary(incoming_clock_.domain.boundaries)) throw ThreadExitUnwind{};
              return DispatchTable::InvokeAt(function, entry, context);
          }) {}

SessionRestart::Current SessionRestore::Current() const {
    const auto clock = clock_.Capture();
    SessionRestart::Current current;
    current.frame = clock.domain.boundaries;
    current.recording = output_.RecordingInput();
    current.plan.providers = clock.providers;
    current.plan.clock = SessionRestart::Clock::Rebase;
    current.plan.recording = current.recording ? SessionRestart::Recording::NewSegment :
                                              SessionRestart::Recording::Disabled;
    // F8 consumes complete NEW captures. Historical CLI migration remains an
    // explicitly separate boot operation; no absent/reset fallback is inferred.
    for (const auto& owner : restorer_.Registry().Specs())
        current.plan.required_sections.push_back(owner.tag);
    return current;
}

bool SessionRestore::ReadImage(const savestate::FileMeta& meta,
    const std::vector<savestate::Blob>& blobs, std::string& error) {
    if (!clock_.ValidateImage(meta, blobs, error)) return false;
    for (const auto& blob : blobs) {
        if (blob.tag == SessionClock::kTag && !SessionClock::Decode(blob, incoming_clock_, error)) return false;
        if (blob.tag != savestate::TAG_THRD) continue;
        SessionCheckpoint::Records records;
        if (!ThreadSection::Parse(blob.payload, records, &error)) return false;
        incoming_boundary_ = 0;
        for (const auto& record : records) if (record.is_boundary) {
            if (incoming_boundary_) { error = "Reproduction has multiple boundary owners."; return false; }
            incoming_boundary_ = record.tid;
        }
    }
    if (!incoming_boundary_) { error = "Reproduction has no boundary owner."; return false; }
    return true;
}

SessionRestart::Sources SessionRestore::Sources() {
    SessionRestart::Sources sources;
    sources.on_game_owner = [] { return EeRunToken::Instance().CanCaptureCurrentThread(); };
    sources.on_supervisor = [] { return ThreadManager::Instance().CurrentIncarnation() == 0; };
    sources.closing = [this] { return session_.Closing(); };
    sources.current = [this] { return Current(); };
    sources.can_resume = DispatchTable::CanResumeAt;
    sources.can_restart_wait = DispatchTable::CanRestartWait;
    sources.validate_epoch = [this](const auto&, std::string& error) {
        if (output_.OutputFailed()) { error = output_.OutputError(); return false; }
        if (output_.RecordingInput() && incoming_clock_.domain.tick > UINT32_MAX) {
            error = "Restored input tick exceeds the recording format."; return false;
        }
        return true;
    };
    sources.current_quiescent = [this](std::string& error) {
        return lifecycle_.inspect && lifecycle_.inspect(error);
    };
    sources.validate_image = [this](const auto& meta, const auto& blobs, auto& error) {
        return ReadImage(meta, blobs, error);
    };
    return sources;
}

SessionRestart::Hooks SessionRestore::RestartHooks() {
    SessionRestart::Hooks hooks;
    hooks.prepare_image = [this](const auto& bytes, auto& error) {
        return lifecycle_.prepare(error) && restorer_.PrepareImage(bytes.data(), bytes.size(), &error);
    };
    hooks.discard_prepared = [this] { restorer_.DiscardPrepared(); };
    hooks.join_workers_and_writers = [this](std::string& error) {
        workers_.Join();
        return lifecycle_.joined(error);
    };
    hooks.begin_epoch = [this](const auto& epoch, auto& error) {
        if (!output_.BeginOutputSegment(epoch.restored_frame, incoming_clock_.domain.tick, &error)) return false;
        files_.Select(output_.LegDir(), output_.OutputSegment());
        return true;
    };
    hooks.apply_image = [this](const auto& bytes, auto& error) {
        return restorer_.RestorePrepared(bytes.data(), bytes.size(), &error);
    };
    hooks.reset_after_restore = [this](std::string& error) {
        boundary_.ResetAfterRestore();
        workers_.SetBoundary(incoming_boundary_);
        session_.RebaseFrame(incoming_clock_.domain.boundaries);
        resume_gate_.store(true);
        return lifecycle_.resumed(static_cast<uint32_t>(incoming_clock_.domain.boundaries), error);
    };
    hooks.cancel_workers = [this] { workers_.Cancel(); };
    return hooks;
}

bool SessionRestore::Bind(SessionRuntime::Hooks& hooks, bool approved) {
    if (!approved) return true;
    if (!boundary_.Attached() || !lifecycle_.inspect || !lifecycle_.prepare ||
        !lifecycle_.joined || !lifecycle_.resumed) return false;
    hooks.restore = [this](const auto& request, uint64_t) {
        const auto result = restart_.PrepareFile(request.id, request.path);
        return SessionRuntime::CaptureResult{result.phase, result.detail};
    };
    hooks.unwind_restore = [this] { workers_.Cancel(); };
    hooks.restart = [this] {
        const auto result = restart_.RestartPrepared();
        return SessionRuntime::RestoreCompletion{result.request, {result.phase, result.detail},
            result.frame, output_.InputPath(), result.requires_close};
    };
    return true;
}
} // namespace ps2r::app
