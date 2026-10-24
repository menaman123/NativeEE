#pragma once
// Launcher -- the first-run launcher / settings screen (CHARTER 2 C2), in the
// SAME binary, BEFORE the game starts. An SDL window with its own GL context
// and ImGui context on the main thread; nothing of the game exists yet.
//
//   first run (no extracted game files under the user data root):
//     drop a disc image on the window (or type its path) -> Verify (DiscVerify:
//     ISO9660, SLUS_123.45 present, ELF SHA-256 == the build's; the exact
//     refusal shown verbatim) -> Extract (FirstRun::Run on a worker thread with
//     a progress bar: ELF, disc copy or reference, memcards, nativeee.ini, the
//     BUILD-ID stamp last) -> controller detection -> Play.
//   afterwards: the settings screen (LauncherSettings pages: Display,
//     Controls, Save states, Fixes, Data) with Play; `launcher.show=off` skips
//     it on a plain start; `--launcher` brings it back.
// The harness never sees it: a command line carrying --game-files / --profile /
// --set / --iso / --headless / ... bypasses the launcher entirely (main.cpp).
// Changes made here land in the Config the game starts with and in nativeee.ini (ConfigStore).
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/host/first_run.h"

struct SDL_Window;
struct ImGuiContext;
namespace ps2r { class Config; }
struct TitleProfile;

namespace ps2r::app {

class LauncherSettings;

class Launcher {
public:
    enum class Outcome { Play, Quit, Failed };

    // Should the launcher run for this start? (no harness CLI, a window, and
    // either the files are absent, --launcher was given, or launcher.show).
    static bool Wanted(const Config& cfg, const TitleProfile& title, bool harness_cli, bool forced);

    Launcher(Config& cfg, const TitleProfile& title, std::string build_id);
    ~Launcher();
    Launcher(const Launcher&) = delete;
    Launcher& operator=(const Launcher&) = delete;

    // Harness: write the 3rd presented frame as a P6 PPM and leave (Outcome::Quit),
    // or press Play after it when `then_play`.
    void SetScreenshot(const std::string& ppm_path, bool then_play);
    // Harness / first-run drill: press Play as soon as the Ready screen is up
    // (after an extraction when one is running).
    void SetAutoPlay() { auto_play_ = true; }
    // The scripted drop: this image is verified and extracted at open, exactly
    // as a drop on the window (--launcher-disc; the first-run drill's "choose the ISO").
    void SetScriptedDrop(const std::string& image) { scripted_drop_ = image; }
    // The Ready page to open on: play | display | controls | saves | fixes | data.
    void SetHarnessPage(const std::string& page);

    // Opens the window, runs the loop until Play / close, tears everything
    // down (SDL video quit) so the game's own window starts clean.
    Outcome Run();

private:
    enum class Screen { NeedDisc, Extracting, Ready };
    struct ExtractState {
        std::mutex mutex;
        FirstRun::Progress progress;
        bool finished = false;
        FirstRun::Result result;
        std::atomic<bool> cancel{false};
    };

    bool OpenWindow(std::string& error);
    void CloseWindow();
    void PumpEvents();
    void DrawFrame();
    void DrawNeedDisc();
    void DrawExtracting();
    void DrawReady();
    void StartExtraction(const std::string& image);
    void FinishExtraction();
    void DetectControllers();
    void OpenDataFolder();
    std::string DiscPath() const;
    // The title as the screen names it: TitleProfile::display_name, else its serial.
    std::string TitleName() const;

    Config& cfg_;
    const TitleProfile& title_;
    std::string build_id_;
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    ImGuiContext* imgui_ = nullptr;
    bool video_inited_ = false, controllers_inited_ = false;
    bool running_ = true;
    Outcome outcome_ = Outcome::Quit;
    Screen screen_ = Screen::NeedDisc;
    std::string image_path_;        // the dropped / typed image
    char path_buffer_[1024] = {};
    bool copy_disc_ = true;
    std::string verify_message_;    // the last verify result shown
    bool verify_ok_ = false;
    std::unique_ptr<ExtractState> extract_;
    std::thread worker_;
    std::vector<std::string> controllers_;
    std::vector<void*> controller_handles_;   // SDL_GameController*, opened for the press-to-bind events
    std::unique_ptr<LauncherSettings> settings_;
    int page_ = 0;
    std::string last_extract_summary_;
    std::string screenshot_;
    bool then_play_ = false;
    bool auto_play_ = false;
    std::string scripted_drop_;
    int frames_ = 0;
    bool DumpFrame(const std::string& path);
};

}  // namespace ps2r::app
