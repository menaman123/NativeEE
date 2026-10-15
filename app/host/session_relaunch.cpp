#include "app/host/session_relaunch.h"

#include <mutex>
#include <utility>

#include "app/host/savestate_slots.h"
#include "app/host/session_workers.h"

namespace ps2r::app {
using Phase = SessionControls::Phase;

namespace {
std::mutex& PendingMutex() {
    static std::mutex mutex;
    return mutex;
}
}  // namespace

std::string& SessionRelaunch::PendingPath() {
    static std::string path;
    return path;
}

std::string SessionRelaunch::TakePending() {
    std::lock_guard<std::mutex> lock(PendingMutex());
    std::string path;
    path.swap(PendingPath());
    return path;
}

bool SessionRelaunch::Pending() {
    std::lock_guard<std::mutex> lock(PendingMutex());
    return !PendingPath().empty();
}

SessionRelaunch::SessionRelaunch(SessionWorkers& workers, const savestate::SectionRegistry& registry,
                                 std::string slots_directory, std::string live_composition,
                                 std::string live_build_id)
    : workers_(workers), registry_(registry), slots_directory_(std::move(slots_directory)),
      live_composition_(std::move(live_composition)), live_build_id_(std::move(live_build_id)) {}

void SessionRelaunch::Bind(SessionRuntime::Hooks& hooks) {
    hooks.restore = [this](const SessionControls::Request& request, uint64_t frame) {
        std::string error;
        std::string path = request.path;
        // The browser's selection is a file; F8 with nothing selected takes the
        // newest slot. A path outside the slot directory is still a file the
        // user chose: it is preflighted like any other.
        if (path.empty()) path = SavestateSlots::Latest(slots_directory_, &error);
        if (path.empty()) return SessionRuntime::CaptureResult{Phase::Refused, error};
        savestate::FileMeta meta;
        if (!SavestateSlots::Preflight(path, registry_, &error, &meta))
            return SessionRuntime::CaptureResult{Phase::Refused, path + ": " + error};
        // The sidecar's composition against the live one, with the session
        // still open: a slot of another profile / hle.* composition is refused
        // HERE by name, never discovered by the relaunched --load-state.
        std::string warning;
        if (!SavestateSlots::PreflightComposition(path, live_composition_, live_build_id_, &error, &warning))
            return SessionRuntime::CaptureResult{Phase::Refused, path + ": " + error};
        if (!warning.empty()) SavestateSlots::Report("F8 restore", "Warning", path + ": " + warning);
        // The image main will exec: unresolvable means refuse now, not after
        // the session is gone.
        const std::string image = SavestateSlots::RunningImage(&error);
        if (image.empty())
            return SessionRuntime::CaptureResult{Phase::Refused,
                "cannot resolve the running executable image for the relaunch: " + error};
        request_ = request.id;
        frame_ = frame;
        path_ = path;
        return SessionRuntime::CaptureResult{Phase::Working,
            "Savestate " + path + " (frame " + std::to_string(meta.vsync_frame) +
            ") accepted: closing this session and restarting " + image + " with --load-state."};
    };
    hooks.unwind_restore = [this] { workers_.Cancel(); };
    hooks.restart = [this] {
        SessionRuntime::RestoreCompletion completion;
        completion.request = request_;
        completion.frame = frame_;
        completion.requires_close = true;
        if (path_.empty()) {
            completion.result = {Phase::Failed, "Relaunch restart without an accepted savestate."};
            return completion;
        }
        {
            std::lock_guard<std::mutex> lock(PendingMutex());
            PendingPath() = path_;
        }
        completion.result = {Phase::Succeeded, "Restarting from " + path_ + " (--load-state)."};
        return completion;
    };
}

}  // namespace ps2r::app
