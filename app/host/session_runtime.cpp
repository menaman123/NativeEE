#include "app/host/session_runtime.h"

#include <SDL.h>
#include <algorithm>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#include "app/host/platform_input.h"
#include "engine/platform/host/platform.h"

namespace ps2r::app {
using Action = SessionControls::Action;
using Phase = SessionControls::Phase;

SessionRuntime::SessionRuntime(PlatformInput* input)
    : input_(input), main_thread_(std::this_thread::get_id()), host_rounding_(std::fegetround()),
      window_(controls_) {
    if (input_) { input_->BindMainWake(&wake_main_); input_->BindOwnerBusy(&main_busy_); }
}

SessionRuntime::~SessionRuntime() {
    CancelMainWork();
    if (input_) { input_->StopService(); input_->BindMainWake(nullptr); input_->BindOwnerBusy(nullptr); }
}

bool SessionRuntime::Open(int volume, bool audio, bool recording, std::string recording_path, std::string& error,
                          std::string game_saves_path) {
    volume_.store(std::clamp(volume, 0, 100));
    capabilities_.volume = audio;
    capabilities_.fullscreen = true;
    capabilities_.keyboard_input = input_ != nullptr;
    status_.recording = recording;
    status_.recording_path = std::move(recording_path);
    status_.game_saves_path = std::move(game_saves_path);
    if (!window_.Open(error)) return false;
    Publish();
    return true;
}

void SessionRuntime::SetHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
    capabilities_.pause = static_cast<bool>(hooks_.pause);
    capabilities_.single_step = capabilities_.pause;
    capabilities_.save_reproduction = static_cast<bool>(hooks_.capture);
    capabilities_.restore_reproduction = hooks_.restore && hooks_.unwind_restore && hooks_.restart && hooks_.alive;
    Publish();
}

void SessionRuntime::SetClockReadout(uint32_t game_hz, uint32_t render_fps) {
    game_hz_.store(game_hz);
    render_fps_.store(render_fps);
}

void SessionRuntime::SetEventTrace(bool on) { event_trace_ = on; }

void SessionRuntime::ConfigurePresentation(bool vsync, int fps_cap) {
    if (std::this_thread::get_id() != main_thread_) return;
    fps_cap_ = std::clamp(fps_cap, 0, 1000);
    window_.SetVSync(vsync);
    Publish();
}

void SessionRuntime::ConfigureReproductions(std::string directory) {
    if (std::this_thread::get_id() != main_thread_) return;
    status_.reproductions_path = std::move(directory);
    Publish();
}

void SessionRuntime::ConfigureSettings(bool perf_overlay, Persist persist) {
    if (std::this_thread::get_id() != main_thread_) return;
    perf_overlay_.store(perf_overlay);
    persist_ = std::move(persist);
    capabilities_.perf_overlay = true;
    Publish();
}

void SessionRuntime::ConfigureDisplayRows(const DisplaySettings& seed) {
    if (std::this_thread::get_id() != main_thread_) return;
    display_rows_ = true;
    scale_setting_.store(std::clamp(seed.scale, 0, 4));
    effective_scale_.store(std::clamp(seed.scale, 1, 4));   // 0 (native) resolves on the first Publish
    aspect_setting_.store(std::clamp(seed.aspect, 0, 4));
    render_rate_.store(seed.render_rate != 0 ? 1 : 0);
    FramePacer::Cap cap = FramePacer::Cap::Hz60;
    if (FramePacer::CapOf(seed.frame_cap, cap)) { pacer_.SetCap(cap); frame_cap_.store(seed.frame_cap); }
    pacer_.SetRelax(seed.present_relax);
    display_hz_ = std::clamp(seed.display_hz, 0, 1000);
    display_mode_.store(std::clamp(seed.display_mode, 0, 2));
    if (display_mode_.load() != window_.DisplayMode()) {
        std::string error;
        if (!window_.SetDisplayMode(display_mode_.load(), error))
            std::fprintf(stderr, "nativeee: video.fullscreen: %s\n", error.c_str());
    }
    capabilities_.display_rows = true;
    Publish();
}

void SessionRuntime::ConfigurePauseMenu(bool on) {
    if (std::this_thread::get_id() != main_thread_) return;
    controls_.SetPauseMenu(on);
    Publish();
}

void SessionRuntime::ConfigureControlsPage(ControlsSection::Model model) {
    if (std::this_thread::get_id() != main_thread_) return;
    window_.Gui().SetControlsModel(std::move(model));
}

int SessionRuntime::PresentHz() const {
    if (display_hz_ > 0) return display_hz_;
    const int display = window_.DisplayRefreshHz();
    return display > 0 ? display : 60;
}

void SessionRuntime::SetPresentObserver(std::function<void(std::chrono::steady_clock::time_point, bool, uint64_t, uint64_t)> observer) {
    if (std::this_thread::get_id() != main_thread_) return;
    present_observer_ = std::move(observer);
}

void SessionRuntime::SetRenderer(std::string renderer) {
    if (std::this_thread::get_id() != main_thread_) return;
    renderer_ = std::move(renderer);
}

void SessionRuntime::ConfigureWindowDump(std::string dir) {
    if (std::this_thread::get_id() != main_thread_) return;
    window_dump_dir_ = std::move(dir);
}

void SessionRuntime::SetAspect(std::shared_ptr<gs::AspectSelection> aspect) {
    if (std::this_thread::get_id() != main_thread_) return;
    aspect_ = std::move(aspect);
    Publish();
}

void SessionRuntime::SetTextureFilter(std::shared_ptr<render::TextureFilterSelection> selection) {
    if (std::this_thread::get_id() != main_thread_) return;
    texture_filter_ = std::move(selection);
    capabilities_.texture_filter = texture_filter_ != nullptr;
    Publish();
}

int SessionRuntime::TextureFilterState() const {
    return texture_filter_ ? static_cast<int>(texture_filter_->Filter()) : 0;
}

int SessionRuntime::AnisotropyState() const {
    return texture_filter_ ? texture_filter_->Anisotropy() : 1;
}

void SessionRuntime::Publish() {
    status_.frame = frame_.load();
    status_.game_hz = game_hz_.load();
    status_.render_fps = render_fps_.load();
    status_.doubled_frames = doubled_frames_.load(std::memory_order_relaxed);
    status_.paused = paused_.load();
    status_.volume_percent = volume_.load();
    status_.fullscreen = window_.Fullscreen();
    fullscreen_state_.store(status_.fullscreen);
    display_mode_.store(window_.DisplayMode());
    status_.display_mode = display_mode_.load();
    status_.perf_overlay = perf_overlay_.load();
    status_.texture_filter = TextureFilterState();
    status_.anisotropy = AnisotropyState();
    status_.vsync = window_.VSync();
    vsync_state_.store(status_.vsync);
    status_.fps_cap = fps_cap_;
    window_.DrawableSize(status_.window_width, status_.window_height);
    if (display_rows_ && scale_setting_.load() == 0 && status_.window_height > 0) {
        // Native window: the factor that covers the drawable in 448-line steps.
        const int native = std::clamp((status_.window_height + 447) / 448, 1, 4);
        if (native != effective_scale_.load()) {
            effective_scale_.store(native);
            scale_request_.store(native);
        }
    }
    status_.scale = effective_scale_.load();
    status_.aspect = aspect_setting_.load();
    status_.render_rate = render_rate_.load();
    status_.frame_cap = frame_cap_.load();
    if (aspect_) {
        aspect_->SetWindow(status_.window_width, status_.window_height);
        window_.SetPictureAspect(aspect_->Ratio());
    }
    status_.keyboard_input = input_ && input_->KeyboardEnabled();
    keyboard_state_.store(status_.keyboard_input);
    status_.renderer = renderer_;
    status_.input_name = !input_ ? "No live input" : input_->GamepadActive() ? "Controller" :
        status_.keyboard_input ? "Keyboard and mouse" : "No enabled input device";
    status_.status = closing_ ? "Closing session..." : (restore_started_ ? "Restoring reproduction..." : "");
    controls_.Publish(capabilities_, status_);
}

bool SessionRuntime::OnMain(std::function<void()> operation) {
    if (std::this_thread::get_id() == main_thread_) { operation(); return true; }
    auto request = std::make_shared<MainWork>();
    request->operation = std::move(operation);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_ || main_requests_.size() >= 32) return false;
        main_requests_.push_back(request);
    }
    wake_main_.Notify();
    while (!request->done.load(std::memory_order_acquire)) {
        const auto generation = request->wake.Generation();
        if (!request->done.load(std::memory_order_acquire)) request->wake.Wait(generation);
    }
    return request->accepted;
}

bool SessionRuntime::PostMain(std::function<void()> operation) {
    if (std::this_thread::get_id() == main_thread_) { operation(); return true; }
    auto request = std::make_shared<MainWork>();
    request->operation = std::move(operation);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_ || main_requests_.size() >= 32) return false;
        main_requests_.push_back(std::move(request));
    }
    wake_main_.Notify();
    return true;
}

void SessionRuntime::PostFrame(std::vector<uint8_t> rgba, int width, int height, uint64_t tag) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        rgba.size() != static_cast<size_t>(width) * height * 4 || rgba.size() > (64u << 20)) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_frame_ = {std::move(rgba), width, height, ps2r::Park::NowNs(), tag};
    }
    wake_main_.Notify();
}

bool SessionRuntime::InjectFunctionKey(int number) {
    if (number < 1 || number > 12) return false;
    const int scancode = static_cast<int>(SDL_SCANCODE_F1) + (number - 1);
    return InjectKey(scancode, true) && InjectKey(scancode, false);
}

bool SessionRuntime::InjectKey(int scancode, bool down) {
    if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES || !window_.WindowId()) return false;
    SDL_Window* focus = SDL_GetKeyboardFocus();
    SDL_Event event{};
    event.key.windowID = focus ? SDL_GetWindowID(focus) : 0u;   // SDL_SendKeyboardKey's stamp
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = 0;
    event.key.keysym.scancode = static_cast<SDL_Scancode>(scancode);
    event.key.keysym.sym = SDL_GetKeyFromScancode(event.key.keysym.scancode);
    event.key.timestamp = SDL_GetTicks();
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    if (SDL_PushEvent(&event) != 1) return false;
    wake_main_.Notify();
    return true;
}

void SessionRuntime::ArmCloseAfter(uint64_t ns) {
    close_deadline_ns_.store(ps2r::Park::NowNs() + ns, std::memory_order_release);
    wake_main_.Notify();
}

bool SessionRuntime::InjectClose() {
    if (!window_.WindowId()) return false;
    SDL_Event event{};
    event.type = SDL_QUIT;
    event.quit.timestamp = SDL_GetTicks();
    if (SDL_PushEvent(&event) != 1) return false;
    wake_main_.Notify();
    return true;
}

void SessionRuntime::RequestClose() {
    closing_.store(true);
    wake_game_.Notify();
    wake_main_.Notify();
}

void SessionRuntime::CancelMainWork() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& request : main_requests_) {
        request->done.store(true, std::memory_order_release);
        request->wake.Notify();
    }
    main_requests_.clear();
}

template <class Swap>
bool SessionRuntime::TimedDisplaySwap(Swap&& swap) {
    const auto started = std::chrono::steady_clock::now();
    main_busy_.store(true, std::memory_order_release);
    const bool ok = swap();
    main_busy_.store(false, std::memory_order_release);
    const uint64_t us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    display_swaps_.fetch_add(1, std::memory_order_relaxed);
    display_swap_total_us_.fetch_add(us, std::memory_order_relaxed);
    uint64_t seen = display_swap_max_us_.load(std::memory_order_relaxed);
    while (us > seen && !display_swap_max_us_.compare_exchange_weak(seen, us)) {}
    return ok;
}

void SessionRuntime::ProcessCommands() {
    SessionControls::Request request;
    while (controls_.TakeNext(request)) {
        if (closing_ && request.action != Action::Close) {
            controls_.Finish(request.id, Phase::Refused, "The session is closing.");
            continue;
        }
        bool ok = true;
        std::string detail;
        switch (request.action) {
            case Action::Close:
                RequestClose();
                detail = "Shutdown requested.";
                break;
            case Action::SetVolume:
                volume_.store(request.value);
                detail = "Output volume applied.";
                break;
            case Action::SetFullscreen:
                ok = TimedDisplaySwap([&] { return window_.SetFullscreen(request.value != 0, detail); });
                if (ok) detail = "Display mode applied.";
                break;
            case Action::SetKeyboardInput:
                if (input_) input_->SetKeyboardEnabled(request.value != 0);
                else ok = false;
                detail = ok ? "Keyboard input applied." : "Live input is unavailable.";
                if (ok && persist_) persist_("input.kbm_pad", request.value != 0 ? "on" : "off");
                break;
            case Action::SetPerfOverlay:
                perf_overlay_.store(request.value != 0);
                detail = request.value != 0 ? "Performance overlay shown." : "Performance overlay hidden.";
                if (persist_) persist_("ui.perf_overlay", request.value != 0 ? "on" : "off");
                break;
            case Action::SetDisplayMode: {
                static const char* const kModes[] = {"windowed", "fullscreen", "borderless"};
                ok = TimedDisplaySwap([&] { return window_.SetDisplayMode(request.value, detail); });
                if (ok) {
                    display_mode_.store(request.value);
                    detail = "Display mode applied.";
                    if (persist_) persist_("video.fullscreen", kModes[request.value]);
                }
                break;
            }
            case Action::SetScale:
                scale_setting_.store(request.value);
                if (request.value != 0) {
                    effective_scale_.store(request.value);
                    scale_request_.store(request.value);
                } else {
                    effective_scale_.store(0);   // Publish resolves the native factor and requests it
                }
                detail = request.value != 0 ? "Internal scale applied at the next frame." : "Internal scale follows the window.";
                if (persist_) persist_("video.scale", std::to_string(request.value));
                break;
            case Action::SetVSync:
                ok = TimedDisplaySwap([&] { return window_.SetVSync(request.value != 0); });
                detail = ok ? "VSync applied." : "The swap interval was refused.";
                if (ok && persist_) persist_("video.vsync", request.value != 0 ? "on" : "off");
                break;
            case Action::SetAspect: {
                static const char* const kAspects[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
                aspect_setting_.store(request.value);
                if (aspect_) aspect_->SetSelection(static_cast<gs::AspectSelection::Shape>(request.value));
                detail = "Aspect ratio applied.";
                if (persist_) persist_("video.aspect", kAspects[request.value]);
                break;
            }
            case Action::SetFrameCap: {
                FramePacer::Cap cap = FramePacer::Cap::Hz60;
                ok = FramePacer::CapOf(request.value, cap);
                if (ok) {
                    pacer_.SetCap(cap);
                    frame_cap_.store(request.value);
                    detail = std::string("Frame cap applied: ") + FramePacer::CapName(cap) + ".";
                    if (persist_) persist_("video.frame_cap", FramePacer::CapName(cap));
                } else detail = "Not a frame cap.";
                break;
            }
            case Action::SetRenderRate:
                render_rate_.store(request.value != 0 ? 1 : 0);
                detail = request.value != 0 ? "Rendering at the display's rate." : "Rendering one frame per game tick.";
                if (persist_) persist_("video.render_rate", request.value != 0 ? "display" : "60");
                break;
            case Action::SetTextureFilter:
                if (texture_filter_) {
                    const auto mode = static_cast<render::TextureFilter>(request.value);
                    texture_filter_->SetFilter(mode);
                    detail = std::string("Texture filter applied: ") + render::TextureFilterSelection::Name(mode) + ".";
                    if (persist_) persist_("video.texture_filter", render::TextureFilterSelection::Name(mode));
                } else { ok = false; detail = "Texture filtering is unavailable in this session."; }
                break;
            case Action::SetAnisotropy:
                if (texture_filter_) {
                    texture_filter_->SetAnisotropy(request.value);
                    detail = "Anisotropic filtering applied: " + std::to_string(texture_filter_->Anisotropy()) + "x.";
                    if (persist_) persist_("video.anisotropy", std::to_string(texture_filter_->Anisotropy()));
                } else { ok = false; detail = "Texture filtering is unavailable in this session."; }
                break;
            case Action::TogglePause: case Action::Step: case Action::SaveReproduction:
            case Action::RestoreReproduction: {
                std::lock_guard<std::mutex> lock(mutex_);
                game_requests_.push_back(request);
                wake_game_.Notify();
                continue;
            }
            default: ok = false; detail = "This action has no runtime handler."; break;
        }
        controls_.Finish(request.id, ok ? Phase::Succeeded : Phase::Failed, detail);
    }
}

void SessionRuntime::PumpMain() {
    SDL_Event event;
    // Event trace (SetEventTrace; the host arms it from dev.log_level=narrate --
    // Config is the one environment reader): every key / window event the pump
    // drains, with the id SDL stamped on it, the session window's id, SDL's
    // keyboard-focus window and how the GUI routed it -- the attribution trace
    // for a key that "does nothing" (the s5 F5).
    const bool trace = event_trace_;
    while (SDL_PollEvent(&event)) {
        const bool key = event.type == SDL_KEYDOWN || event.type == SDL_KEYUP;
        if (key) {
            ++key_census_.key_events;
            if (event.key.keysym.sym >= SDLK_F1 && event.key.keysym.sym <= SDLK_F12 && event.type == SDL_KEYDOWN)
                ++key_census_.function_keys;
        }
        if (trace && (key || event.type == SDL_WINDOWEVENT)) {
            SDL_Window* focus = SDL_GetKeyboardFocus();
            std::fprintf(stderr, "[sdl-trace] %s windowID=%u session_window=%u focus_window=%u menu=%d",
                event.type == SDL_KEYDOWN ? "KEYDOWN" : event.type == SDL_KEYUP ? "KEYUP" : "WINDOWEVENT",
                key ? event.key.windowID : event.window.windowID, window_.WindowId(),
                focus ? SDL_GetWindowID(focus) : 0u, controls_.MenuOpen() ? 1 : 0);
            if (key) std::fprintf(stderr, " scancode=%d sym=%d repeat=%d", event.key.keysym.scancode,
                                  event.key.keysym.sym, event.key.repeat);
            else std::fprintf(stderr, " event=%u", event.window.event);
            std::fprintf(stderr, "\n");
        }
        const auto route = window_.HandleEvent(event);
        const int fkey = key && event.type == SDL_KEYDOWN && event.key.keysym.sym >= SDLK_F1 &&
                                 event.key.keysym.sym <= SDLK_F12 ? int(event.key.keysym.sym - SDLK_F1 + 1) : 0;
        const bool hotkey = fkey == 1 || fkey == 5 || fkey == 8 || fkey == 9 || fkey == 10;
        if (hotkey && !route.consumed) {
            // LOUD, never silent: a hotkey the GUI did not take names the stamp
            // that lost it (the only reason SessionGui declines a hotkey).
            ++key_census_.discarded;
            std::fprintf(stderr, "nativeee: F%d discarded: SDL stamped window id %u (session window %u)\n",
                         fkey, event.key.windowID, window_.WindowId());
        }
        if (trace && key)
            std::fprintf(stderr, "[sdl-trace]   route accepted=%d consumed=%d capture_keyboard=%d -> pad mapper=%d\n",
                         route.accepted, route.consumed, route.capture_keyboard, input_ && !route.consumed ? 1 : 0);
        if (input_) {
            input_->SetUiCapture(route.capture_keyboard || route.capture_mouse);
            if (!route.consumed || event.type == SDL_WINDOWEVENT ||
                event.type == SDL_JOYDEVICEADDED || event.type == SDL_JOYDEVICEREMOVED)
                input_->HandleEvent(event);
        }
    }
    ProcessCommands();
    if (input_) {
        input_->SetUiCapture(controls_.MenuOpen());
        input_->ServicePending();
    }
    std::deque<std::shared_ptr<MainWork>> work;
    Frame frame;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        work.swap(main_requests_);
        std::swap(frame, incoming_frame_);
    }
    for (auto& request : work) {
        if (!closing_) { request->operation(); request->accepted = true; }
        request->done.store(true, std::memory_order_release);
        request->wake.Notify();
    }
    if (!frame.rgba.empty()) {
        // The posted picture joins the two-deep queue the pacer admits from (Run) in order:
        // two pictures inside one period are shown at consecutive edges, not the newer over
        // the older. A third arrival drops the oldest unshown (the game's tick ran; only its
        // picture is not shown).
        if (pending_frame_.rgba.empty()) {
            pending_frame_ = std::move(frame);
        } else {
            if (!queued_frame_.rgba.empty()) {
                pacer_.Supersede();
                pending_frame_ = std::move(queued_frame_);
            }
            queued_frame_ = std::move(frame);
        }
    }
    PollCapture();
    PollRestore();
    Publish();
}

void SessionRuntime::PollCapture() {
    const uint64_t id = capture_id_.load();
    if (!id || !hooks_.poll_capture) return;
    // Bounded drain also handles a stale result before the real completion
    // during the last poll after shutdown. Providers must never block here.
    for (size_t attempt = 0; attempt < SessionControls::kMaxPending; ++attempt) {
        std::optional<CaptureCompletion> completion;
        try {
            completion = hooks_.poll_capture();
        } catch (const std::exception& failure) {
            completion = CaptureCompletion{id, {Phase::Failed, failure.what()}, {}};
        } catch (...) {
            completion = CaptureCompletion{id, {Phase::Failed, "Capture completion provider failed."}, {}};
        }
        if (!completion) return;
        if (completion->request != id) {
            controls_.Waiting(id, "Ignored a stale writer result; current reproduction is still pending.");
            continue;
        }
        auto result = std::move(completion->result);
        if (result.phase == Phase::Working || result.phase == Phase::Waiting || result.phase == Phase::Queued)
            result = {Phase::Failed, "Writer completion provider returned a nonterminal result."};
        if (result.phase == Phase::Succeeded &&
            (completion->path.empty() || completion->path.size() > SessionControls::kTextLimit ||
             completion->path.find('\0') != std::string::npos))
            result = {Phase::Failed, "Writer completed without a valid reproduction path."};
        // Clear before Finish can admit another capture on the game owner.
        capture_id_.store(0);
        if (controls_.Finish(id, result.phase, result.detail) && result.phase == Phase::Succeeded)
            status_.restore_path = std::move(completion->path);
        return;
    }
}

void SessionRuntime::CaptureAtBoundary(uint64_t frame) {
    if (!save_request_.id) return;
    auto result = hooks_.capture ? hooks_.capture(save_request_, frame) :
        CaptureResult{Phase::Refused, "Reproduction capture is unavailable."};
    if (result.phase == Phase::Working && hooks_.poll_capture) {
        controls_.Waiting(save_request_.id, result.detail);
        // Publish only after Capture returned. Even an immediate write cannot
        // have its completion consumed before its request identity is known.
        capture_id_.store(save_request_.id);
        save_request_ = {};
    } else if (result.phase == Phase::Waiting && !paused_ && ++save_attempts_ < 120) {
        controls_.Waiting(save_request_.id, result.detail);
    } else {
        if (result.phase == Phase::Queued || result.phase == Phase::Working)
            result = {Phase::Failed, "Capture handler returned no completion or wait result."};
        // An owner refused every boundary of the wait: the refusal names that
        // owner (the last boundary's reason) and the count, never a silent
        // no-op. Paused, the single boundary's reason is the refusal itself.
        if (result.phase == Phase::Waiting) {
            result.phase = Phase::Refused;
            if (!paused_)
                result.detail = "No admissible boundary in " + std::to_string(save_attempts_ + 1) +
                    " consecutive boundaries; the last owner to refuse: " + result.detail;
        }
        controls_.Finish(save_request_.id, result.phase, result.detail);
        save_request_ = {};
    }
}

bool SessionRuntime::Boundary(uint64_t frame) {
    frame_.store(frame);
    if (step_id_) {
        if (hooks_.pause) hooks_.pause(true);
        controls_.Finish(step_id_, Phase::Succeeded, "Advanced one game boundary; paused.");
        step_id_ = 0;
    }
    for (;;) {
        if (closing_) return false;
        std::deque<SessionControls::Request> requests;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests.swap(game_requests_);
        }
        bool step = false;
        bool restore = false;
        for (const auto& request : requests) {
            if (restore) {
                controls_.Finish(request.id, Phase::Refused, "Restore is already unwinding this game owner.");
            } else if (request.action == Action::RestoreReproduction) {
                restore = RestoreAtBoundary(request, frame);
            } else if (request.action == Action::TogglePause) {
                paused_.store(!paused_.load());
                if (hooks_.pause) hooks_.pause(paused_);
                controls_.Finish(request.id, Phase::Succeeded, paused_ ? "Game paused." : "Game resumed.");
            } else if (request.action == Action::Step) {
                if (!paused_) controls_.Finish(request.id, Phase::Refused, "Pause before advancing one tick.");
                else { step_id_ = request.id; step = true; }
            } else if (request.action == Action::SaveReproduction) {
                save_request_ = request;
                save_attempts_ = 0;
            }
        }
        if (restore) {
            if (step_id_) controls_.Finish(step_id_, Phase::Cancelled, "Restore cancelled the pending step.");
            step_id_ = 0;
            return false;
        }
        CaptureAtBoundary(frame);
        wake_main_.Notify();
        if (step) {
            if (hooks_.pause) hooks_.pause(false);
            return true;
        }
        if (!paused_) return true;
        const auto generation = wake_game_.Generation();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!game_requests_.empty() || closing_) continue;
        }
        wake_game_.Wait(generation);
    }
}

int SessionRuntime::Run(std::function<int()> game, unsigned shutdown_seconds) {
    // Construction precedes root's guest-binding setup, which can change
    // main's mode. Restore the caller's host mode before SDL/GUI pumping.
    // Each actual game worker establishes its own guest mode at entry.
    if (host_rounding_ == -1 || std::fesetround(host_rounding_) != 0) {
        std::fprintf(stderr, "Session could not restore host floating-point rounding.\n");
        RequestClose();
        game = [] { return 2; }; // Existing supervisor still drains services.
    }
    struct Worker {
        std::function<int()> game;
        SessionRuntime* session;
        std::atomic<bool> done{false};
        int result = 2;
        std::exception_ptr failure{};
    } worker{std::move(game), this};
    // The game thread carries a 1 GiB stack (the recompiled EE's frames): host::ThreadCreate
    // is the one native-thread entry with an explicit stack size (pthread_attr_setstacksize /
    // _beginthreadex with STACK_SIZE_PARAM_IS_A_RESERVATION -- reserved, committed lazily).
    ps2r::host::NativeThread thread;
    const bool created = ps2r::host::ThreadCreate(&thread, [](void* argument) {
        auto& job = *static_cast<Worker*>(argument);
        // The game thread (the EE) runs at the interactive class the pacer, the GS
        // consumer, the MTVU worker and main already hold (platform::host: the
        // macOS QoS pin; a name on Linux): a pthread_create'd thread on macOS is
        // the default QoS class, the one the scheduler moves to the efficiency
        // cores first when the machine is loaded -- the level route's EE read ~40 CPU
        // ms per frame on a shared machine with every other thread of the process
        // pinned above it (track 1d). Scheduling only; the game's work is unchanged.
        ps2r::platform::host::PinCurrentThreadInteractive("ps2r-game");
        job.session->shutdown_.Enter("game_unwind");   // the game's own return / unwind to here
        try { job.result = job.session->Supervise(job.game()); } catch (...) { job.failure = std::current_exception(); }
        // Service joins belong under the same main-owned shutdown deadline as
        // the game join. Main continues to pump until this worker is finished.
        job.session->RequestClose();
        job.session->shutdown_.Begin();   // a game that returned on its own: the close starts here
        job.session->shutdown_.Leave();
        try {
            if (job.session->hooks_.stopped) job.session->hooks_.stopped();
        } catch (...) {
            if (!job.failure) job.failure = std::current_exception();
        }
        job.done.store(true, std::memory_order_release);
        job.session->wake_main_.Notify();
    }, &worker, size_t{1024} * 1024 * 1024);
    if (!created) {
        RequestClose();
        if (input_) input_->StopService();
        if (hooks_.stop) hooks_.stop();
        if (hooks_.stopped) hooks_.stopped();
        PollCapture();
        PollRestore();
        Publish();
        controls_.CancelPending("The game thread could not be started.");
        std::fprintf(stderr, "Session game thread creation failed (1 GiB stack).\n");
        return 2;
    }
    uint64_t close_started = 0, next_present = 0;
    while (!worker.done.load(std::memory_order_acquire)) {
        const auto generation = wake_main_.Generation();
        PumpMain();
        const uint64_t now = ps2r::Park::NowNs();
        const uint64_t restore_started = restore_started_.load();
        if (restore_started && now >= restore_started &&
            now - restore_started >= uint64_t(shutdown_seconds) * 1000000000ull) {
            std::fprintf(stderr, "Session restore exceeded %u seconds; terminating process.\n", shutdown_seconds);
            std::_Exit(2);
        }
        // The presented-frame cadence (video.frame_cap; FramePacer): the oldest waiting picture
        // is uploaded and drawn at the pacer's edge (at once when it was posted after an open
        // edge: the train slips to it); the fps_cap train below keeps re-drawing the shown
        // picture (the menu, the overlays) between edges.
        if (pacer_.Admit(now, pending_frame_.posted_ns)) {
            window_.Upload(pending_frame_.rgba, pending_frame_.width, pending_frame_.height);
            shown_tag_ = pending_frame_.tag;
            shown_posted_ns_ = pending_frame_.posted_ns;
            doubled_frames_.store(pacer_.Count().doubled, std::memory_order_relaxed);
            pending_frame_ = std::move(queued_frame_);
            queued_frame_ = {};
            picture_fresh_ = true;
            next_present = now;
        }
        if (now >= next_present) {
            // Not marked main_busy_: a present is a few ms (a vsync'd swap at most a
            // period) -- the pad's bounded wait covers it; marking it made a third of
            // the boundaries' reads late (fix2: late=633 of 1800).
            window_.Draw();
            presents_.fetch_add(1, std::memory_order_relaxed);
            if (present_observer_) present_observer_(std::chrono::steady_clock::now(), picture_fresh_, shown_tag_, shown_posted_ns_);
            picture_fresh_ = false;
            // video.fps_cap paces the presenter (0 presents on every pump); under
            // video.render_rate=display the period is the display's (A3), the
            // edges a fixed train (re-anchored when the pump fell a frame behind).
            const uint64_t period = render_rate_.load() != 0 ? 1000000000ull / uint64_t(std::max(PresentHz(), 1)) :
                                    fps_cap_ > 0 ? 1000000000ull / uint64_t(fps_cap_) : 0;
            if (period == 0) next_present = now;
            else if (next_present == 0 || now - next_present > 2 * period) next_present = now + period;
            else next_present += period;
        }
        if (const uint64_t deadline = close_deadline_ns_.load(std::memory_order_acquire); deadline && now >= deadline) {
            close_deadline_ns_.store(0, std::memory_order_release);
            std::fprintf(stderr, "nativeee: harness.close_after_ms: %s\n",
                         InjectClose() ? "SDL_QUIT pushed into SDL's queue (main)" : "SDL_PushEvent refused");
        }
        if (closing_) {
            if (!stop_called_) {
                stop_called_ = true;
                close_started = now;
                shutdown_.Begin();
                if (input_) input_->StopService();
                CancelMainWork();
                if (hooks_.stop) hooks_.stop();
            }
            if (now - close_started >= uint64_t(shutdown_seconds) * 1000000000ull) {
                uint64_t stuck_ms = 0;
                const std::string stuck = shutdown_.Current(&stuck_ms);
                std::fprintf(stderr, "Session shutdown exceeded %u seconds; terminating process (shutdown=%s; stuck in %s for %llu ms).\n",
                             shutdown_seconds, shutdown_.Render().c_str(), stuck.empty() ? "<no step>" : stuck.c_str(),
                             static_cast<unsigned long long>(stuck_ms));
                if (hooks_.shutdown_report) hooks_.shutdown_report(shutdown_.Render());
                // Do not destroy resources still referenced by a stuck worker.
                std::_Exit(2);
            }
        }
        // Sleep to the next present edge (the re-draw train or the pacer's admission edge,
        // whichever is first), at most 4 ms (the command / frame pump); a posted picture
        // wakes the pump (PostFrame notifies).
        const uint64_t until = ps2r::Park::NowNs();
        uint64_t to_edge = next_present > until ? next_present - until : 0;
        if (!pending_frame_.rgba.empty()) to_edge = std::min(to_edge, pacer_.ToNextEdge(until));
        wake_main_.WaitFor(std::min<uint64_t>(4000000, std::max<uint64_t>(to_edge, 200000)), generation);
    }
    ps2r::host::ThreadJoin(thread);
    shutdown_.End();
    std::fprintf(stderr, "nativeee: session shutdown %s\n", shutdown_.Render().c_str());
    // The presented frame's record (ConfigureWindowDump): one last draw of the
    // picture the session ended on, with its overlays, read back from the window.
    if (!window_dump_dir_.empty()) {
        std::string error;
        if (!window_.DumpPresented(window_dump_dir_ + "/w_final.ppm", error))
            std::fprintf(stderr, "nativeee: window dump: %s\n", error.c_str());
    }
    // Close admission before the pump ends, including an empty/failed start.
    RequestClose();
    if (input_) input_->StopService();
    CancelMainWork();
    // stopped() drained the writer before worker.done; consume its last real
    // result even if it completed after main's final interactive pump.
    PollCapture();
    PollRestore();
    Publish();
    controls_.CancelPending("Session ended before this action completed.");
    if (worker.failure) std::rethrow_exception(worker.failure);
    return worker.result;
}

}  // namespace ps2r::app
