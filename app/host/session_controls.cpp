#include "app/host/session_controls.h"

#include <algorithm>
#include <utility>

namespace ps2r::app {

std::string SessionControls::Bounded(std::string text) {
    if (text.size() > kTextLimit) text.resize(kTextLimit);
    return text;
}

bool SessionControls::Terminal(Phase phase) {
    return phase == Phase::Succeeded || phase == Phase::Refused ||
           phase == Phase::Failed || phase == Phase::Cancelled;
}

void SessionControls::Remember(Result result) {
    if (recent_.size() == kHistoryLimit) recent_.pop_front();
    recent_.push_back(std::move(result));
}

bool SessionControls::Supported(Action action) const {
    switch (action) {
        case Action::ToggleMenu: case Action::Close: return true;
        case Action::SaveReproduction: return capabilities_.save_reproduction;
        case Action::RestoreReproduction: return capabilities_.restore_reproduction;
        case Action::TogglePause: return capabilities_.pause;
        case Action::Step: return capabilities_.single_step;
        case Action::SetVolume: return capabilities_.volume;
        case Action::SetFullscreen: return capabilities_.fullscreen;
        case Action::SetKeyboardInput: return capabilities_.keyboard_input;
        case Action::SetRecording: return capabilities_.recording;
        case Action::SetPerfOverlay: return capabilities_.perf_overlay;
        case Action::SetDisplayMode: return capabilities_.fullscreen;
        case Action::SetScale: case Action::SetVSync: case Action::SetAspect: case Action::SetRenderRate:
        case Action::SetFrameCap:
            return capabilities_.display_rows;
        case Action::SetTextureFilter: case Action::SetAnisotropy: return capabilities_.texture_filter;
    }
    return false;
}

bool SessionControls::Conflicts(Action action) const {
    const auto capture = [](Action a) {
        return a == Action::SaveReproduction || a == Action::RestoreReproduction;
    };
    const auto pause = [](Action a) { return a == Action::TogglePause || a == Action::Step; };
    return std::any_of(pending_.begin(), pending_.end(), [&](const Result& result) {
        const Action other = result.request.action;
        return other == action || (capture(action) && capture(other)) ||
               (pause(action) && pause(other));
    });
}

uint64_t SessionControls::Submit(Action action, int value, std::string path) {
    // The admission itself is acknowledged: the observer receives the Queued
    // result the moment the key press is accepted, and a queue-admission
    // refusal like every finished request -- both outside the lock.
    Observer observer;
    Result admitted;
    const uint64_t id = SubmitLocked(action, value, std::move(path), observer, admitted);
    if (observer && (admitted.phase == Phase::Refused || admitted.phase == Phase::Queued)) observer(admitted);
    return id;
}

uint64_t SessionControls::SubmitLocked(Action action, int value, std::string path, Observer& observer,
                                       Result& admitted) {
    std::lock_guard<std::mutex> lock(mutex_);
    Result result{{next_id_++, action, value, {}}, Phase::Queued, "Queued for the game owner."};
    // Do not truncate a path and accidentally access a different file.
    const bool bad_path = path.size() > kTextLimit || path.find('\0') != std::string::npos;
    if (!bad_path) result.request.path = std::move(path);
    const auto refuse = [&](const char* reason) {
        result.phase = Phase::Refused;
        result.message = reason;
        Remember(result);
        menu_open_ = true;
        observer = observer_;
        admitted = result;
        return result.request.id;
    };
    if (action == Action::ToggleMenu) {
        menu_open_ = !menu_open_;
        result.phase = Phase::Succeeded;
        result.message = menu_open_ ? "Menu opened." : "Menu closed.";
        Remember(result);
        return result.request.id;
    }
    if (closing_) return refuse("The session is closing.");
    if (!Supported(action)) return refuse("This action is unavailable in this session.");
    if (bad_path) return refuse("The reproduction path is invalid or too long.");
    if (action == Action::SetVolume && (value < 0 || value > 100))
        return refuse("Volume must be between 0 and 100.");
    if ((action == Action::SetFullscreen || action == Action::SetKeyboardInput ||
         action == Action::SetRecording || action == Action::SetPerfOverlay || action == Action::SetVSync ||
         action == Action::SetRenderRate) && value != 0 && value != 1)
        return refuse("This setting must be on or off.");
    if (action == Action::SetDisplayMode && (value < 0 || value > 2))
        return refuse("The display mode must be windowed, fullscreen or borderless.");
    if (action == Action::SetScale && (value < 0 || value > 4))
        return refuse("The internal scale must be native or 1..4.");
    if (action == Action::SetAspect && (value < 0 || value > 4))
        return refuse("The aspect selection must be one of the five shapes.");
    if (action == Action::SetFrameCap && (value < 0 || value > 2))
        return refuse("The frame cap must be off, 30 or 60 (0..2).");
    if (action == Action::SetTextureFilter && (value < 0 || value > 3))
        return refuse("Texture filter must be title, point, bilinear or trilinear (0..3).");
    if (action == Action::SetAnisotropy && value != 1 && value != 2 && value != 4 && value != 8 && value != 16)
        return refuse("Anisotropy must be 1, 2, 4, 8 or 16.");
    // The main/UI snapshot may lag a completed pause command. The game owner
    // validates Step against its current pause state when consuming the queue.
    if (Conflicts(action)) return refuse("A related request is already pending.");
    if (action == Action::RestoreReproduction && result.request.path.empty()) {
        result.request.path = restore_path_;
        // F8 with nothing selected in the browser: the newest slot (the root
        // installs SavestateSlots::Latest); its reason names the directory.
        std::string reason;
        if (result.request.path.empty() && restore_fallback_) result.request.path = restore_fallback_(&reason);
        if (result.request.path.empty())
            return refuse(reason.empty() ? "No reproduction save is selected." : Bounded(reason).c_str());
    }
    if (action == Action::Close) {
        closing_ = true;
        pending_.push_front(result);
    } else {
        if (pending_.size() >= kMaxPending) return refuse("The command queue is full.");
        pending_.push_back(result);
    }
    observer = observer_;
    admitted = result;
    return result.request.id;
}

bool SessionControls::HandleFunctionKey(int number, bool down, bool repeat, bool text_input) {
    Action action;
    switch (number) {
        case 1: action = Action::ToggleMenu; break;
        case 5: action = Action::SaveReproduction; break;
        case 8: action = Action::RestoreReproduction; break;
        case 9: action = Action::TogglePause; break;
        case 10: action = Action::Step; break;
        default: return false;
    }
    if (down && !repeat && (number == 1 || !text_input)) Submit(action);
    return true;
}

bool SessionControls::TakeNext(Request& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Result& result : pending_) {
        if (result.phase != Phase::Queued) continue;
        if (closing_ && result.request.action != Action::Close) continue;
        result.phase = Phase::Working;
        result.message = "Waiting for completion.";
        request = result.request;
        return true;
    }
    return false;
}

bool SessionControls::Waiting(uint64_t id, std::string reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Result& result : pending_) {
        if (result.request.id != id || result.phase == Phase::Queued) continue;
        result.phase = Phase::Waiting;
        result.message = reason.empty() ? "Waiting for a safe boundary." : Bounded(std::move(reason));
        menu_open_ = true;
        return true;
    }
    return false;
}

void SessionControls::SetObserver(Observer observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = std::move(observer);
}

void SessionControls::SetRestoreFallback(RestoreFallback fallback) {
    std::lock_guard<std::mutex> lock(mutex_);
    restore_fallback_ = std::move(fallback);
}

bool SessionControls::Finish(uint64_t id, Phase phase, std::string message) {
    if (!Terminal(phase)) return false;
    Observer observer;
    Result finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_.begin();
        for (; it != pending_.end(); ++it)
            if (it->request.id == id && it->phase != Phase::Queued) break;
        if (it == pending_.end()) return false;
        it->phase = phase;
        it->message = message.empty() ? PhaseLabel(phase) : Bounded(std::move(message));
        if (phase != Phase::Succeeded) menu_open_ = true;
        if (it->request.action == Action::Close && phase != Phase::Succeeded) closing_ = false;
        finished = *it;
        Remember(std::move(*it));
        pending_.erase(it);
        observer = observer_;
    }
    if (observer) observer(finished);
    return true;
}

void SessionControls::CancelPending(std::string reason) {
    Observer observer;
    std::vector<Result> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true;
        for (Result& result : pending_) {
            result.phase = Phase::Cancelled;
            result.message = reason.empty() ? "Cancelled during shutdown." : Bounded(reason);
            cancelled.push_back(result);
            Remember(std::move(result));
        }
        pending_.clear();
        observer = observer_;
    }
    if (observer) for (const Result& result : cancelled) observer(result);
}

void SessionControls::Publish(Capabilities capabilities, RuntimeState runtime) {
    runtime.volume_percent = std::clamp(runtime.volume_percent, 0, 100);
    runtime.input_name = Bounded(std::move(runtime.input_name));
    runtime.recording_path = Bounded(std::move(runtime.recording_path));
    // A path used for restore is never silently shortened.
    if (runtime.restore_path.size() > kTextLimit || runtime.restore_path.find('\0') != std::string::npos)
        runtime.restore_path.clear();
    runtime.game_saves_path = Bounded(std::move(runtime.game_saves_path));
    runtime.status = Bounded(std::move(runtime.status));
    std::lock_guard<std::mutex> lock(mutex_);
    capabilities_ = capabilities;
    if (runtime.restore_path != runtime_.restore_path) restore_path_ = runtime.restore_path;
    runtime_ = std::move(runtime);
}

bool SessionControls::SelectRestorePath(std::string path) {
    if (path.size() > kTextLimit || path.find('\0') != std::string::npos) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    restore_path_ = std::move(path);
    return true;
}

SessionControls::Snapshot SessionControls::Read() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s{capabilities_, runtime_, menu_open_, closing_, pause_menu_, menu_paused_, restore_path_,
               {pending_.begin(), pending_.end()}, {recent_.begin(), recent_.end()}};
    return s;
}

bool SessionControls::MenuOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return menu_open_;
}

void SessionControls::SetPauseMenu(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    pause_menu_ = on;
}

bool SessionControls::PauseMenu() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pause_menu_;
}

void SessionControls::SetMenuPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    menu_paused_ = paused;
}

bool SessionControls::MenuPaused() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return menu_paused_;
}

const char* SessionControls::ActionLabel(Action action) {
    switch (action) {
        case Action::ToggleMenu: return "Menu";
        case Action::SaveReproduction: return "Save reproduction";
        case Action::RestoreReproduction: return "Restore reproduction";
        case Action::TogglePause: return "Pause / resume";
        case Action::Step: return "Advance one tick";
        case Action::Close: return "Close session";
        case Action::SetVolume: return "Volume";
        case Action::SetFullscreen: return "Fullscreen";
        case Action::SetKeyboardInput: return "Keyboard input";
        case Action::SetRecording: return "Input recording";
        case Action::SetPerfOverlay: return "Performance overlay";
        case Action::SetDisplayMode: return "Display mode";
        case Action::SetScale: return "Internal scale";
        case Action::SetVSync: return "VSync";
        case Action::SetAspect: return "Aspect ratio";
        case Action::SetRenderRate: return "Render rate";
        case Action::SetFrameCap: return "Frame cap";
        case Action::SetTextureFilter: return "Texture filter";
        case Action::SetAnisotropy: return "Anisotropic filtering";
    }
    return "Unknown action";
}

const char* SessionControls::PhaseLabel(Phase phase) {
    switch (phase) {
        case Phase::Queued: return "Queued";
        case Phase::Working: return "Working";
        case Phase::Waiting: return "Pending";
        case Phase::Succeeded: return "Completed";
        case Phase::Refused: return "Refused";
        case Phase::Failed: return "Failed";
        case Phase::Cancelled: return "Cancelled";
    }
    return "Unknown status";
}

}  // namespace ps2r::app
