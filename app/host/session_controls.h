#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::app {

// Commands cross from the window/UI owner to the one game owner. No callback,
// file access, game execution or SDL/GL operation runs under this object's lock.
class SessionControls {
public:
    enum class Action {
        ToggleMenu, SaveReproduction, RestoreReproduction, TogglePause,
        Step, Close, SetVolume, SetFullscreen, SetKeyboardInput, SetRecording,
        // The in-game 'Display' page's perf overlay toggle (ui.perf_overlay):
        // applied by the presenter, persisted in the settings file.
        SetPerfOverlay,
        // The Display page's rows (Charter 2 A2/A3), each applied live by the
        // presenter / the video stack and persisted in the settings file:
        // video.fullscreen (0 windowed, 1 fullscreen, 2 borderless), video.scale
        // (0 = native window, 1..4), video.vsync (0/1), video.aspect (the
        // AspectSel order: 0 auto, 1 4:3, 2 16:9, 3 16:10, 4 21:9) and
        // video.render_rate (0 = 60, 1 = display) and video.frame_cap (the presented-
        // frame cadence: 0 off, 1 30, 2 60 -- FramePacer).
        SetDisplayMode, SetScale, SetVSync, SetAspect, SetRenderRate, SetFrameCap,
        // The Display page's texture filtering rows (video.texture_filter: the
        // mode's index title|point|bilinear|trilinear; video.anisotropy: 1|2|4|8|16):
        // applied to the owned textures' samplers from the next draw, persisted.
        SetTextureFilter, SetAnisotropy
    };
    enum class Phase { Queued, Working, Waiting, Succeeded, Refused, Failed, Cancelled };
    struct Capabilities {
        bool pause = false;
        bool single_step = false;
        bool save_reproduction = false;
        bool restore_reproduction = false;
        bool volume = false;
        bool fullscreen = false;
        bool keyboard_input = false;
        bool recording = false;
        bool perf_overlay = false;
        bool display_rows = false;   // the A2/A3 rows (a live video stack)
        bool texture_filter = false;
    };
    struct RuntimeState {
        uint64_t frame = 0;
        // The clock contract's latest completed 1 s window: game boundaries per
        // second (60 when the game keeps wall speed) and frames presented per second.
        uint32_t game_hz = 0;
        uint32_t render_fps = 0;
        bool paused = false;
        int volume_percent = 100;
        bool fullscreen = false;
        // Presenter readers: video.vsync, video.fps_cap (0 = uncapped) and
        // the window's current drawable size. Resize never changes the
        // game's logical render-target dimensions.
        bool vsync = false;
        int fps_cap = 60;
        int window_width = 0;
        int window_height = 0;
        bool keyboard_input = true;
        bool recording = false;
        // ui.perf_overlay: the game Hz / render fps overlay over the presented frame.
        bool perf_overlay = false;
        // The Display page's live values (A2/A3): video.fullscreen's mode, video.scale
        // (0 = native window), video.aspect's selection and video.render_rate.
        int display_mode = 0;
        int scale = 1;
        int aspect = 0;
        int render_rate = 0;
        int frame_cap = 2;   // video.frame_cap's index (0 off, 1 30, 2 60)
        // video.texture_filter (the mode index) and video.anisotropy in force.
        int texture_filter = 0;
        int anisotropy = 1;
        std::string input_name;
        std::string recording_path;
        std::string restore_path;
        std::string reproductions_path;   // the folder F5 writes into and the browser lists
        std::string game_saves_path;
        std::string status;
    };
    struct Request {
        uint64_t id = 0;
        Action action = Action::ToggleMenu;
        int value = 0;
        std::string path;
    };
    struct Result {
        Request request;
        Phase phase = Phase::Queued;
        std::string message;
    };
    struct Snapshot {
        Capabilities capabilities;
        RuntimeState runtime;
        bool menu_open = false;
        bool closing = false;
        // The pause-menu shape (SetPauseMenu): Escape opens the menu and pauses
        // the game instead of closing the window; `menu_paused` = the menu holds
        // a pause it will release on Resume.
        bool pause_menu = false;
        bool menu_paused = false;
        std::string restore_path;
        std::vector<Result> pending;
        std::vector<Result> recent;
    };

    static constexpr size_t kMaxPending = 16;
    static constexpr size_t kHistoryLimit = 32;
    static constexpr size_t kTextLimit = 4096;

    SessionControls() = default;
    SessionControls(const SessionControls&) = delete;
    SessionControls& operator=(const SessionControls&) = delete;

    // Every command gets an ID and a visible result, including a refused queue
    // admission. Empty save path asks root to generate a unique destination.
    // Empty restore path captures the UI's current selection at submission.
    uint64_t Submit(Action action, int value = 0, std::string path = {});
    // F1/5/8/9/10. Key repeats and key-up never submit. F1 remains available
    // during text entry; the other commands are suppressed while editing text.
    bool HandleFunctionKey(int number, bool down, bool repeat, bool text_input);

    // One composition-root consumer calls TakeNext and dispatches to the action's
    // owner. A request is delivered once; Close has reserved space and priority.
    bool TakeNext(Request& request);
    // Waiting is pending, not success. Finish rejects stale/duplicate IDs and
    // non-terminal phases. Root must report real write/restore completion.
    bool Waiting(uint64_t id, std::string reason);
    bool Finish(uint64_t id, Phase phase, std::string message);
    // The loud route for terminal results (U4/U5: no silent save success or
    // refusal): the observer receives every finished or cancelled request AFTER
    // the lock is released, on the finishing thread. Root installs the one that
    // prints `nativeee: savestate ...` to stderr and the SAVE log.
    using Observer = std::function<void(const Result&)>;
    void SetObserver(Observer observer);
    // F8 with no browser selection: the path to restore (empty = none; the
    // reason names why, e.g. the slot directory to press F5 into first).
    using RestoreFallback = std::function<std::string(std::string* reason)>;
    void SetRestoreFallback(RestoreFallback fallback);
    // Shutdown cancels outstanding requests visibly; it performs no teardown.
    void CancelPending(std::string reason);
    void Publish(Capabilities capabilities, RuntimeState runtime);
    // UI selection used by both the Restore button and F8. False rejects an
    // invalid/overlong path without changing the previous selection.
    bool SelectRestorePath(std::string path);
    Snapshot Read() const;
    bool MenuOpen() const;
    // The pause-menu shape (the composition's choice, main thread before the
    // pump): Escape = open the menu + pause (SessionGui), the panel offers Resume
    // / Quit. Off (the default) keeps Escape = the window close. `MenuPaused` is
    // the GUI's note that the menu issued the pause it must release on Resume.
    void SetPauseMenu(bool on);
    bool PauseMenu() const;
    void SetMenuPaused(bool paused);
    bool MenuPaused() const;
    static const char* ActionLabel(Action action);
    static const char* PhaseLabel(Phase phase);

private:
    uint64_t SubmitLocked(Action action, int value, std::string path, Observer& observer, Result& admitted);
    bool Supported(Action action) const;
    bool Conflicts(Action action) const;
    void Remember(Result result);
    static bool Terminal(Phase phase);
    static std::string Bounded(std::string text);

    mutable std::mutex mutex_;
    Observer observer_;   // read under the lock, invoked outside it
    RestoreFallback restore_fallback_;
    Capabilities capabilities_;
    RuntimeState runtime_;
    std::string restore_path_;
    std::deque<Result> pending_;
    std::deque<Result> recent_;
    uint64_t next_id_ = 1;
    bool menu_open_ = false;
    bool closing_ = false;
    bool pause_menu_ = false;
    bool menu_paused_ = false;
};

}  // namespace ps2r::app
