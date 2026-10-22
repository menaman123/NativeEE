#include "app/host/session_controls.h"

#include <cstdio>
#include <cfenv>
#include <cstring>
#include <thread>
#include <vector>

#if PS2R_PRODUCT_CHECKPOINT_TESTS
#include <filesystem>
#include "app/host/session_checkpoint.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/savestate/thread_capture.h"
#include "engine/ee/savestate/thread_relauncher.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_event_queue.h"
#endif

#if PS2R_PRODUCT_GUI_TESTS
#include <SDL.h>
#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER
#include "app/gui/session_gui.h"
#include "app/host/platform_input.h"
#include "engine/platform/sdl/gl_context.h"
#include "app/host/session_runtime.h"
#include "engine/ee/cpu/ps2_float.h"
#endif

using Controls = ps2r::app::SessionControls;
using Action = Controls::Action;
using Phase = Controls::Phase;
static int failures = 0;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static Controls::Capabilities Capabilities() {
    Controls::Capabilities capabilities;
    capabilities.pause = capabilities.single_step = true;
    capabilities.save_reproduction = capabilities.restore_reproduction = true;
    capabilities.volume = capabilities.fullscreen = capabilities.keyboard_input = capabilities.recording = true;
    return capabilities;
}

static void HotkeysAndUiShareCommands() {
    const int keys[] = {5, 8, 9, 10};
    const Action actions[] = {Action::SaveReproduction, Action::RestoreReproduction,
                              Action::TogglePause, Action::Step};
    for (size_t i = 0; i < 4; ++i) {
        Controls keys_controls, buttons;
        Controls::RuntimeState state;
        state.paused = true;
        state.restore_path = "reproductions/selected.save";
        keys_controls.Publish(Capabilities(), state);
        buttons.Publish(Capabilities(), state);
        CHECK(keys_controls.HandleFunctionKey(keys[i], true, false, false));
        const auto id = buttons.Submit(actions[i]);
        Controls::Request key_request, button_request;
        CHECK(keys_controls.TakeNext(key_request));
        CHECK(buttons.TakeNext(button_request));
        CHECK(button_request.id == id);
        CHECK(key_request.action == button_request.action);
        CHECK(key_request.path == button_request.path);
        CHECK(!keys_controls.TakeNext(key_request));
    }
    Controls controls;
    controls.Publish(Capabilities(), {});
    for (int key : {1, 5, 8, 9, 10}) {
        CHECK(controls.HandleFunctionKey(key, false, false, false));
        CHECK(controls.HandleFunctionKey(key, true, true, false));
        if (key != 1) CHECK(controls.HandleFunctionKey(key, true, false, true));
    }
    CHECK(controls.Read().pending.empty());
    CHECK(!controls.MenuOpen());
    CHECK(controls.HandleFunctionKey(1, true, false, true));
    CHECK(controls.MenuOpen());
    controls.Submit(Action::ToggleMenu);
    CHECK(!controls.MenuOpen());
    CHECK(!controls.HandleFunctionKey(2, true, false, false));
}

static void SaveNeverSucceedsBeforeOwnerCompletion() {
    Controls controls;
    controls.Publish(Capabilities(), {});
    const auto save = controls.Submit(Action::SaveReproduction);
    CHECK(controls.Read().pending.front().phase == Phase::Queued);
    CHECK(!controls.Finish(save, Phase::Succeeded, "Premature"));
    CHECK(!controls.Waiting(save, "Premature"));
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(request.id == save && request.path.empty());
    CHECK(controls.Read().recent.empty());
    CHECK(controls.Waiting(save, "Unsubmitted graphics work."));
    CHECK(controls.Read().pending.front().phase == Phase::Waiting);
    CHECK(controls.Read().pending.front().message == "Unsubmitted graphics work.");
    CHECK(controls.MenuOpen());
    const auto duplicate = controls.Submit(Action::SaveReproduction);
    CHECK(duplicate != save);
    CHECK(controls.Read().recent.back().phase == Phase::Refused);
    controls.Submit(Action::RestoreReproduction, 0, "other.save");
    CHECK(controls.Read().recent.back().phase == Phase::Refused);
    CHECK(controls.Read().pending.size() == 1);
    CHECK(!controls.Finish(save, Phase::Waiting, "Not terminal"));
    CHECK(controls.Finish(save, Phase::Refused, "No safe boundary within the capture budget."));
    CHECK(controls.Read().pending.empty());
    CHECK(controls.Read().recent.back().phase == Phase::Refused);
    CHECK(!controls.Finish(save, Phase::Succeeded, "Stale writer callback"));
    const auto next = controls.Submit(Action::SaveReproduction);
    CHECK(controls.TakeNext(request));
    CHECK(request.id == next);
    CHECK(controls.Finish(next, Phase::Failed, "Disk full; no reproduction save was written."));
    CHECK(controls.Read().recent.back().message.find("Disk full") != std::string::npos);
}

static void RestoreValidationFailureLeavesPublishedStateAlone() {
    Controls controls;
    Controls::RuntimeState state;
    state.frame = 912;
    state.restore_path = "reproductions/chosen.save";
    state.input_name = "Controller";
    state.game_saves_path = "progress";
    controls.Publish(Capabilities(), state);
    const auto id = controls.Submit(Action::RestoreReproduction);
    // Selection changes cannot redirect an already submitted restore.
    state.restore_path = "reproductions/other.save";
    controls.Publish(Capabilities(), state);
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(request.path == "reproductions/chosen.save");
    CHECK(controls.Finish(id, Phase::Failed, "Validation failed before restore."));
    const auto result = controls.Read();
    CHECK(result.runtime.frame == 912);
    CHECK(result.runtime.input_name == "Controller");
    CHECK(result.runtime.game_saves_path == "progress");
    CHECK(result.recent.back().phase == Phase::Failed);
}

static void CapabilitiesAndValuesAreHonest() {
    Controls controls;
    for (Action action : {Action::SaveReproduction, Action::RestoreReproduction, Action::TogglePause,
                          Action::Step, Action::SetVolume, Action::SetFullscreen,
                          Action::SetKeyboardInput, Action::SetRecording}) {
        controls.Submit(action);
        CHECK(controls.Read().pending.empty());
        CHECK(controls.Read().recent.back().phase == Phase::Refused);
    }
    controls.Publish(Capabilities(), {});
    const auto step = controls.Submit(Action::Step);
    Controls::Request step_request;
    CHECK(controls.TakeNext(step_request) && step_request.id == step);
    CHECK(controls.Finish(step, Phase::Refused, "Pause before advancing one tick."));
    controls.Submit(Action::RestoreReproduction);
    CHECK(controls.Read().recent.back().message == "No reproduction save is selected.");
    controls.Submit(Action::SetVolume, -1);
    CHECK(controls.Read().pending.empty());
    controls.Submit(Action::SetVolume, 101);
    CHECK(controls.Read().pending.empty());
    for (auto action : {Action::SetFullscreen, Action::SetKeyboardInput, Action::SetRecording}) {
        controls.Submit(action, 2);
        CHECK(controls.Read().pending.empty());
    }
    controls.Submit(Action::RestoreReproduction, 0, std::string("bad\0path", 8));
    CHECK(controls.Read().pending.empty());
    controls.Submit(Action::RestoreReproduction, 0, std::string(Controls::kTextLimit + 1, 'x'));
    CHECK(controls.Read().pending.empty());
    auto id = controls.Submit(Action::SetVolume, 30);
    CHECK(controls.Read().runtime.volume_percent == 100);
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(request.value == 30);
    CHECK(controls.Finish(id, Phase::Succeeded, "Volume applied."));
    CHECK(controls.Read().runtime.volume_percent == 100);  // Only Publish changes observed state.
}

static void CompletedPauseDoesNotUseStaleUiState() {
    Controls controls;
    controls.Publish(Capabilities(), {}); // playing snapshot on the main owner
    const auto pause = controls.Submit(Action::TogglePause);
    Controls::Request request;
    CHECK(controls.TakeNext(request) && request.id == pause);
    CHECK(controls.Finish(pause, Phase::Succeeded, "Game paused."));
    // The game owner has paused; the main loop has not published again.
    CHECK(!controls.Read().runtime.paused);
    const auto step = controls.Submit(Action::Step);
    CHECK(controls.TakeNext(request) && request.id == step && request.action == Action::Step);
    CHECK(controls.Finish(step, Phase::Succeeded, "Advanced one boundary."));
}

static void CloseCannotBeStarvedByPendingSave() {
    Controls controls;
    controls.Publish(Capabilities(), {});
    const auto save = controls.Submit(Action::SaveReproduction);
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(controls.Waiting(save, "Waiting for a capture boundary."));
    controls.Submit(Action::SetVolume, 20);
    const auto close = controls.Submit(Action::Close);
    CHECK(controls.Read().closing);
    CHECK(controls.TakeNext(request));
    CHECK(request.action == Action::Close && request.id == close);
    CHECK(!controls.TakeNext(request));
    controls.Submit(Action::Close);
    CHECK(controls.Read().recent.back().phase == Phase::Refused);
    CHECK(controls.Finish(close, Phase::Succeeded, "Shutdown requested."));
    controls.CancelPending("Session closed before this action completed.");
    CHECK(controls.Read().pending.empty());
    CHECK(controls.Read().recent.back().phase == Phase::Cancelled);
    CHECK(!controls.Finish(save, Phase::Succeeded, "Late callback"));
    controls.Submit(Action::SaveReproduction);
    CHECK(controls.Read().pending.empty());
    CHECK(controls.Read().recent.back().phase == Phase::Refused);
}

static void BoundedConcurrentRequests() {
    Controls controls;
    controls.Publish(Capabilities(), {});
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&] {
            for (int j = 0; j < 100; ++j) controls.Submit(Action::SaveReproduction);
        });
    }
    for (auto& producer : producers) producer.join();
    auto view = controls.Read();
    CHECK(view.pending.size() == 1);
    CHECK(view.recent.size() == Controls::kHistoryLimit);
    CHECK(view.recent.back().request.id == 400);
    CHECK(view.pending.front().request.id == 1);
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(controls.Waiting(request.id, std::string(Controls::kTextLimit * 2, 'x')));
    CHECK(controls.Read().pending.front().message.size() == Controls::kTextLimit);
    CHECK(controls.Finish(request.id, Phase::Succeeded, "Written."));
    CHECK(!controls.Finish(request.id, Phase::Succeeded, "Duplicate completion."));
}

static void RestoreSelectionSharedWithHotkey() {
    Controls controls;
    Controls::RuntimeState state;
    state.restore_path = "first.ps2st";
    controls.Publish(Capabilities(), state);
    CHECK(controls.Read().restore_path == "first.ps2st");
    CHECK(controls.SelectRestorePath("chosen.ps2st"));
    controls.Publish(Capabilities(), state);
    CHECK(controls.Read().restore_path == "chosen.ps2st");
    CHECK(!controls.SelectRestorePath(std::string("bad\0path", 8)));
    CHECK(!controls.SelectRestorePath(std::string(Controls::kTextLimit + 1, 'x')));
    CHECK(controls.HandleFunctionKey(8, true, false, false));
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(request.path == "chosen.ps2st");
    CHECK(controls.Finish(request.id, Phase::Failed, "Invalid reproduction save."));
    state.restore_path = "just-written.ps2st";
    controls.Publish(Capabilities(), state);
    CHECK(controls.Read().restore_path == "just-written.ps2st");
}

// U4/U5 + the s3 play session's silent F5: every F5/F8 result is delivered to
// the installed observer OUTSIDE the lock -- the Queued acknowledgement the
// moment the key press is admitted, the admission refusal, the terminal
// result from Finish, and the shutdown cancellation. The observer reads the
// controls back (a deadlock here would be a lock held across the callback).
static void ObserverIsLoudOutsideTheLock() {
    Controls controls;
    std::vector<Controls::Result> seen;
    controls.SetObserver([&](const Controls::Result& result) {
        seen.push_back(result);
        (void)controls.Read();   // must not deadlock: delivered outside the lock
    });
    controls.Publish(Capabilities(), {});
    // Queued acknowledgement.
    const auto save = controls.Submit(Action::SaveReproduction);
    CHECK(seen.size() == 1 && seen.back().request.id == save && seen.back().phase == Phase::Queued);
    // Terminal result from the game owner.
    Controls::Request request;
    CHECK(controls.TakeNext(request) && request.id == save);
    CHECK(controls.Waiting(save, "graphics: Graphics owned pending=2,failed=0"));
    CHECK(seen.size() == 1);   // Waiting is a progress phase, shown in the overlay, not re-announced
    CHECK(controls.Finish(save, Phase::Refused,
        "No admissible boundary in 120 consecutive boundaries; the last owner to refuse: graphics: Graphics owned pending=2,failed=0"));
    CHECK(seen.size() == 2 && seen.back().phase == Phase::Refused);
    CHECK(seen.back().message.find("the last owner to refuse: graphics: Graphics owned pending=2,failed=0") != std::string::npos);
    CHECK(controls.MenuOpen());   // the refusal also opens the menu (the on-screen route)
    // Admission refusal (unsupported action) reaches the observer too.
    Controls::Capabilities none;
    controls.Publish(none, {});
    const auto refused = controls.Submit(Action::RestoreReproduction, 0, "x.ps2st");
    CHECK(seen.size() == 3 && seen.back().request.id == refused && seen.back().phase == Phase::Refused);
    CHECK(seen.back().message == "This action is unavailable in this session.");
    // Cancellation at shutdown.
    controls.Publish(Capabilities(), {});
    const auto late = controls.Submit(Action::SaveReproduction);
    CHECK(seen.size() == 4 && seen.back().phase == Phase::Queued);
    controls.CancelPending("Session closing.");
    CHECK(seen.size() == 5 && seen.back().request.id == late && seen.back().phase == Phase::Cancelled);
    // Replacing the observer with nothing silences the route without breaking it.
    controls.SetObserver({});
    CHECK(controls.Submit(Action::ToggleMenu) != 0 && seen.size() == 5);
}

// F8 with nothing selected: the root's fallback (the newest slot) supplies the
// path; when it has none its reason is the refusal, never a silent no-op.
static void RestoreFallsBackToTheLatestSlot() {
    Controls controls;
    controls.Publish(Capabilities(), {});
    std::string reason_given;
    std::string fallback_path;
    controls.SetRestoreFallback([&](std::string* reason) {
        if (fallback_path.empty() && reason) *reason = reason_given;
        return fallback_path;
    });
    reason_given = "No savestate slot (slot<N>.ps2st) under /tmp/x/savestates -- press F5 first.";
    const auto none = controls.Submit(Action::RestoreReproduction);
    CHECK(controls.Read().recent.back().request.id == none && controls.Read().recent.back().phase == Phase::Refused);
    CHECK(controls.Read().recent.back().message == reason_given);
    fallback_path = "/tmp/x/savestates/slot3.ps2st";
    CHECK(controls.HandleFunctionKey(8, true, false, false));
    Controls::Request request;
    CHECK(controls.TakeNext(request) && request.action == Action::RestoreReproduction);
    CHECK(request.path == fallback_path);
    CHECK(controls.Finish(request.id, Phase::Working, "accepted") == false);   // non-terminal: Waiting() is the route
    CHECK(controls.Finish(request.id, Phase::Succeeded, "Restarting."));
    // An explicit browser selection still wins over the fallback.
    CHECK(controls.SelectRestorePath("chosen.ps2st"));
    const auto chosen = controls.Submit(Action::RestoreReproduction);
    CHECK(controls.TakeNext(request) && request.id == chosen && request.path == "chosen.ps2st");
}

#if PS2R_PRODUCT_GUI_TESTS
static void NoticesStayVisibleAndExpireWithoutCapturingInput() {
    Controls controls;
    controls.Publish(Capabilities(), {});
    ps2r::app::SessionPanel panel;
    CHECK(!panel.WantsFrame(controls.Read(), 10.0));
    const auto save = controls.Submit(Action::SaveReproduction);
    CHECK(panel.WantsFrame(controls.Read(), 10.0));
    CHECK(!controls.MenuOpen());
    Controls::Request request;
    CHECK(controls.TakeNext(request));
    CHECK(panel.WantsFrame(controls.Read(), 90.0));
    CHECK(controls.Finish(save, Phase::Succeeded, "Reproduction written."));
    CHECK(panel.WantsFrame(controls.Read(), 100.0));
    CHECK(panel.WantsFrame(controls.Read(), 105.0));
    CHECK(!panel.WantsFrame(controls.Read(), 106.0));
    controls.Submit(Action::ToggleMenu);
    CHECK(panel.WantsFrame(controls.Read(), 200.0));
    controls.Submit(Action::ToggleMenu);
    CHECK(!panel.WantsFrame(controls.Read(), 201.0));
}

static SDL_Event KeyEvent(Uint32 window, SDL_Keycode key, bool down, bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = window;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    event.key.repeat = repeat;
    return event;
}

static bool WriteScreenshot(const char* path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width * height * 4));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    bool ok = std::fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const auto offset = static_cast<size_t>((y * width + x) * 4);
            if (std::fwrite(pixels.data() + offset, 1, 3, file) != 3) ok = false;
        }
    }
    return std::fclose(file) == 0 && ok;
}

static void GuiOnRealSdlAndGlOwners(const char* screenshot) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_Window* window = SDL_CreateWindow("Session menu test", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 900, 850, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window != nullptr);
    if (!window) { SDL_Quit(); return; }
    void* gl_context = SDL_GL_CreateContext(window);
    CHECK(gl_context != nullptr);
    if (!gl_context) { SDL_DestroyWindow(window); SDL_Quit(); return; }
    std::string gl_error;
    CHECK(ps2r::platform::GlContext::LoadEntryPoints(&gl_error));
    {
        Controls controls;
        Controls::RuntimeState state;
        state.frame = 420;
        state.paused = true;
        state.recording = true;
        state.input_name = "Keyboard and mouse";
        state.recording_path = "play-session/input.padlog";
        state.restore_path = "reproductions/selected.ps2st";
        state.game_saves_path = "progress";
        controls.Publish(Capabilities(), state);
        ps2r::app::SessionGui gui(controls);
        std::string error;
        CHECK(gui.Initialize(window, gl_context, error));
        CHECK(error.empty());
        CHECK(!gui.Initialize(window, gl_context, error));
        CHECK(!error.empty());
        CHECK(!gui.BuildFrame());
        const auto id = SDL_GetWindowID(window);
        auto route = gui.ProcessEvent(KeyEvent(id + 1, SDLK_F1, true));
        CHECK(!route.consumed && !controls.MenuOpen());
        route = gui.ProcessEvent(KeyEvent(id, SDLK_F1, true));
        CHECK(route.accepted && route.consumed && route.capture_keyboard && route.capture_mouse);
        CHECK(controls.MenuOpen());
        CHECK(gui.BuildFrame());
        CHECK(gui.BuildFrame());

        // A real button click reaches the same command as F9. Synthetic SDL
        // coordinates target the fixed test window's Resume button.
        SDL_Event mouse{};
        mouse.type = SDL_MOUSEMOTION;
        mouse.motion.windowID = id;
        mouse.motion.x = 80;
        mouse.motion.y = 89;
        CHECK(gui.ProcessEvent(mouse).consumed);
        CHECK(gui.BuildFrame());
        mouse = {};
        mouse.type = SDL_MOUSEBUTTONDOWN;
        mouse.button.windowID = id;
        mouse.button.button = SDL_BUTTON_LEFT;
        mouse.button.state = SDL_PRESSED;
        mouse.button.x = 80;
        mouse.button.y = 89;
        CHECK(gui.ProcessEvent(mouse).consumed);
        CHECK(gui.BuildFrame());
        mouse.type = SDL_MOUSEBUTTONUP;
        mouse.button.state = SDL_RELEASED;
        CHECK(gui.ProcessEvent(mouse).consumed);
        CHECK(gui.BuildFrame());
        Controls::Request pause_request;
        CHECK(controls.TakeNext(pause_request));
        CHECK(pause_request.action == Action::TogglePause);
        CHECK(controls.Finish(pause_request.id, Phase::Succeeded, "Pause action acknowledged by test owner."));

        route = gui.ProcessEvent(KeyEvent(id, SDLK_F5, true, true));
        CHECK(route.consumed && controls.Read().pending.empty());
        route = gui.ProcessEvent(KeyEvent(id, SDLK_F5, false));
        CHECK(route.consumed && controls.Read().pending.empty());
        route = gui.ProcessEvent(KeyEvent(id, SDLK_F5, true));
        CHECK(route.consumed && controls.Read().pending.size() == 1);
        Controls::Request request;
        CHECK(controls.TakeNext(request));
        CHECK(request.action == Action::SaveReproduction);
        CHECK(controls.Waiting(request.id, "Waiting for a safe capture boundary."));
        CHECK(gui.BuildFrame());

        // Capture has no GL calls: main builds the UI while the context is
        // detached, then the render owner draws it. Wrong-owner SDL calls fail.
        CHECK(SDL_GL_MakeCurrent(window, nullptr) == 0);
        CHECK(gui.BuildFrame());
        bool wrong_event = true, wrong_build = true, wrong_shutdown = true, rendered = false;
        bool current_ok = false, state_preserved = false;
        std::thread renderer([&] {
            wrong_event = gui.ProcessEvent(KeyEvent(id, SDLK_F9, true)).accepted;
            wrong_build = gui.BuildFrame();
            std::string thread_error;
            wrong_shutdown = gui.Shutdown(thread_error);
            current_ok = SDL_GL_MakeCurrent(window, gl_context) == 0;
            if (!current_ok) return;
            glViewport(7, 8, 83, 91);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glClearColor(0.04f, 0.06f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rendered = gui.Render();
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            state_preserved = viewport[0] == 7 && viewport[1] == 8 && viewport[2] == 83 &&
                              viewport[3] == 91 && glIsEnabled(GL_DEPTH_TEST) && !glIsEnabled(GL_BLEND);
            glFinish();
            SDL_GL_MakeCurrent(window, nullptr);
        });
        renderer.join();
        CHECK(!wrong_event && !wrong_build && !wrong_shutdown);
        CHECK(current_ok && rendered && state_preserved);
        CHECK(!gui.Render());
        CHECK(SDL_GL_MakeCurrent(window, gl_context) == 0);
        CHECK(gui.Render());
        if (screenshot) CHECK(WriteScreenshot(screenshot, 900, 850));

        CHECK(controls.Finish(request.id, Phase::Refused, "Capture remained busy; no file was written."));
        CHECK(gui.BuildFrame());
        CHECK(gui.Render());
        // Key-up after menu close must not queue another toggle or save.
        gui.ProcessEvent(KeyEvent(id, SDLK_F1, true));
        CHECK(!controls.MenuOpen());
        CHECK(gui.BuildFrame());  // Closed-menu result notice remains visible.
        CHECK(gui.Render());
        gui.ProcessEvent(KeyEvent(id, SDLK_F1, false));
        CHECK(!controls.MenuOpen());
        // The completed game picture is bottom-up RGBA. It remains behind the
        // menu and keeps its orientation when the main presenter letterboxes.
        GLuint picture = 0;
        glGenTextures(1, &picture);
        glBindTexture(GL_TEXTURE_2D, picture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const unsigned char colors[] = {
            255, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors);
        gui.SetPicture(picture, 2, 2);
        CHECK(gui.BuildFrame() && gui.Render());
        unsigned char pixel[4]{};
        glReadPixels(100, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
        glReadPixels(800, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0);
        glReadPixels(100, 500, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255);
        gui.SetPicture(0, 0, 0);
        glDeleteTextures(1, &picture);
        SDL_Event close{};
        close.type = SDL_WINDOWEVENT;
        close.window.windowID = id + 1;
        close.window.event = SDL_WINDOWEVENT_CLOSE;
        CHECK(!gui.ProcessEvent(close).consumed);
        CHECK(!controls.Read().closing);
        close.window.windowID = id;
        CHECK(gui.ProcessEvent(close).consumed);
        CHECK(controls.TakeNext(request) && request.action == Action::Close);
        CHECK(controls.Finish(request.id, Phase::Succeeded, "Shutdown queued."));
        controls.CancelPending("Session closed.");
        CHECK(gui.Shutdown(error) && error.empty());
        CHECK(gui.Shutdown(error));
    }
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

static void InputIsSampledOnlyOnMain() {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    {
        ps2r::platform::sdl::InputSettings settings;
        settings.gamepad_enabled = false;
        PlatformInput input(settings);
        ps2r::Park wake;
        input.BindMainWake(&wake);
        CHECK(!input.Connected());
        SDL_Event key{};
        key.type = SDL_KEYDOWN;
        key.key.keysym.scancode = SDL_SCANCODE_SPACE;
        CHECK(input.HandleEvent(key));
        key.type = SDL_KEYUP;
        CHECK(input.HandleEvent(key));
        // Event-only pumps cannot consume the quick press.
        CHECK(!input.ServicePending());
        CHECK(!input.ServicePending());
        std::atomic<bool> done{false};
        PadProtocol::Input first, second;
        bool connection = false, motors = true, wrong_event = true, wrong_service = true;
        std::thread game([&] {
            wrong_event = input.HandleEvent(key);
            wrong_service = input.ServicePending();
            first = input.Sample();
            connection = input.Connected();
            second = input.Sample();
            motors = input.QueueMotors(1, 255);
            input.SetKeyboardEnabled(false);
            done.store(true);
            wake.Notify();
        });
        while (!done.load()) {
            const auto generation = wake.Generation();
            input.ServicePending();
            if (!done.load()) wake.WaitFor(1000000, generation);
        }
        game.join();
        CHECK(!wrong_event && !wrong_service);
        CHECK(first.buttons != 0);
        CHECK(second.buttons == 0);
        CHECK(connection && input.Connected());  // Same accepted sample despite a settings change.
        CHECK(!motors && !input.GamepadActive());
        CHECK(!input.KeyboardEnabled());
        CHECK(input.Sample().buttons == 0);
        CHECK(!input.Connected());
        CHECK(input.Samples() == 3);
        input.SetKeyboardEnabled(true);
        key.type = SDL_KEYDOWN;
        CHECK(input.HandleEvent(key));
        input.ResetPending();
        CHECK(input.Sample().buttons == 0);
        CHECK(input.Connected());
        CHECK(!input.QueueMotors(2, 256));
        input.SetUiCapture(true);
        CHECK(input.HandleEvent(key));
        input.SetUiCapture(false);
        CHECK(input.Sample().buttons == 0);

        done.store(false);
        bool stopped_motors = true;
        std::thread blocked([&] {
            stopped_motors = input.QueueMotors(0, 0);
            done.store(true);
            wake.Notify();
        });
        // Stop must wake a queued request even when no sample can be served.
        input.StopService();
        blocked.join();
        CHECK(done.load() && !stopped_motors);
        CHECK(input.Sample().buttons == 0);
        CHECK(!input.Connected());
    }
    SDL_Quit();
}

static void MotorRequestsUseActualSdlResults() {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    struct MotorEvidence {
        std::thread::id main = std::this_thread::get_id();
        bool on_main = true;
        int calls = 0, result = 0;
        Uint16 low = 0, high = 0;
    } evidence;
    SDL_VirtualJoystickDesc descriptor{};
    descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    descriptor.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    descriptor.naxes = SDL_CONTROLLER_AXIS_MAX;
    descriptor.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    descriptor.name = "Session motor fixture";
    descriptor.userdata = &evidence;
    descriptor.Rumble = [](void* data, Uint16 low, Uint16 high) -> int {
        auto& observation = *static_cast<MotorEvidence*>(data);
        observation.on_main &= std::this_thread::get_id() == observation.main;
        ++observation.calls;
        observation.low = low;
        observation.high = high;
        return observation.result;
    };
    const int device = SDL_JoystickAttachVirtualEx(&descriptor);
    CHECK(device >= 0);
    if (device >= 0) {
        {
            ps2r::platform::sdl::InputSettings settings;
            settings.kbm_enabled = false;
            PlatformInput input(settings);
            ps2r::Park wake;
            input.BindMainWake(&wake);
            std::atomic<bool> done{false};
            bool accepted = false;
            std::thread game([&] {
                accepted = input.QueueMotors(1, 64);
                done.store(true);
                wake.Notify();
            });
            while (!done.load()) {
                const auto generation = wake.Generation();
                input.ServicePending();
                if (!done.load()) wake.WaitFor(1000000, generation);
            }
            game.join();
            // The game owner's QueueMotors is a post (Charter 3 item 4: never a wait
            // on main): accepted = a live gamepad can take it; the owner's next
            // ServicePending applies the coalesced value on main.
            CHECK(accepted);
            input.ServicePending();
            CHECK(evidence.on_main && evidence.calls > 0);
            CHECK(evidence.low == 64 * 257 && evidence.high == 65535);
            evidence.result = -1;
            CHECK(!input.QueueMotors(0, 65));
            evidence.result = 0;
            input.ResetPending();
            CHECK(evidence.low == 0 && evidence.high == 0);
        }
        CHECK(SDL_JoystickDetachVirtual(device) == 0);
    }
    SDL_Quit();
}

template <typename Predicate>
static bool Await(Predicate predicate) {
    ps2r::Park wait;
    const auto deadline = ps2r::Park::NowNs() + 3000000000ull;
    while (!predicate()) {
        if (ps2r::Park::NowNs() >= deadline) return false;
        wait.WaitFor(1000000, wait.Generation());
    }
    return true;
}

static bool Completed(const Controls& controls, uint64_t id, Phase phase) {
    for (const auto& result : controls.Read().recent)
        if (result.request.id == id) return result.phase == phase;
    return false;
}

// The s5 F5: a REAL SDL key event through the production route -- SDL's own
// queue, SessionRuntime::Run's main pump (SDL_PollEvent), SessionWindow,
// SessionGui::ProcessEvent, SessionControls -- never GuiShell::ProcessEvent
// called by hand. Case 1 stamps the press the way SDL stamps a physical press
// when it recorded no keyboard-focus window (windowID = 0: the --harness-window
// play run's trace showed SDL_GetKeyboardFocus() == NULL for the whole
// session); case 2 stamps the session window's own id; case 3 uses the
// product's InjectFunctionKey (the `harness.inject_key` route). Every press
// must reach SessionControls as a SaveReproduction request (the observer's
// Queued result names the action) -- RED before the fix for case 1: the strict
// `windowID == ours` test in SessionGui discarded it silently.
static void RealSdlKeyEventReachesSessionControls() {
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    {
        ps2r::platform::sdl::InputSettings settings;
        settings.gamepad_enabled = false;
        PlatformInput input(settings);
        ps2r::app::SessionRuntime runtime(&input);
        std::string error;
        CHECK(runtime.Open(80, true, false, "", error));
        if (!error.empty()) { std::printf("%s\n", error.c_str()); SDL_Quit(); return; }
        // A capture hook makes SaveReproduction supported; it is refused at the
        // boundary (the proof is the ARRIVAL at SessionControls, not a write).
        ps2r::app::SessionRuntime::Hooks hooks;
        hooks.capture = [&](const Controls::Request&, uint64_t) {
            return ps2r::app::SessionRuntime::CaptureResult{Phase::Refused, "No writer in this pin."};
        };
        runtime.SetHooks(std::move(hooks));
        std::atomic<int> queued{0};   // SaveReproduction presses SessionControls admitted
        runtime.Controls().SetObserver([&](const Controls::Result& result) {
            if (result.request.action == Action::SaveReproduction && result.phase == Phase::Queued) ++queued;
        });
        // The shape SDL_SendKeyboardKey gives a physical F5 (down, then up).
        auto push_f5 = [&](Uint32 window_id) {
            SDL_Event event{};
            event.type = SDL_KEYDOWN;
            event.key.windowID = window_id;
            event.key.state = SDL_PRESSED;
            event.key.keysym.scancode = SDL_SCANCODE_F5;
            event.key.keysym.sym = SDLK_F5;
            CHECK(SDL_PushEvent(&event) == 1);
            event.type = SDL_KEYUP;
            event.key.state = SDL_RELEASED;
            CHECK(SDL_PushEvent(&event) == 1);
            runtime.Wake().Notify();
        };
        bool no_focus = false, own_window = false, injected = false, idle = true;
        auto& controls = runtime.Controls();
        auto settle = [&](uint64_t& frame) {   // boundaries until the refused press is terminal
            return Await([&] { runtime.Boundary(++frame); return controls.Read().pending.empty(); });
        };
        const int code = runtime.Run([&]() -> int {
            uint64_t frame = 0;
            // Case 1 (the s5 shape): no focus window recorded -> windowID 0.
            push_f5(0);
            no_focus = Await([&] { runtime.Boundary(++frame); return queued.load() == 1; });
            idle &= settle(frame);
            // Case 2: the press stamped with the session window's own id.
            Uint32 ours = 0;
            for (Uint32 id = 1; id < 16 && !ours; ++id)
                if (SDL_Window* w = SDL_GetWindowFromID(id)) ours = SDL_GetWindowID(w);
            push_f5(ours);
            own_window = Await([&] { runtime.Boundary(++frame); return queued.load() == 2; });
            idle &= settle(frame);
            // Case 3: the harness's synthetic press (SDL stamp of the moment).
            CHECK(runtime.InjectFunctionKey(5));
            injected = Await([&] { runtime.Boundary(++frame); return queued.load() == 3; });
            idle &= settle(frame);
            runtime.RequestClose();
            return 0;
        }, 5);
        CHECK(code == 0);
        CHECK(no_focus);     // RED before the fix: discarded at SessionGui's window-id test
        CHECK(own_window);
        CHECK(injected);
        CHECK(idle);
        CHECK(queued.load() == 3);
        const auto keys = runtime.Keys();
        CHECK(keys.key_events == 6 && keys.function_keys == 3);
        CHECK(keys.discarded == 0);   // the LOUD `nativeee: F5 discarded:` line never fired
        std::printf("key route: key_events=%llu function_keys=%llu discarded=%llu queued=%d no_focus=%d own_window=%d injected=%d\n",
                    (unsigned long long)keys.key_events, (unsigned long long)keys.function_keys,
                    (unsigned long long)keys.discarded, queued.load(), no_focus, own_window, injected);
    }
    SDL_Quit();
}

// No title code runs here. Exercise the actual main pump against a synthetic
// game owner, including the relaunch shape where the boundary is a different
// worker from the thread supervising/joining the session.
static void RuntimeKeepsMainResponsive(bool separate_boundary_worker) {
    const int original_rounding = std::fegetround();
    const int host_rounding = separate_boundary_worker ? FE_DOWNWARD : FE_TONEAREST;
    CHECK(std::fesetround(host_rounding) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    {
        ps2r::platform::sdl::InputSettings settings;
        settings.gamepad_enabled = false;
        PlatformInput input(settings);
        ps2r::app::SessionRuntime runtime(&input);
        std::string error;
        CHECK(runtime.Open(80, true, true, "existing-recorder/input.padlog", error));
        if (!error.empty()) {
            std::printf("%s\n", error.c_str());
            std::fesetround(original_rounding);
            return;
        }
        const auto main_thread = std::this_thread::get_id();
        std::thread::id game_thread;
        bool handlers_on_owner = true, dispatch_on_main = false, cleanup_off_main = false;
        bool main_rounding_kept = false, guest_rounding_kept = true;
        int capture_calls = 0;
        std::string capture_path;
        std::atomic<Uint32> window_id{0};
        ps2r::app::SessionRuntime::Hooks hooks;
        hooks.pause = [&](bool) { handlers_on_owner &= std::this_thread::get_id() == game_thread; };
        hooks.capture = [&](const Controls::Request& request, uint64_t) {
            handlers_on_owner &= std::this_thread::get_id() == game_thread;
            ++capture_calls;
            capture_path = request.path;
            return ps2r::app::SessionRuntime::CaptureResult{
                Phase::Waiting, "Unsubmitted graphics work; no file written."};
        };
        hooks.stopped = [&] { cleanup_off_main = std::this_thread::get_id() != main_thread; };
        runtime.SetHooks(std::move(hooks));
        auto& controls = runtime.Controls();
        const auto pause = controls.Submit(Action::TogglePause);
        bool sequence_ok = true;
        std::thread director([&] {
            sequence_ok = Await([&] {
                return Completed(controls, pause, Phase::Succeeded) && controls.Read().runtime.paused;
            });
            if (!sequence_ok) { runtime.RequestClose(); return; }
            const auto frame_before_step = controls.Read().runtime.frame;
            const auto keyboard = controls.Submit(Action::SetKeyboardInput, 0);
            const auto volume = controls.Submit(Action::SetVolume, 37);
            const auto capture = controls.Submit(Action::SaveReproduction, 0, "requested-reproduction");
            const auto restore = controls.Submit(Action::RestoreReproduction, 0, "unavailable");
            sequence_ok &= Await([&] {
                return Completed(controls, keyboard, Phase::Succeeded) &&
                    Completed(controls, volume, Phase::Succeeded) &&
                    Completed(controls, capture, Phase::Refused) &&
                    Completed(controls, restore, Phase::Refused);
            });
            sequence_ok &= controls.Read().runtime.frame == frame_before_step;
            sequence_ok &= runtime.Volume() == 37 && !input.KeyboardEnabled();
            sequence_ok &= Await([&] {
                return controls.Read().runtime.input_name == "No enabled input device";
            });
            // The live SDL hotkey, with repeat/up ignored, advances one boundary.
            auto key = KeyEvent(window_id.load(), SDLK_F10, true);
            sequence_ok &= SDL_PushEvent(&key) == 1;
            key.key.repeat = 1;
            sequence_ok &= SDL_PushEvent(&key) == 1;
            key.type = SDL_KEYUP;
            sequence_ok &= SDL_PushEvent(&key) == 1;
            sequence_ok &= Await([&] {
                for (const auto& result : controls.Read().recent)
                    if (result.request.action == Action::Step && result.phase == Phase::Succeeded)
                        return controls.Read().runtime.frame == frame_before_step + 1;
                return false;
            });
            sequence_ok &= controls.Read().runtime.paused;
            // Settings and owner dispatch are still serviced during the pause.
            bool main_operation = false;
            sequence_ok &= runtime.OnMain([&] { main_operation = std::this_thread::get_id() == main_thread; });
            sequence_ok &= main_operation;
            const auto resume = controls.Submit(Action::TogglePause);
            sequence_ok &= Await([&] {
                return Completed(controls, resume, Phase::Succeeded) && !controls.Read().runtime.paused &&
                    controls.Read().runtime.frame > frame_before_step + 1;
            });
            const auto repause = controls.Submit(Action::TogglePause);
            sequence_ok &= Await([&] {
                return Completed(controls, repause, Phase::Succeeded) && controls.Read().runtime.paused;
            });
            SDL_Event close{};
            close.type = SDL_WINDOWEVENT;
            close.window.windowID = window_id.load();
            close.window.event = SDL_WINDOWEVENT_CLOSE;
            sequence_ok &= SDL_PushEvent(&close) == 1;
            if (!Await([&] { return runtime.Closing(); })) {
                sequence_ok = false;
                runtime.RequestClose();
            }
        });
        auto boundary_worker = [&] {
            game_thread = std::this_thread::get_id();
            Ps2Float::SetGuestThreadRounding();
            runtime.OnMain([&] {
                dispatch_on_main = std::this_thread::get_id() == main_thread;
                main_rounding_kept = std::fegetround() == host_rounding;
                window_id.store(SDL_GetWindowID(SDL_GL_GetCurrentWindow()));
            });
            for (uint64_t frame = 0; !runtime.Closing(); ++frame) {
                guest_rounding_kept &= std::fegetround() == FE_TOWARDZERO;
                input.Sample();
                runtime.PostFrame(std::vector<uint8_t>(4 * 4 * 4, 127), 4, 4);
                if (!runtime.Boundary(frame)) break;
            }
            guest_rounding_kept &= std::fegetround() == FE_TOWARDZERO;
        };
        // Reproduce root binding setup changing main after session creation.
        Ps2Float::SetGuestThreadRounding();
        CHECK(std::fegetround() == FE_TOWARDZERO);
        const int result = runtime.Run([&] {
            if (separate_boundary_worker) {
                std::thread boundary(boundary_worker);
                boundary.join();
            } else boundary_worker();
            return 17;
        }, 3);
        director.join();
        CHECK(result == 17);
        CHECK(sequence_ok);
        CHECK(game_thread != main_thread && handlers_on_owner && dispatch_on_main && cleanup_off_main);
        CHECK(main_rounding_kept && guest_rounding_kept);
        CHECK(std::fegetround() == host_rounding);
        CHECK(capture_calls == 1 && capture_path == "requested-reproduction");
        CHECK(input.Samples() >= 2);
        CHECK(controls.Read().pending.empty() && controls.Read().closing);
        CHECK(!controls.Read().capabilities.restore_reproduction);
        CHECK(controls.Read().runtime.recording_path == "existing-recorder/input.padlog");
        bool late_dispatch = true;
        std::thread late([&] { late_dispatch = runtime.OnMain([] {}); });
        late.join();
        CHECK(!late_dispatch);
    }
    SDL_Quit();
    CHECK(std::fesetround(original_rounding) == 0);
}

static int StuckWorkerShutdown() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    ps2r::app::SessionRuntime runtime(nullptr);
    std::string error;
    if (!runtime.Open(100, false, false, {}, error)) return 3;
    runtime.Controls().Submit(Action::Close);
    return runtime.Run([] {
        ps2r::Park never_notified;
        never_notified.Wait(never_notified.Generation());
        return 4;
    }, 1);
}
#endif

#if PS2R_PRODUCT_CHECKPOINT_TESTS
// Test ABI fixture only; no generated title code is compiled or executed.
// Like the generated ABI, these accessors can see ONLY the calling thread.
static thread_local ThreadCapture::Chain capture_fixture_stack;
static thread_local bool capture_fixture_complete = true;
extern "C" uint32_t ee_get_call_stack_depth() { return static_cast<uint32_t>(capture_fixture_stack.size()); }
extern "C" bool ee_get_call_stack_top(uint32_t n, uint32_t* function, uint32_t* ra) {
    if (n >= capture_fixture_stack.size()) return false;
    const auto& frame = capture_fixture_stack[capture_fixture_stack.size() - 1 - n];
    *function = frame.func_addr;
    *ra = frame.ra;
    return true;
}
extern "C" bool ee_call_stack_push(uint32_t function, uint32_t ra) {
    if (capture_fixture_stack.size() == ThreadSection::kMaxChain) return false;
    capture_fixture_stack.push_back({function, ra});
    return true;
}
extern "C" void ee_call_stack_pop() { capture_fixture_stack.pop_back(); }

static bool LiveFixtureEntry(uint32_t function, uint32_t entry) {
    return (function == 0x1100 && entry == 0x1118) ||
        (function == 0x2200 && (entry == 0x2228 || entry == 0x2218)) ||
        (function == 0x3300 && entry == 0) ||
        (function == 0x4400 && entry == 0x4448) ||
        (function == 0x5500 && entry == 0x5518) ||
        (function == 0x6600 && (entry == 0x6668 || entry == 0x6618));
}

static ThreadCapture::Proofs LiveFixtureProofs() {
    return {LiveFixtureEntry, [] { return capture_fixture_complete; },
        [](uint32_t function, int type) { return function == 0x3300 && type == TSW_SLEEP; }};
}

static bool WaitForCapturePark(int tid) {
    const auto until = ps2r::Park::NowNs() + 3000000000ull;
    while (ps2r::Park::NowNs() < until) {
        EeRunToken::Instance().PreemptPoint();
        const auto info = ThreadManager::Instance().Lookup(tid);
        if (info) {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->capture_parked) return true;
        }
        // A restarted host worker may not have entered its ready slot yet.
        // Exercise the existing scheduler; do not change its election rules.
        std::this_thread::yield();
    }
    return false;
}

static void FreshWorkersAndIncarnations() {
    auto& threads = ThreadManager::Instance();
    auto& token = EeRunToken::Instance();
    threads.ResetForTest();
    token.ResetForTest();
    threads.SetStackBytes(2u << 20);
    ThreadSection historical(threads);
    ThreadSection::Record stale;
    stale.tid = 1;
    stale.started = stale.has_ctx = true;
    stale.ctx.cpuRegs.GPR.r[2].UL[0] = 999;
    stale.chain = {{0xdead, 0}};
    std::vector<uint8_t> image;
    ThreadSection::Emit({stale}, image);
    std::string error;
    CHECK(historical.Restore(image, &error));
    CpuContext boundary{};
    boundary.cpuRegs.GPR.r[31].UL[0] = 0x1118;
    boundary.cpuRegs.GPR.r[2].UL[0] = 1;
    const auto main_info = threads.AdoptCurrentThread(1, 64);
    main_info->ctx = &boundary;
    capture_fixture_stack = {{0x2200, 0}, {0x1100, 0x2228}};
    token.GuestEnter(1, 64);
    {
        int installed_entries = 1;
        auto proofs = LiveFixtureProofs();
        proofs.can_resume = [&](uint32_t function, uint32_t entry) {
            if (installed_entries < 0) throw 7;
            return installed_entries && LiveFixtureEntry(function, entry);
        };
        ThreadCapture capture(threads, std::move(proofs));
        std::atomic<int> runs{0};
        threads.SetGuestBody([&](int tid, CpuContext& context) {
            const auto argument = context.cpuRegs.GPR.r[4].UL[0];
            context.cpuRegs.GPR.r[2].UL[0] = 100 + argument;
            capture_fixture_complete = argument != 2;
            capture_fixture_stack = {{0x4400, 0}, {0x3300, 0x4448}};
            ++runs;
            threads.SleepThread();
            if (argument == 4) threads.SuspendThread(tid);
            capture_fixture_stack.clear();
        });
        ThreadParam parameter;
        parameter.priority = 10; // StartThread yields until this worker parks.
        const int tid = threads.CreateThread(parameter);
        CHECK(tid > 1 && threads.StartThread(tid, 1, 0, 0) == KE_OK);
        CHECK(WaitForCapturePark(tid));
        const auto peer = threads.Lookup(tid);
        const auto first_incarnation = peer->capture_incarnation;
        ThreadCapture::Records fresh;
        CHECK(capture.CaptureBoundary(1, boundary, fresh, error));
        CHECK(fresh.size() == 2 && fresh[0].has_ctx && fresh[1].has_ctx);
        CHECK(fresh[0].ctx.cpuRegs.GPR.r[2].UL[0] == 1 && fresh[0].chain[0].func_addr == 0x1100);
        CHECK(fresh[1].ctx.cpuRegs.GPR.r[2].UL[0] == 101 && fresh[1].chain[0].func_addr == 0x3300);
        const auto first = fresh;
        // Drive the real capture provider into the real state-file writer.
        ps2r::savestate::SectionRegistry registry;
        historical.Register(registry);
        ps2r::app::SessionCheckpoint checkpoint(registry, {
            [&] { return token.CanCaptureCurrentThread(); },
            [](std::string&) { return true; },
            [&](auto& out, std::string& reason) { return capture.CaptureBoundary(1, boundary, out, reason); },
            LiveFixtureEntry
        });
        const std::string directory_s = ps2r::host::MakeTempDirectory("thread_capture_", nullptr);
        CHECK(!directory_s.empty());
        const char* directory = directory_s.c_str();
        const std::string path = std::string(directory) + "/fresh.ps2st";
        CHECK(checkpoint.Capture(1, path, {}).phase == Phase::Working);
        checkpoint.Join();
        const auto written = checkpoint.Poll();
        CHECK(written && written->phase == Phase::Succeeded);
        ps2r::savestate::FileMeta meta;
        std::vector<ps2r::savestate::Blob> decoded;
        CHECK(ps2r::savestate::ReadStateFile(path, &meta, &decoded, &error));
        CHECK(ThreadSection::Parse(decoded[0].payload, fresh, &error));
        CHECK(fresh[1].ctx.cpuRegs.GPR.r[2].UL[0] == 101 && fresh[1].has_ctx);
        std::filesystem::remove_all(directory);
        bool foreign_accepted = true;
        std::thread foreign([&] {
            ThreadCapture::Records result = first;
            std::string reason;
            foreign_accepted = capture.CaptureBoundary(1, boundary, result, reason) || !result.empty();
        });
        foreign.join();
        CHECK(!foreign_accepted);
        capture_fixture_complete = false;
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && fresh.empty());
        capture_fixture_complete = true;
        capture_fixture_stack.front().ra = 0x2228; // Cold boot bypassed the root dispatch.
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) &&
              error.find("root entry") != std::string::npos && fresh.empty());
        capture_fixture_stack.front().ra = 0;
        installed_entries = 0;
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && fresh.empty());
        CHECK(error.find("Thread 1:") != std::string::npos &&
              error.find("fn=0x00001100, entry=0x00001118") != std::string::npos);
        installed_entries = -1;
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && fresh.empty());
        installed_entries = 1;
        // A real pending wake must not be saved as an unreleased sleep.
        CHECK(threads.ChangeThreadPriority(1, 0) == 64);
        CHECK(threads.WakeupThread(tid) == KE_OK);
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && fresh.empty());
        CHECK(threads.ChangeThreadPriority(1, 64) == 0);
        CHECK(capture.CaptureBoundary(1, boundary, fresh, error));
        CHECK(!fresh[1].started && !fresh[1].has_ctx && fresh[1].chain.empty());
        CHECK(threads.StartThread(tid, 2, 0, 0) == KE_OK);
        CHECK(WaitForCapturePark(tid));
        CHECK(peer->capture_incarnation > first_incarnation);
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && error.find("omitted") != std::string::npos);
        CHECK(threads.WakeupThread(tid) == KE_OK);
        CHECK(threads.StartThread(tid, 3, 0, 0) == KE_OK);
        CHECK(WaitForCapturePark(tid));
        CHECK(capture.CaptureBoundary(1, boundary, fresh, error));
        CHECK(fresh[1].ctx.cpuRegs.GPR.r[2].UL[0] == 103);
        CHECK(first[1].ctx.cpuRegs.GPR.r[2].UL[0] == 101); // Immutable prior image.
        CHECK(threads.WakeupThread(tid) == KE_OK);
        CHECK(threads.StartThread(tid, 4, 0, 0) == KE_OK);
        CHECK(WaitForCapturePark(tid));
        CHECK(threads.WakeupThread(tid) == KE_OK); // Worker now self-suspends.
        {
            std::lock_guard<std::mutex> lock(peer->m);
            CHECK(peer->capture_parked && peer->capture_suspended);
        }
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error) && error.find("suspension") != std::string::npos);
        CHECK(threads.ResumeThread(tid) == KE_OK);
        CHECK(runs == 4);
        token.Shutdown();
        CHECK(!capture.CaptureBoundary(1, boundary, fresh, error));
        threads.JoinAll();
        threads.SetGuestBody({});
    }
    token.GuestExit(1);
    capture_fixture_stack.clear();
    threads.ResetForTest();
    token.ResetForTest();
}

static void WalkChainCapturesEveryDeferredFrame() {
    auto& threads = ThreadManager::Instance();
    auto& token = EeRunToken::Instance();
    threads.ResetForTest();
    token.ResetForTest();
    CpuContext context{};
    context.cpuRegs.GPR.r[31].UL[0] = 0x1118;
    threads.AdoptCurrentThread(1, 64)->ctx = &context;
    token.GuestEnter(1, 64);
    ThreadCapture capture(threads, LiveFixtureProofs());
    ThreadSection::Record saved;
    saved.tid = 1;
    saved.started = saved.has_ctx = saved.is_boundary = true;
    saved.chain = {{0x1100, 0x2228}, {0x2200, 0x6668}, {0x6600, 0}};
    saved.ctx = context;
    unsigned level = 0;
    ThreadRelauncher walker(threads, [&](uint32_t function, uint32_t entry, CpuContext& live) {
        CHECK(entry == (level == 0 ? 0x1118u : saved.chain[level - 1].ra));
        // Simulate the generated wrapper recording the internal resume RA.
        capture_fixture_stack = {{function, live.cpuRegs.GPR.r[31].UL[0]}};
        if (level == 0) capture_fixture_stack.push_back({0x5500, 0x1118});
        live.cpuRegs.GPR.r[31].UL[0] = level == 0 ? 0x5518 : function + 0x18;
        ThreadCapture::Records result;
        std::string error;
        CHECK(capture.CaptureBoundary(1, live, result, error));
        CHECK(result.size() == 1);
        if (result.empty()) return true;
        const auto& chain = result[0].chain;
        CHECK(chain.size() == (level == 0 ? 4 : saved.chain.size() - level));
        const size_t active_level = level == 0 ? 1 : 0;
        CHECK(chain[active_level].func_addr == function && chain[active_level].ra == saved.chain[level].ra);
        CHECK(chain.back().func_addr == 0x6600 && chain.back().ra == 0);
        if (level == 0) CHECK(chain[0].func_addr == 0x5500 && chain[0].ra == 0x1118);
        capture_fixture_stack.clear();
        ++level;
        return true;
    });
    walker.WalkChain(saved, context);
    CHECK(level == 3);
    ThreadRelauncher interrupted(threads, [](uint32_t, uint32_t, CpuContext&) -> bool { throw 7; });
    try { interrupted.WalkChain(saved, context); } catch (int) {}
    // The last resume scope must be gone; a subsequent ordinary boundary
    // must not acquire any of that chain's old tail.
    context.cpuRegs.GPR.r[31].UL[0] = 0x1118;
    capture_fixture_stack = {{0x2200, 0}, {0x1100, 0x2228}};
    ThreadCapture::Records fresh;
    std::string error;
    CHECK(capture.CaptureBoundary(1, context, fresh, error));
    CHECK(fresh.size() == 1 && fresh[0].chain.size() == 2);
    token.GuestExit(1);
    capture_fixture_stack.clear();
    threads.ResetForTest();
    token.ResetForTest();
}

static void ProductionRestartPreservesOneOwner() {
    auto& threads = ThreadManager::Instance();
    auto& token = EeRunToken::Instance();
    auto& events = EeEventQueue::Instance();
    threads.ResetForTest();
    token.ResetForTest();
    threads.SetStackBytes(2u << 20);
    CpuContext context{};
    context.cpuRegs.GPR.r[31].UL[0] = 0x1118;
    context.cpuRegs.GPR.r[2].UL[0] = 456;
    const auto main_info = threads.AdoptCurrentThread(1, 64);
    main_info->ctx = &context;
    const auto old_incarnation = main_info->capture_incarnation;
    token.GuestEnter(1, 64);
    capture_fixture_stack = {{0x2200, 0}, {0x1100, 0x2228}};
    ThreadCapture capture(threads, LiveFixtureProofs());
    threads.SetGuestBody([&](int, CpuContext& live) {
        live.cpuRegs.GPR.r[2].UL[0] = 123;
        capture_fixture_stack = {{0x4400, 0}, {0x3300, 0x4448}};
        threads.SleepThread();
    });
    ThreadParam parameter;
    parameter.priority = 10;
    const int peer_tid = threads.CreateThread(parameter);
    CHECK(threads.StartThread(peer_tid, 0, 0, 0) == KE_OK && WaitForCapturePark(peer_tid));
    ThreadCapture::Records saved;
    std::string error;
    CHECK(capture.CaptureBoundary(1, context, saved, error));
    std::vector<uint8_t> owned_image;
    ThreadSection::Emit(saved, owned_image);
    CHECK(!threads.RestartAfterJoin(&error) && threads.Running());
    bool foreign_released = true;
    std::thread foreign([&] {
        std::string reason;
        foreign_released = threads.ReleaseCurrentThread(&reason);
    });
    foreign.join();
    CHECK(!foreign_released && main_info->ctx == &context);
    threads.TerminateAll();
    CHECK(!threads.RestartAfterJoin(&error) && error.find("joined") != std::string::npos);
    threads.JoinAll();
    CHECK(!threads.RestartAfterJoin(&error) && error.find("context") != std::string::npos);
    CHECK(threads.ReleaseCurrentThread(&error));
    CHECK(main_info->ctx == nullptr && !main_info->started && !token.CurrentThreadHoldsToken());
    // Pending old work is a hard refusal, not a silent drop into the new run.
    events.Enqueue({EeEventQueue::Event::kIntcRaise, 0, 0});
    CHECK(!threads.RestartAfterJoin(&error) && error.find("deliveries") != std::string::npos);
    CHECK(!threads.Running() && threads.Lookup(1) == main_info);
    {
        EeRunToken::HostGate old_delivery_owner;
        // Test delivery has no handler/side effect. Production root drains its
        // old deliveries on their owner before requesting the final unwind.
        events.DrainIfPendingOnHolder();
        CHECK(!threads.RestartAfterJoin(&error) && error.find("active owner") != std::string::npos);
    }
    CHECK(threads.RestartAfterJoin(&error));
    CHECK(threads.Running() && threads.SnapshotTable().empty());
    CHECK(!token.ElectionHeld() && !token.CurrentThreadHoldsToken());
    ThreadSection restored(threads);
    CHECK(restored.Restore(owned_image, &error));
    capture.ResetAfterRestore();
    std::atomic<int> resumed{0};
    bool recaptured = false;
    ThreadRelauncher walker(threads, [&](uint32_t function, uint32_t entry, CpuContext& live) {
        // Generated wrapper fixture; a resumed wrapper's RA is internal.
        capture_fixture_stack = {{function, live.cpuRegs.GPR.r[31].UL[0]}};
        if (function == 0x3300) {
            if (entry != 0) return false;
            ++resumed;
            threads.SleepThread(); // Rebuild the saved park before boundary runs.
        } else if (function == 0x1100) {
            ++resumed;
            ThreadCapture::Records fresh;
            std::string reason;
            recaptured = token.CanCaptureCurrentThread() && entry == 0x1118 &&
                capture.CaptureBoundary(1, live, fresh, reason);
            if (recaptured)
                recaptured = fresh.size() == 2 && fresh[0].chain.size() == 2 && fresh[1].chain.size() == 2 &&
                    fresh[0].ctx.cpuRegs.GPR.r[2].UL[0] == 456 &&
                    fresh[1].ctx.cpuRegs.GPR.r[2].UL[0] == 123 &&
                    threads.CurrentIncarnation() > old_incarnation &&
                    threads.Lookup(1) != main_info && main_info->ctx == nullptr;
            threads.WakeupThread(peer_tid);
        }
        capture_fixture_stack.clear();
        return true;
    });
    CHECK(walker.Relaunch(restored.Restored(), &error) == 2);
    // Synthetic workers create no further threads, so this join cannot block
    // a StartThread call; root keeps its existing supervisor protocol.
    threads.JoinAll();
    CHECK(recaptured && resumed == 2);
    CHECK(!token.AnyReadyBesides(EeRunToken::kNone));
    // Repeat the production lifecycle without ResetForTest between runs.
    CHECK(!threads.RestartAfterJoin(&error)); // Still running until explicitly stopped.
    threads.TerminateAll();
    threads.JoinAll();
    CHECK(threads.RestartAfterJoin(&error));
    CpuContext next_context{};
    auto next = threads.AdoptCurrentThread(1, 64);
    next->ctx = &next_context;
    token.GuestEnter(1, 64);
    CHECK(next->capture_incarnation > old_incarnation && token.CanCaptureCurrentThread());
    CHECK(threads.ReleaseCurrentThread(&error));
    threads.TerminateAll();
    threads.JoinAll();
    threads.SetGuestBody({});
    capture_fixture_stack.clear();
    // Test cleanup only; both actual restart cycles above used production APIs.
    threads.ResetForTest();
    token.ResetForTest();
}

static ps2r::app::SessionCheckpoint::Records FreshFixture() {
    ThreadSection::Record boundary;
    boundary.tid = 1;
    boundary.started = boundary.has_ctx = boundary.is_boundary = true;
    boundary.ctx.cpuRegs.GPR.r[31].UL[0] = 0x1118;
    boundary.ctx.cpuRegs.GPR.r[2].UL[0] = 1;  // Finalized boundary return value.
    boundary.chain = {{0x1100, 0x2228}, {0x2200, 0}};
    ThreadSection::Record sleeper;
    sleeper.tid = 2;
    sleeper.started = sleeper.has_ctx = true;
    sleeper.chain = {{0x3300, 0x4448}, {0x4400, 0}};
    return {boundary, sleeper};
}

static bool KnownFixtureEntry(uint32_t function, uint32_t entry) {
    return (function == 0x1100 && entry == 0x1118) ||
        (function == 0x2200 && entry == 0x2228) ||
        (function == 0x3300 && entry == 0) ||
        (function == 0x4400 && entry == 0x4448);
}

static void FreshContinuationAndResumeTailPins() {
    using Checkpoint = ps2r::app::SessionCheckpoint;
    std::string error;
    auto live = FreshFixture();
    CHECK(Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    live[0].ctx.cpuRegs.GPR.r[31].UL[0] = 0x5558;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    CHECK(error.find("Thread 1:") != std::string::npos &&
          error.find("fn=0x00001100, entry=0x00005558") != std::string::npos);
    live = FreshFixture();
    live[1].has_ctx = false;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    live = FreshFixture();
    live[1].tid = live[0].tid;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    live = FreshFixture();
    live[1].chain[0].ra = 0x5558;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    CHECK(error.find("Thread 2:") != std::string::npos &&
          error.find("level 1 (fn=0x00004400, entry=0x00005558)") != std::string::npos);
    live = FreshFixture();
    live[1].started = false;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    live[1].has_ctx = false;
    live[1].chain.clear();
    CHECK(Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));
    live[0].is_boundary = false;
    CHECK(!Checkpoint::ValidateContinuations(live, KnownFixtureEntry, error));

    // Save-after-restore must keep the resume dispatcher's deferred tail,
    // and the original caller RA, not the wrapper's internal re-entry RA.
    const auto saved = FreshFixture()[0].chain;
    const std::vector<ThreadSection::ChainFrame> active{{0x5500, 0x1118}, {0x1100, 0x1118}};
    std::vector<ThreadSection::ChainFrame> complete;
    CHECK(Checkpoint::ExpandResumeChain(active, saved, 0, complete, error));
    CHECK(complete.size() == 3 && complete[0].func_addr == 0x5500);
    CHECK(complete[1].func_addr == 0x1100 && complete[1].ra == 0x2228);
    CHECK(complete[2].func_addr == 0x2200 && complete[2].ra == 0);
    CHECK(!Checkpoint::ExpandResumeChain(active, saved, 1, complete, error));
    CHECK(complete.empty());
    CHECK(!Checkpoint::ExpandResumeChain(active, saved, saved.size(), complete, error));
    auto in_place = active;
    CHECK(Checkpoint::ExpandResumeChain(in_place, saved, 0, in_place, error));
    CHECK(in_place.size() == 3 && in_place[1].ra == 0x2228);
}

static void CapturedStateIsOwnedAndWriteResultsAreReal() {
    using Checkpoint = ps2r::app::SessionCheckpoint;
    using namespace ps2r::savestate;
    const std::string directory_s = ps2r::host::MakeTempDirectory("session_checkpoint_", nullptr);
    CHECK(!directory_s.empty());
    const char* directory = directory_s.c_str();
    const std::string path = std::string(directory) + "/reproduction.ps2st";
    SectionRegistry sections;
    int stale_serializes = 0, captures = 0;
    bool on_owner = false, quiescent = true, allow_validation = true, throw_capture = false;
    auto live = FreshFixture();
    std::vector<uint8_t> memory{1, 2, 3, 4};
    SectionSpec thread_spec;
    thread_spec.tag = TAG_THRD;
    thread_spec.owner = "ThreadSection fixture";
    thread_spec.serialize = [&](std::vector<uint8_t>&) { ++stale_serializes; };
    thread_spec.validate = [](const Blob& blob, std::string* error) {
        Checkpoint::Records decoded;
        return ThreadSection::Parse(blob.payload, decoded, error);
    };
    CHECK(sections.Register(thread_spec));
    SectionSpec memory_spec;
    memory_spec.tag = TAG_RAM;
    memory_spec.owner = "Memory fixture";
    memory_spec.serialize = [&](std::vector<uint8_t>& out) { out = memory; };
    memory_spec.validate = [&](const Blob&, std::string* error) {
        if (!allow_validation && error) *error = "Memory fixture validation failed.";
        return allow_validation;
    };
    CHECK(sections.Register(memory_spec));
    Checkpoint::Sources sources;
    sources.on_game_owner = [&] { return on_owner; };
    sources.quiescent = [&](std::string& reason) {
        if (!quiescent) reason = "Movie still owns active buffers after EOF.";
        return quiescent;
    };
    sources.fresh_threads = [&](Checkpoint::Records& out, std::string&) {
        ++captures;
        if (throw_capture) throw 7;
        out = live;
        return true;
    };
    sources.can_resume = KnownFixtureEntry;
    Checkpoint checkpoint(sections, std::move(sources));
    FileMeta meta;
    meta.vsync_frame = 77;
    CHECK(checkpoint.Capture(1, path, meta).phase == Phase::Refused);
    CHECK(captures == 0 && !checkpoint.Busy() && !std::filesystem::exists(path));
    on_owner = true;
    quiescent = false;
    auto result = checkpoint.Capture(2, path, meta);
    CHECK(result.phase == Phase::Waiting && result.detail.find("buffers") != std::string::npos);
    CHECK(captures == 0 && !checkpoint.Busy() && !std::filesystem::exists(path));
    quiescent = true;
    allow_validation = false;
    result = checkpoint.Capture(3, path, meta);
    CHECK(result.phase == Phase::Failed && !checkpoint.Busy());
    CHECK(!std::filesystem::exists(path) && !checkpoint.Poll());
    allow_validation = true;
    result = checkpoint.Capture(4, path, meta);
    CHECK(result.phase == Phase::Working && checkpoint.Busy());
    // The next live frame cannot change bytes already owned by the writer.
    memory[0] = 99;
    live[0].ctx.cpuRegs.GPR.r[2].UL[0] = 999;
    CHECK(checkpoint.Capture(5, path, meta).phase == Phase::Refused);
    checkpoint.Join();
    const auto done = checkpoint.Poll();
    CHECK(done && done->request == 4 && done->phase == Phase::Succeeded && done->frame == 77);
    CHECK(!checkpoint.Busy() && !checkpoint.Poll());
    FileMeta decoded_meta;
    std::vector<Blob> decoded;
    std::string error;
    CHECK(ReadStateFile(path, &decoded_meta, &decoded, &error));
    CHECK(decoded_meta.vsync_frame == 77 && decoded.size() == 2);
    CHECK(decoded[1].tag == TAG_RAM && decoded[1].payload == std::vector<uint8_t>({1, 2, 3, 4}));
    Checkpoint::Records decoded_threads;
    CHECK(ThreadSection::Parse(decoded[0].payload, decoded_threads, &error));
    CHECK(decoded_threads.size() == 2 && decoded_threads[0].has_ctx && decoded_threads[1].has_ctx);
    CHECK(decoded_threads[0].ctx.cpuRegs.GPR.r[2].UL[0] == 1);
    CHECK(decoded_threads[0].ctx.cpuRegs.GPR.r[31].UL[0] == 0x1118);
    CHECK(stale_serializes == 0);
    result = checkpoint.Capture(6, std::string(directory) + "/missing/reproduction.ps2st", meta);
    CHECK(result.phase == Phase::Working);
    checkpoint.Join();
    const auto failed = checkpoint.Poll();
    CHECK(failed && failed->request == 6 && failed->phase == Phase::Failed && !failed->detail.empty());
    CHECK(!checkpoint.Busy());
    throw_capture = true;
    CHECK(checkpoint.Capture(7, path, meta).phase == Phase::Failed && !checkpoint.Busy());
    throw_capture = false;
    SectionSpec missing_spec;
    missing_spec.tag = TAG_HEAP;
    missing_spec.required = false; // Optional does not mean safe to lose current bookkeeping.
    missing_spec.owner = "Heap fixture";
    CHECK(sections.Register(missing_spec));
    result = checkpoint.Capture(8, path, meta);
    CHECK(result.phase == Phase::Refused && result.detail.find("Heap fixture") != std::string::npos);
    CHECK(!checkpoint.Busy() && !checkpoint.Poll());
    std::filesystem::remove_all(directory);
}

#if PS2R_PRODUCT_GUI_TESTS
static void RuntimePublishesCheckpointCompletion(bool close_before_completion) {
    using Runtime = ps2r::app::SessionRuntime;
    using Checkpoint = ps2r::app::SessionCheckpoint;
    using namespace ps2r::savestate;
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    const std::string directory_s = ps2r::host::MakeTempDirectory("session_checkpoint_runtime_", nullptr);
    CHECK(!directory_s.empty());
    const char* directory = directory_s.c_str();
    const std::string path = std::string(directory) + "/reproduction.ps2st";
    {
        Runtime runtime(nullptr);
        std::string error;
        CHECK(runtime.Open(100, false, false, {}, error));
        if (!error.empty()) { std::printf("%s\n", error.c_str()); return; }
        const auto main_thread = std::this_thread::get_id();
        std::thread::id game_thread;
        SectionRegistry sections;
        SectionSpec spec;
        spec.tag = TAG_THRD;
        spec.owner = "Thread fixture";
        spec.validate = [](const Blob& blob, std::string* reason) {
            Checkpoint::Records records;
            return ThreadSection::Parse(blob.payload, records, reason);
        };
        CHECK(sections.Register(spec));
        Checkpoint checkpoint(sections, {
            [&] { return std::this_thread::get_id() == game_thread; },
            [](std::string&) { return true; },
            [](Checkpoint::Records& out, std::string&) { out = FreshFixture(); return true; },
            KnownFixtureEntry
        });
        std::atomic<bool> allow_report{false}, drained{false};
        std::atomic<uint64_t> admitted{0};
        bool callbacks_on_owner = true, sent_stale = false;
        int captures = 0;
        Runtime::Hooks hooks;
        hooks.pause = [](bool) {};
        hooks.capture = [&](const Controls::Request& request, uint64_t frame) {
            ++captures;
            FileMeta meta;
            meta.vsync_frame = static_cast<uint32_t>(frame);
            auto result = checkpoint.Capture(request.id, request.path, meta);
            admitted.store(result.request);
            return Runtime::CaptureResult{result.phase, result.detail};
        };
        hooks.poll_capture = [&]() -> std::optional<Runtime::CaptureCompletion> {
            callbacks_on_owner &= std::this_thread::get_id() == main_thread;
            if (!allow_report.load()) return {};
            if (!sent_stale) {
                sent_stale = true;
                return Runtime::CaptureCompletion{admitted.load() + 1000,
                    {Phase::Succeeded, "Stale result must not complete the current save."}, path};
            }
            const auto result = checkpoint.Poll();
            if (!result) return {};
            return Runtime::CaptureCompletion{result->request, {result->phase, result->detail}, result->path};
        };
        hooks.stopped = [&] {
            checkpoint.Join();
            drained.store(true);
            // Exercise the final completion poll: production does not withhold
            // a ready result; this fixture makes it available only after drain.
            if (close_before_completion) allow_report.store(true);
        };
        runtime.SetHooks(std::move(hooks));
        auto& controls = runtime.Controls();
        const auto pause = controls.Submit(Action::TogglePause);
        uint64_t saved_id = 0, failed_id = 0;
        bool sequence_ok = true;
        std::thread director([&] {
            sequence_ok = Await([&] {
                return Completed(controls, pause, Phase::Succeeded) && controls.Read().runtime.paused;
            });
            if (!sequence_ok) { runtime.RequestClose(); return; }
            const auto frame = controls.Read().runtime.frame;
            saved_id = controls.Submit(Action::SaveReproduction, 0, path);
            sequence_ok &= Await([&] {
                for (const auto& result : controls.Read().pending)
                    if (result.request.id == saved_id && result.phase == Phase::Waiting)
                        return admitted.load() == saved_id;
                return false;
            });
            sequence_ok &= !Completed(controls, saved_id, Phase::Succeeded);
            if (!close_before_completion) {
                allow_report.store(true);
                sequence_ok &= Await([&] {
                    return Completed(controls, saved_id, Phase::Succeeded) &&
                        controls.Read().runtime.restore_path == path;
                });
                sequence_ok &= controls.Read().runtime.paused && controls.Read().runtime.frame == frame;
                failed_id = controls.Submit(Action::SaveReproduction, 0, std::string(directory) + "/missing/fail.ps2st");
                sequence_ok &= Await([&] { return Completed(controls, failed_id, Phase::Failed); });
                sequence_ok &= controls.Read().runtime.restore_path == path;
            }
            controls.Submit(Action::Close);
            if (!Await([&] { return runtime.Closing(); })) { sequence_ok = false; runtime.RequestClose(); }
        });
        const int result = runtime.Run([&] {
            game_thread = std::this_thread::get_id();
            for (uint64_t frame = 0; !runtime.Closing(); ++frame)
                if (!runtime.Boundary(frame)) break;
            return 0;
        }, 3);
        director.join();
        CHECK(result == 0 && sequence_ok && callbacks_on_owner && drained.load());
        CHECK(captures == (close_before_completion ? 1 : 2));
        CHECK(Completed(controls, saved_id, Phase::Succeeded));
        CHECK(!failed_id || Completed(controls, failed_id, Phase::Failed));
        CHECK(controls.Read().pending.empty() && controls.Read().runtime.restore_path == path);
        CHECK(!controls.Read().capabilities.restore_reproduction);
        FileMeta meta;
        std::vector<Blob> decoded;
        CHECK(ReadStateFile(path, &meta, &decoded, &error));
    }
    std::filesystem::remove_all(directory);
    SDL_Quit();
}
#endif
#endif

int main(int argc, char** argv) {
#if PS2R_PRODUCT_CHECKPOINT_TESTS
    if (argc >= 2 && std::strcmp(argv[1], "--thread-capture") == 0) {
        FreshWorkersAndIncarnations();
        WalkChainCapturesEveryDeferredFrame();
        ProductionRestartPreservesOneOwner();
        std::printf("ThreadCapture: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
#if PS2R_PRODUCT_GUI_TESTS
    if (argc >= 2 && std::strcmp(argv[1], "--checkpoint-runtime") == 0) {
        RuntimePublishesCheckpointCompletion(false);
        RuntimePublishesCheckpointCompletion(true);
        std::printf("SessionCheckpointRuntime: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
#endif
    if (argc >= 2 && std::strcmp(argv[1], "--checkpoint") == 0) {
        FreshContinuationAndResumeTailPins();
        CapturedStateIsOwnedAndWriteResultsAreReal();
        std::printf("SessionCheckpoint: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
#endif
#if PS2R_PRODUCT_GUI_TESTS
    if (argc >= 2 && std::strcmp(argv[1], "--shutdown-timeout") == 0)
        return StuckWorkerShutdown();
    if (argc >= 2 && std::strcmp(argv[1], "--hotkey-route") == 0) {
        RealSdlKeyEventReachesSessionControls();
        std::printf("SessionHotkeyRoute: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--runtime") == 0) {
        RuntimeKeepsMainResponsive(false);
        RuntimeKeepsMainResponsive(true);
        std::printf("SessionRuntime: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--gui") == 0) {
        GuiOnRealSdlAndGlOwners(argc >= 3 ? argv[2] : nullptr);
        std::printf("SessionGui: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    HotkeysAndUiShareCommands();
    SaveNeverSucceedsBeforeOwnerCompletion();
    RestoreValidationFailureLeavesPublishedStateAlone();
    CapabilitiesAndValuesAreHonest();
    CompletedPauseDoesNotUseStaleUiState();
    CloseCannotBeStarvedByPendingSave();
    BoundedConcurrentRequests();
    RestoreSelectionSharedWithHotkey();
    ObserverIsLoudOutsideTheLock();
    RestoreFallsBackToTheLatestSlot();
#if PS2R_PRODUCT_GUI_TESTS
    NoticesStayVisibleAndExpireWithoutCapturingInput();
    InputIsSampledOnlyOnMain();
    MotorRequestsUseActualSdlResults();
#endif
    std::printf("SessionControls: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
