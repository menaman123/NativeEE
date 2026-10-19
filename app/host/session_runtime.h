#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/host/frame_pacer.h"
#include "app/host/session_controls.h"
#include "app/host/session_window.h"
#include "app/host/shutdown_census.h"
#include "engine/gs/hle/present/aspect_selection.h"
#include "engine/render/texture_filter_selection.h"
#include "engine/core/threads/park.h"

class PlatformInput;

namespace ps2r::app {

// The interactive host loop. Main owns windows/input; existing game scheduling
// keeps one execution owner. Boundary commands execute only on that owner.
class SessionRuntime {
public:
    struct CaptureResult {
        SessionControls::Phase phase = SessionControls::Phase::Refused;
        std::string detail;
    };
    struct CaptureCompletion {
        uint64_t request = 0;
        CaptureResult result;
        std::string path;
    };
    struct RestoreCompletion {
        uint64_t request = 0;
        CaptureResult result;
        uint64_t frame = 0;
        std::string recording_path;
        bool requires_close = false;
    };
    struct Hooks {
        // Main: request cancellation only; do not join or wait here.
        std::function<void()> stop;
        // Game owner: runs while holding the existing execution token.
        std::function<void(bool)> pause;
        std::function<CaptureResult(const SessionControls::Request&, uint64_t)> capture;
        // Main: nonblocking writer result; required if capture returns Working.
        // Called only after admission, including while paused and after stopped.
        std::function<std::optional<CaptureCompletion>()> poll_capture;
        // Complete restore composition: pure preflight on the completed game
        // owner, unwind/wake there, restart on the joined supervisor. alive
        // observes the current boundary incarnation without running guest code.
        std::function<CaptureResult(const SessionControls::Request&, uint64_t)> restore;
        std::function<void()> unwind_restore;
        std::function<RestoreCompletion()> restart;
        std::function<bool()> alive;
        // Supervisor: after game returns, before completion is published.
        // Join renderer/writers here so main keeps pumping and enforcing the
        // shutdown deadline. No SDL window operations belong in this callback.
        std::function<void()> stopped;
        // Main, the watchdog: the shutdown census body (run.meta shutdown=) for
        // the leg to publish before the hard exit -- the attribution survives.
        std::function<void(const std::string&)> shutdown_report;
    };
    explicit SessionRuntime(PlatformInput* input);
    ~SessionRuntime();
    bool Open(int volume, bool audio, bool recording, std::string recording_path, std::string& error,
              std::string game_saves_path = {});
    void SetHooks(Hooks hooks);
    // Main thread, any time: video.vsync / video.fps_cap readers for the
    // presenter. fps_cap <= 0 presents on every pump (vsync, when on, paces).
    void ConfigurePresentation(bool vsync, int fps_cap);
    // Main thread, after Open: the window's title bar names the title (TitleProfile::display_name).
    void SetWindowTitle(const std::string& title) { window_.SetTitle(title); }
    // The session's display shape (video.aspect; Charter 2 A1): the window's
    // drawable size feeds `auto`, and the picture is fit at the ratio in force.
    void SetAspect(std::shared_ptr<gs::AspectSelection> aspect);
    // The texture-filtering enhancement (video.texture_filter / video.anisotropy;
    // Charter 2 A2): the GL device samples the owned textures through this
    // selection; the Display page's rows change it here (main thread, at the
    // command) and persist it. Main thread before Run; enables the rows.
    void SetTextureFilter(std::shared_ptr<render::TextureFilterSelection> selection);
    // Any thread: the mode index (title|point|bilinear|trilinear) and anisotropy in force.
    int TextureFilterState() const;
    int AnisotropyState() const;
    // Main thread: the reproduction folder the menu browser lists (the same
    // folder the capture owner writes into).
    void ConfigureReproductions(std::string directory);
    // Main thread before Run: the user-facing settings the in-game menu (the
    // title's added pages, MenuExtension) and the GUI share. `perf_overlay` is
    // the ui.perf_overlay seed; `persist` writes one `key=value` into the
    // settings file after a change is applied (empty = nothing persisted).
    using Persist = std::function<void(const std::string& key, const std::string& value)>;
    void ConfigureSettings(bool perf_overlay, Persist persist);
    // Any thread: the live user-facing values the in-game menu mirrors.
    bool PerfOverlay() const { return perf_overlay_.load(); }
    bool FullscreenState() const { return fullscreen_state_.load(); }
    bool KeyboardInputState() const { return keyboard_state_.load(); }
    // The Display page's rows (Charter 2 A2/A3). Main thread before Run: the
    // seeds from Config -- video.scale (0 = native window), video.aspect (the
    // AspectSel order), video.render_rate (0 = 60, 1 = display), video.fullscreen's
    // mode and harness.display_hz (0 = the window's display). Each row is
    // applied live by the presenter (vsync, mode, aspect, render rate) or
    // handed to the game owner's boundary (the scale: TakeScaleRequest) and
    // persisted through `persist`. Without this call the rows are not offered.
    struct DisplaySettings {
        int scale = 1;
        int aspect = 0;
        int render_rate = 0;
        int display_mode = 0;
        int display_hz = 0;
        int frame_cap = 2;   // video.frame_cap's index: 0 off, 1 30, 2 60 (FramePacer::Cap)
    };
    void ConfigureDisplayRows(const DisplaySettings& seed);
    int ScaleSetting() const { return scale_setting_.load(); }          // 0 = native window, 1..4
    int EffectiveScale() const { return effective_scale_.load(); }      // the factor in force / requested, 1..4
    int AspectSetting() const { return aspect_setting_.load(); }
    int RenderRateSetting() const { return render_rate_.load(); }
    // The presented-frame cadence in force (video.frame_cap's index; FramePacer::Cap) and
    // the pacer's census row ("cap=..:admitted=..:held=..:superseded=..:late=.."). Any thread
    // for the index; the row is read after Run (main).
    int FrameCapSetting() const { return frame_cap_.load(); }
    std::string FrameCapCensus() const { return pacer_.CensusRow(); }
    int DisplayModeState() const { return display_mode_.load(); }
    bool VSyncState() const { return vsync_state_.load(); }
    // The display rate the presenter paces at under render_rate=display (Hz).
    int PresentHz() const;
    // Game owner, at its frame boundary: the internal scale the user asked for
    // since the last take (1..4; 0 = none pending). The caller applies it on
    // the backend owner between frames (VideoStack::ApplyScale).
    int TakeScaleRequest() { return scale_request_.exchange(0); }
    // Main thread before Run: the pause-menu shape -- Escape opens the menu and
    // pauses the game (hooks.pause must exist for the pause), the panel's Resume /
    // Quit; off, Escape is the window close (the first title's shape).
    void ConfigurePauseMenu(bool on);
    // Main thread before Run: the menu's Controls page model (the keyboard rows'
    // press-to-bind over the composition's bindings file + live mapper reload).
    void ConfigureControlsPage(ControlsSection::Model model);
    // Main thread before Run: called after EVERY frame the presenter draws to
    // the window (the display-rate render census and the camera interpolation);
    // `new_picture` = this draw showed a picture the window had not shown before
    // (a Live present), else the held picture again (a Hold: the presented-frame
    // cadence a player sees is the train of the new_picture draws).
    void SetPresentObserver(std::function<void(std::chrono::steady_clock::time_point, bool new_picture)> observer);
    uint64_t Presents() const { return presents_.load(); }
    // Main thread before Run: at the end of the session the presenter's LAST
    // window image (the picture plus the GUI overlays -- what the user saw) is
    // written as <dir>/w_final.ppm; "" = off. The Root pins on the PRESENTED
    // frame read it.
    void ConfigureWindowDump(std::string dir);
    // Runs bootstrap/relaunch/join on a large-stack host thread while main
    // pumps. Shutdown's deadline stays on main, never inside JoinAll.
    int Run(std::function<int()> game, unsigned shutdown_seconds);
    // The shutdown's step census (run.meta shutdown=): Begin at the close
    // request on main, the stopped() ladder's steps on the game worker, End at
    // the worker's join; the watchdog names the step in progress.
    ShutdownCensus& Shutdown() { return shutdown_; }
    const ShutdownCensus& Shutdown() const { return shutdown_; }
    // End of a complete game boundary, holding the existing game run token.
    // False asks the caller to unwind. Paused waits retain that token.
    bool Boundary(uint64_t frame);
    void PostFrame(std::vector<uint8_t> rgba, int width, int height);
    // Any thread: a synthetic PRESS of F<number> (1..12) through the REAL key
    // route -- a SDL_KEYDOWN/SDL_KEYUP pair pushed into SDL's own queue, stamped
    // as SDL stamps a physical press (the keyboard-focus window's id; 0 when no
    // window of this process holds the focus), drained by the main pump like
    // every other event (SDL_PollEvent -> SessionWindow -> SessionGui ->
    // SessionControls). The harness's `--set harness.inject_key=F5@N` and the
    // unit pins use it; it never short-cuts to SessionControls::Submit.
    bool InjectFunctionKey(int number);
    // Any thread: one SDL_KEYDOWN (down) or SDL_KEYUP for `scancode` through the
    // same real route (the harness's held keys: RootKbmPadOnly).
    bool InjectKey(int scancode, bool down);
    // Any thread: a real SDL_QUIT into SDL's queue -- the window manager's close
    // without the finger (harness.close_at_frame; RootExitClean).
    bool InjectClose();
    // Any thread: main pushes the SDL_QUIT `ns` from now (harness.close_after_ms:
    // a close while the game thread is inside a loader with no boundaries).
    void ArmCloseAfter(uint64_t ns);
    // Census of the key route (main thread): key events drained, function keys
    // seen, and function keys the GUI discarded (a window id of another process).
    struct KeyCensus { uint64_t key_events = 0, function_keys = 0, discarded = 0; };
    KeyCensus Keys() const { return key_census_; }
    // The display swaps the presenter executed on its own thread between two
    // presented frames (SetDisplayMode / SetFullscreen / SetVSync: the SDL window
    // and context calls on the thread that owns them -- on macOS the Spaces
    // fullscreen transition blocks that thread for its animation): how many and
    // the longest, in microseconds (run.meta display_swap=count,max_ms). Any
    // thread. The game thread never waits for one (pace_work_max_ms excludes it).
    struct DisplaySwapCensus { uint64_t count = 0, max_us = 0, total_us = 0; };
    DisplaySwapCensus DisplaySwaps() const {
        return {display_swaps_.load(), display_swap_max_us_.load(), display_swap_total_us_.load()};
    }
    // Synchronous owner handoff for window operations. Never executes game code.
    // NOT for the game thread's per-boundary path: main may be inside a display
    // swap for up to a second (Charter 3 item 4) -- use PostMain there.
    bool OnMain(std::function<void()> operation);
    // The same handoff without the wait: the operation runs at main's next pump
    // (in order with OnMain's). False = refused (closing, or the queue is full),
    // the operation dropped. Never blocks the caller.
    bool PostMain(std::function<void()> operation);
    void RequestClose();
    int Volume() const { return volume_.load(); }
    bool Closing() const { return closing_.load(); }
    bool Paused() const { return paused_.load(); }
    bool RestorePending() const { return restore_id_.load() != 0; }
    // Supervisor before any resumed guest can publish a newer frame.
    void RebaseFrame(uint64_t frame) { frame_.store(frame); }
    // Game thread, any time: the clock contract's latest 1 s window (rule 5) for
    // the session panel -- boundaries per second and frames presented per second.
    void SetClockReadout(uint32_t game_hz, uint32_t render_fps);
    // Main thread before Run: print every key / window event the pump drains
    // and how it was routed ([sdl-trace] on stderr). Armed by the host from
    // dev.log_level=narrate.
    void SetEventTrace(bool on);
    SessionControls& Controls() { return controls_; }
    ps2r::Park& Wake() { return wake_main_; }

private:
    struct MainWork {
        std::function<void()> operation;
        std::atomic<bool> done{false};
        bool accepted = false;
        ps2r::Park wake;
    };
    struct Frame {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
    };
    void PumpMain();
    void Publish();
    void ProcessCommands();
    // One presenter-thread display swap (ProcessCommands): the window/context
    // call timed into the DisplaySwapCensus; main only.
    template <class Swap>
    bool TimedDisplaySwap(Swap&& swap);
    void CancelMainWork();
    void CaptureAtBoundary(uint64_t frame);
    void PollCapture();
    bool RestoreAtBoundary(const SessionControls::Request&, uint64_t frame);
    void PollRestore();
    int Supervise(int result);

    PlatformInput* input_;
    // Main is inside a display swap (TimedDisplaySwap: SDL_SetWindowFullscreen,
    // 150-1000 ms on macOS): the pad's Sample does not wait for it at all
    // (PlatformInput::BindOwnerBusy).
    std::atomic<bool> main_busy_{false};
    const std::thread::id main_thread_;
    const int host_rounding_;
    SessionControls controls_;
    SessionWindow window_;
    std::shared_ptr<gs::AspectSelection> aspect_;
    std::shared_ptr<render::TextureFilterSelection> texture_filter_;
    Hooks hooks_;
    SessionControls::Capabilities capabilities_;
    SessionControls::RuntimeState status_;
    std::atomic<int> volume_{100};
    std::atomic<bool> perf_overlay_{false}, fullscreen_state_{false}, keyboard_state_{true}, vsync_state_{false};
    bool display_rows_ = false;
    std::atomic<int> scale_setting_{1}, effective_scale_{1}, scale_request_{0}, aspect_setting_{0}, render_rate_{0},
        display_mode_{0}, frame_cap_{2};
    // The presented-frame cadence (video.frame_cap): the pacer admits the pending picture
    // (the newest posted, main-thread only) at its edges; between edges it waits.
    FramePacer pacer_;
    Frame pending_frame_;
    int display_hz_ = 0;
    std::function<void(std::chrono::steady_clock::time_point, bool)> present_observer_;
    bool picture_fresh_ = false;   // a picture uploaded since the last draw (the observer's new_picture)
    std::atomic<uint64_t> presents_{0};
    std::atomic<uint64_t> display_swaps_{0}, display_swap_max_us_{0}, display_swap_total_us_{0};
    Persist persist_;
    std::string window_dump_dir_;
    int fps_cap_ = 60;
    std::atomic<bool> closing_{false}, paused_{false};
    std::atomic<uint64_t> frame_{0};
    std::atomic<uint32_t> game_hz_{0}, render_fps_{0};
    std::atomic<uint64_t> capture_id_{0};
    std::atomic<uint64_t> restore_id_{0}, restore_started_{0};
    std::optional<RestoreCompletion> restore_completion_;
    uint64_t step_id_ = 0;
    KeyCensus key_census_;
    bool event_trace_ = false;
    SessionControls::Request save_request_;
    unsigned save_attempts_ = 0;
    bool stop_called_ = false;
    ShutdownCensus shutdown_;
    std::atomic<uint64_t> close_deadline_ns_{0};   // ArmCloseAfter; main pushes SDL_QUIT once past it
    std::mutex mutex_;
    std::deque<SessionControls::Request> game_requests_;
    std::deque<std::shared_ptr<MainWork>> main_requests_;
    Frame incoming_frame_;
    ps2r::Park wake_main_, wake_game_;
};

}  // namespace ps2r::app
