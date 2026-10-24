#include "app/host/session_capture.h"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>

#include "app/host/mtvu_stack.h"
#include "app/host/savestate_slots.h"
#include "app/host/session_clock.h"
#include "engine/core/config/config.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/timing/game_boundary.h"

namespace ps2r::app {

namespace {
uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}  // namespace

SessionCapture::SessionCapture(const savestate::SectionRegistry& sections, GameBoundary& boundary, const Config& config,
                               MtvuStack* mtvu, std::string build_id)
    : config_(config), build_id_(std::move(build_id)), checkpoint_(sections, {
        [] { return EeRunToken::Instance().CanCaptureCurrentThread(); },
        [this](std::string& error) {
            std::string owner, reason;
            if (conditions_.Check(&owner, &reason)) return true;
            error = owner + ": " + reason;
            return false;
        },
        [&boundary](SessionCheckpoint::Records& records, std::string& error) {
            return boundary.FreshThreads(records, error);
        },
        DispatchTable::CanResumeAt
    }) {
    // The MTVU worker: the pipeline and the arbiter must run empty before the VU1 sections read
    // the unit (the same wait the shutdown ladder takes); the composition adds its own owners.
    if (mtvu) conditions_.Register("mtvu", [mtvu](std::string* why) { return mtvu->Quiesce(why); });
}

savestate::Quiescence& SessionCapture::Conditions() { return conditions_; }

SessionCheckpoint::Result SessionCapture::Capture(uint64_t request, uint64_t frame, std::string path) {
    savestate::FileMeta meta;
    std::string frame_error;
    if (!SessionClock::SetFrame(meta, frame, frame_error)) {
        ++refused_;
        return {request, SessionControls::Phase::Refused, std::move(path), frame_error, 0};
    }
    meta.wall_ms = NowMs();
    if (path.empty()) {
        const std::filesystem::path directory = SavestateSlots::Directory(config_);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            ++failed_;
            return {request, SessionControls::Phase::Failed, {}, "savestate directory " + directory.string() + ": " + error.message(), 0};
        }
        path = SavestateSlots::SlotPath(directory.string(), config_.save.slot);
    }
    SessionCheckpoint::Result result = checkpoint_.Capture(request, std::move(path), meta);
    if (result.phase == SessionControls::Phase::Working) ++admitted_;
    else if (result.phase == SessionControls::Phase::Refused) ++refused_;
    else if (result.phase == SessionControls::Phase::Failed) ++failed_;
    return result;
}

std::optional<SessionCheckpoint::Result> SessionCapture::Poll() {
    auto result = checkpoint_.Poll();
    if (!result) return result;
    if (result->phase != SessionControls::Phase::Succeeded) {
        ++failed_;
        return result;
    }
    SavestateSlots::Meta meta;
    meta.frame = result->frame;
    meta.wall_ms = NowMs();
    meta.build_id = build_id_;
    meta.composition = SavestateSlots::Composition(config_);
    std::string error;
    if (!SavestateSlots::WriteMeta(result->path, std::move(meta), &error)) {
        ++failed_;
        result->phase = SessionControls::Phase::Failed;
        result->detail = "savestate written but its .meta sidecar failed: " + error;
        return result;
    }
    ++written_;
    result->detail = "Savestate written: " + result->path + " (frame " + std::to_string(result->frame) + ", sidecar " +
                     SavestateSlots::MetaPath(result->path) + ")";
    return result;
}

void SessionCapture::Bind(SessionRuntime::Hooks& hooks) {
    hooks.capture = [this](const SessionControls::Request& request, uint64_t frame) {
        const auto result = Capture(request.id, frame, request.path);
        return SessionRuntime::CaptureResult{result.phase, result.detail};
    };
    hooks.poll_capture = [this]() -> std::optional<SessionRuntime::CaptureCompletion> {
        const auto result = Poll();
        if (!result) return {};
        return SessionRuntime::CaptureCompletion{result->request, {result->phase, result->detail}, result->path};
    };
}

void SessionCapture::Join() { checkpoint_.Join(); }

std::string SessionCapture::Census() const {
    return "admitted=" + std::to_string(admitted_) + ":written=" + std::to_string(written_) + ":refused=" +
           std::to_string(refused_) + ":failed=" + std::to_string(failed_) + ":quiesce_checks=" +
           std::to_string(conditions_.Checks()) + ":quiesce_refusals=" + std::to_string(conditions_.Refusals());
}

std::string SessionCapture::ReproductionsDirectory(const Config& config) { return SavestateSlots::Directory(config); }

}  // namespace ps2r::app
