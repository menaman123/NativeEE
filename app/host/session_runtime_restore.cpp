#include "app/host/session_runtime.h"

#include <exception>
#include <utility>

namespace ps2r::app {
using Phase = SessionControls::Phase;

bool SessionRuntime::RestoreAtBoundary(const SessionControls::Request& request, uint64_t frame) {
    if (!capabilities_.restore_reproduction) {
        controls_.Finish(request.id, Phase::Refused, "Restore composition is unavailable.");
        return false;
    }
    restore_started_.store(ps2r::Park::NowNs());
    wake_main_.Notify();
    CaptureResult result;
    try { result = hooks_.restore(request, frame); }
    catch (const std::exception& error) { result = {Phase::Failed, error.what()}; }
    catch (...) { result = {Phase::Failed, "Restore preflight provider failed."}; }
    if (result.phase != Phase::Working) {
        if (result.phase != Phase::Failed && result.phase != Phase::Refused)
            result = {Phase::Failed, "Restore preflight returned an invalid result."};
        controls_.Finish(request.id, result.phase, result.detail);
        restore_started_.store(0);
        wake_main_.Notify();
        return false;
    }
    controls_.Waiting(request.id, result.detail);
    restore_id_.store(request.id);
    try { hooks_.unwind_restore(); }
    catch (...) {
        controls_.Finish(request.id, Phase::Failed, "Restore owner unwind failed; closing session.");
        RequestClose();
    }
    wake_main_.Notify();
    return true;
}

int SessionRuntime::Supervise(int result) {
    if (result || !hooks_.alive) return result;
    ps2r::Park wait;
    while (!Closing()) {
        const auto id = restore_id_.load();
        if (id) {
            RestoreCompletion completion;
            try { completion = hooks_.restart(); }
            catch (const std::exception& error) {
                completion = {id, {Phase::Failed, error.what()}, 0, {}, true};
            } catch (...) {
                completion = {id, {Phase::Failed, "Restore supervisor failed."}, 0, {}, true};
            }
            if (completion.request != id || (completion.result.phase != Phase::Succeeded &&
                    completion.result.phase != Phase::Failed && completion.result.phase != Phase::Refused)) {
                completion = {id, {Phase::Failed, "Restore returned no matching terminal result."}, 0, {}, true};
            }
            // A refused reconstruction after unwind cannot resume the old
            // owners. The session must close even if a provider omitted it.
            if (completion.result.phase != Phase::Succeeded) completion.requires_close = true;
            const bool close = completion.requires_close;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                restore_completion_ = std::move(completion);
            }
            restore_id_.store(0);
            restore_started_.store(0);
            wake_main_.Notify();
            if (close) { RequestClose(); return 2; }
        } else if (!hooks_.alive()) {
            break;
        }
        wait.WaitFor(4000000, wait.Generation());
    }
    return result;
}

void SessionRuntime::PollRestore() {
    std::optional<RestoreCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        completion.swap(restore_completion_);
    }
    if (!completion) return;
    if (controls_.Finish(completion->request, completion->result.phase, completion->result.detail) &&
        completion->result.phase == Phase::Succeeded) {
        status_.recording_path = std::move(completion->recording_path);
    }
}

} // namespace ps2r::app
