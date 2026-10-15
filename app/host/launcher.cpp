#include "app/host/launcher.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/host/game_files_stamp.h"
#include "app/host/launcher_settings.h"
#include "app/host/user_data.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "engine/core/config/config.h"
#include "engine/core/config/settings_file.h"
#include "engine/platform/host/platform.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/title/title_profile.h"
#include "imgui.h"
#include PS2R_GL_HEADER

namespace ps2r::app {
namespace {
constexpr int kWidth = 860, kHeight = 560;
const char* kPages[] = {"Play", "Display", "Controls", "Save states", "Fixes", "Data"};
bool FileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
}  // namespace

bool Launcher::Wanted(const Config& cfg, const TitleProfile& title, bool harness_cli, bool forced) {
    if (cfg.harness.headless) return false;
    if (forced) return true;   // --launcher wins over the harness flags (a pinned launcher leg)
    if (harness_cli) return false;
    if (!FirstRun::GameFilesPresent(cfg, title)) return true;
    return cfg.launcher.show;
}

Launcher::Launcher(Config& cfg, const TitleProfile& title, std::string build_id)
    : cfg_(cfg), title_(title), build_id_(std::move(build_id)) {}

void Launcher::SetScreenshot(const std::string& ppm_path, bool then_play) {
    screenshot_ = ppm_path;
    then_play_ = then_play;
}

void Launcher::SetHarnessPage(const std::string& page) {
    static const char* kNames[] = {"play", "display", "controls", "saves", "fixes", "data"};
    for (int i = 0; i < 6; i++)
        if (page == kNames[i]) { page_ = i; return; }
    std::fprintf(stderr, "launcher: unknown --launcher-page '%s' (play|display|controls|saves|fixes|data)\n", page.c_str());
}

Launcher::~Launcher() {
    if (worker_.joinable()) {
        if (extract_) extract_->cancel.store(true);
        worker_.join();
    }
    CloseWindow();
}

std::string Launcher::DiscPath() const {
    return !cfg_.paths.iso.empty() ? cfg_.paths.iso : cfg_.paths.game_files + "/" + title_.disc_image_file;
}

bool Launcher::OpenWindow(std::string& error) {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { error = SDL_GetError(); return false; }
        video_inited_ = true;
    }
    platform::GlContext::RequestAttributes();
    window_ = SDL_CreateWindow("NativeEE -- the title -- Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kWidth, kHeight,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) { error = SDL_GetError(); return false; }
    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) { error = SDL_GetError(); return false; }
    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(1);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    IMGUI_CHECKVERSION();
    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;
    if (!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_) || !ImGui_ImplOpenGL3_Init("#version 410 core")) {
        error = "ImGui backend init failed";
        return false;
    }
    SDL_RaiseWindow(window_);
    return true;
}

void Launcher::CloseWindow() {
    if (imgui_) {
        SDL_GL_MakeCurrent(window_, gl_context_);
        ImGui::SetCurrentContext(imgui_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
    }
    if (gl_context_) { SDL_GL_DeleteContext(gl_context_); gl_context_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    for (void* h : controller_handles_) SDL_GameControllerClose(static_cast<SDL_GameController*>(h));
    controller_handles_.clear();
    if (controllers_inited_) { SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER); controllers_inited_ = false; }
    if (video_inited_) { SDL_QuitSubSystem(SDL_INIT_VIDEO); video_inited_ = false; }
}

// Every present controller is OPENED (SDL delivers SDL_CONTROLLERBUTTONDOWN /
// AXISMOTION for open handles only -- the Controls page's press-to-bind reads
// them) and closed with the window; the game's InputSystem opens its own later.
void Launcher::DetectControllers() {
    controllers_.clear();
    for (void* h : controller_handles_) SDL_GameControllerClose(static_cast<SDL_GameController*>(h));
    controller_handles_.clear();
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
        controllers_inited_ = true;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        const char* name = SDL_GameControllerNameForIndex(i);
        controllers_.push_back(name ? name : "controller");
        if (SDL_GameController* gc = SDL_GameControllerOpen(i)) controller_handles_.push_back(gc);
    }
}

void Launcher::OpenDataFolder() {
    const std::string root = cfg_.paths.user_data;
    if (root.empty() || root.find('\'') != std::string::npos) return;
#if defined(__APPLE__)
    const std::string cmd = "open '" + root + "'";
#elif defined(_WIN32)
    const std::string cmd = "explorer \"" + root + "\"";
#else
    const std::string cmd = "xdg-open '" + root + "' >/dev/null 2>&1";
#endif
    (void)std::system(cmd.c_str());
}

Launcher::Outcome Launcher::Run() {
    std::string error;
    if (!OpenWindow(error)) {
        std::fprintf(stderr, "launcher: cannot open the window: %s\n", error.c_str());
        CloseWindow();
        return Outcome::Failed;
    }
    settings_ = std::make_unique<LauncherSettings>(cfg_, UserData::SettingsFile(cfg_), cfg_.paths.user_data);
    DetectControllers();
    screen_ = FirstRun::GameFilesPresent(cfg_, title_) ? Screen::Ready : Screen::NeedDisc;
    std::fprintf(stderr, "launcher: %s (user data %s)\n", screen_ == Screen::Ready ? "settings screen" : "first run: no game files",
                 cfg_.paths.user_data.c_str());
    if (!scripted_drop_.empty()) {
        // The scripted drop (--launcher-disc): the same path a drop on the window takes.
        image_path_ = scripted_drop_;
        std::snprintf(path_buffer_, sizeof path_buffer_, "%s", image_path_.c_str());
        std::fprintf(stderr, "launcher: scripted drop %s\n", image_path_.c_str());
        StartExtraction(image_path_);
    }
    // Harness screenshot mode is bounded: three presented frames normally take
    // well under a second; a window that never presents (occluded swap, a stuck
    // compositor) fails here by name instead of hanging the Root pin to its timeout.
    const uint64_t harness_start_ms = SDL_GetTicks64();
    constexpr uint64_t kHarnessDeadlineMs = 30000;
    while (running_) {
        PumpEvents();
        if (screen_ == Screen::Extracting && extract_) {
            bool finished = false;
            {
                std::lock_guard<std::mutex> lock(extract_->mutex);
                finished = extract_->finished;
            }
            if (finished && worker_.joinable()) FinishExtraction();
        }
        DrawFrame();
        if (auto_play_ && screen_ == Screen::Ready && screenshot_.empty()) {
            // --launcher-then-play without a screenshot: Play the moment the Ready
            // screen is up (one presented frame of it, so the hand-off is seen).
            running_ = false;
            outcome_ = Outcome::Play;
            std::fprintf(stderr, "launcher: Play (auto)\n");
        } else if (auto_play_ && screen_ == Screen::NeedDisc && !worker_.joinable() && !scripted_drop_.empty()) {
            // The scripted drop was refused: nothing to play.
            std::fprintf(stderr, "launcher: scripted drop refused; no game files -- leaving\n");
            running_ = false;
            outcome_ = Outcome::Failed;
        }
        if (!screenshot_.empty() && ++frames_ == 3) {
            const bool ok = DumpFrame(screenshot_);
            std::fprintf(stderr, "launcher: screenshot %s: %s\n", screenshot_.c_str(), ok ? "written" : "FAILED");
            running_ = false;
            outcome_ = then_play_ && screen_ == Screen::Ready ? Outcome::Play : Outcome::Quit;
            if (outcome_ == Outcome::Play) std::fprintf(stderr, "launcher: Play (harness)\n");
        } else if (!screenshot_.empty() && SDL_GetTicks64() - harness_start_ms > kHarnessDeadlineMs) {
            std::fprintf(stderr, "launcher: screenshot %s: FAILED (%d presented frames in %llu ms)\n", screenshot_.c_str(), frames_,
                         (unsigned long long)kHarnessDeadlineMs);
            running_ = false;
            outcome_ = Outcome::Failed;
        }
    }
    CloseWindow();
    return outcome_;
}

bool Launcher::DumpFrame(const std::string& path) {
    // The back buffer of the frame just drawn (DrawFrame swapped it to the front:
    // read the FRONT buffer -- the presented image).
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return false;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) std::fwrite(rgba.data() + (static_cast<size_t>(y) * w + x) * 4, 1, 3, f);
    std::fclose(f);
    return true;
}

void Launcher::PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
            running_ = false;
            outcome_ = Outcome::Quit;
        } else if (e.type == SDL_DROPFILE) {
            image_path_ = e.drop.file;
            SDL_free(e.drop.file);
            std::snprintf(path_buffer_, sizeof path_buffer_, "%s", image_path_.c_str());
            verify_message_.clear();
            if (screen_ == Screen::Ready) page_ = 5;   // Data page: "change disc"
            if (screen_ == Screen::NeedDisc || screen_ == Screen::Ready) StartExtraction(image_path_);
        } else if (e.type == SDL_KEYDOWN && settings_ && settings_->KeyBindingArmed() && !e.key.repeat) {
            settings_->OfferKey(SDL_GetScancodeName(e.key.keysym.scancode));
        } else if (e.type == SDL_KEYDOWN && settings_ && settings_->PadBindingArmed() && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            settings_->CancelBinding();   // Escape leaves the armed pad row as it was
        } else if ((e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERAXISMOTION) && settings_ && settings_->PadBindingArmed()) {
            // Press-to-bind from the pad: a button edge, or a trigger past 25 % travel.
            const std::string control = e.type == SDL_CONTROLLERBUTTONDOWN
                ? ControlsBindings::PadControlFromEvent(true, e.cbutton.button, 0)
                : ControlsBindings::PadControlFromEvent(false, e.caxis.axis, e.caxis.value);
            settings_->OfferPadControl(control);
        } else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) {
            DetectControllers();
        }
    }
}

void Launcher::StartExtraction(const std::string& image) {
    if (worker_.joinable()) return;
    extract_ = std::make_unique<ExtractState>();
    const FirstRun::Plan plan = FirstRun::PlanFor(cfg_, title_, build_id_, image, copy_disc_);
    screen_ = Screen::Extracting;
    ExtractState* st = extract_.get();
    worker_ = std::thread([st, plan] {
        platform::host::RegisterSelf();
        const FirstRun::Result r = FirstRun::Run(plan, [st](const FirstRun::Progress& p) {
            std::lock_guard<std::mutex> lock(st->mutex);
            if (!p.message.empty() || p.stage != st->progress.stage) st->progress.message = p.message;
            st->progress.stage = p.stage;
            st->progress.done = p.done;
            st->progress.total = p.total;
        }, &st->cancel);
        std::lock_guard<std::mutex> lock(st->mutex);
        st->result = r;
        st->finished = true;
    });
}

void Launcher::FinishExtraction() {
    worker_.join();
    const FirstRun::Result& r = extract_->result;
    if (r.ok) {
        // The game starts from THIS Config: the referenced image (or the copy).
        cfg_.paths.iso = r.disc_referenced ? r.disc_path : std::string();
        verify_ok_ = true;
        verify_message_ = r.verify.message;
        last_extract_summary_ = "Extracted: " + r.elf_path + "; disc " + r.disc_path + (r.disc_referenced ? " (referenced)" : " (copied)") +
                                "; stamp " + r.stamp_path;
        std::fprintf(stderr, "launcher: %s\n", last_extract_summary_.c_str());
        screen_ = Screen::Ready;
        page_ = 0;
    } else {
        verify_ok_ = false;
        verify_message_ = r.error;
        std::fprintf(stderr, "launcher: extraction refused: %s\n", r.error.c_str());
        screen_ = FirstRun::GameFilesPresent(cfg_, title_) ? Screen::Ready : Screen::NeedDisc;
    }
}

void Launcher::DrawFrame() {
    SDL_GL_MakeCurrent(window_, gl_context_);
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("the title -- PC (NativeEE)");
    ImGui::SameLine(display.x - 330);
    ImGui::TextDisabled("%s", build_id_.c_str());
    ImGui::Separator();
    switch (screen_) {
        case Screen::NeedDisc: DrawNeedDisc(); break;
        case Screen::Extracting: DrawExtracting(); break;
        case Screen::Ready: DrawReady(); break;
    }
    ImGui::End();
    ImGui::Render();
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

void Launcher::DrawNeedDisc() {
    ImGui::Spacing();
    ImGui::TextWrapped("No game files found under %s.", cfg_.paths.game_files.c_str());
    ImGui::TextWrapped("This program ships no game data. Drop your own the title disc image (NTSC-U, SLUS-12345; .iso or "
                       ".bin) on this window, or type its path below. The image is verified (ISO9660 volume, SLUS_123.45 present, "
                       "ELF SHA-256 equal to the one this build was made from) before anything is written.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-130);
    ImGui::InputTextWithHint("##path", "/path/to/the title.iso", path_buffer_, sizeof path_buffer_);
    ImGui::SameLine();
    if (ImGui::Button("Verify & extract", ImVec2(120, 0)) && path_buffer_[0]) {
        image_path_ = path_buffer_;
        StartExtraction(image_path_);
    }
    ImGui::Checkbox("Copy the disc image into the data folder (recommended; a .bin is converted to .iso). Off: reference it in place.",
                    &copy_disc_);
    if (!verify_message_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, verify_ok_ ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.55f, 0.45f, 1));
        ImGui::TextWrapped("%s", verify_message_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Controllers");
    if (controllers_.empty()) ImGui::TextDisabled("none detected -- keyboard + mouse drive the pad (W/A/S/D, mouse camera, Space jump)");
    for (const std::string& c : controllers_) ImGui::BulletText("%s", c.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("Data folder: %s", cfg_.paths.user_data.c_str());
    ImGui::TextDisabled("Harness use: nativeee --extract-disc <image> (no window); --game-files / --profile / --set bypass this screen.");
}

void Launcher::DrawExtracting() {
    FirstRun::Progress p;
    {
        std::lock_guard<std::mutex> lock(extract_->mutex);
        p = extract_->progress;
    }
    ImGui::Spacing();
    ImGui::Text("Extracting %s", image_path_.c_str());
    const char* stages[] = {"verify", "elf", "disc", "memcards", "settings", "stamp"};
    for (int i = 0; i < 6; i++) {
        const bool done = static_cast<int>(p.stage) > i, now = static_cast<int>(p.stage) == i;
        ImGui::TextColored(done ? ImVec4(0.5f, 1, 0.5f, 1) : now ? ImVec4(1, 1, 0.6f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                           "%s %s", done ? "[x]" : now ? "[>]" : "[ ]", stages[i]);
    }
    const float fraction = p.total > 0 ? static_cast<float>(static_cast<double>(p.done) / static_cast<double>(p.total)) : 0.0f;
    char label[96];
    if (p.stage == FirstRun::Stage::Disc && p.total > 0)
        std::snprintf(label, sizeof label, "%.1f / %.1f MB", p.done / 1048576.0, p.total / 1048576.0);
    else
        std::snprintf(label, sizeof label, "%s", FirstRun::StageName(p.stage));
    ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
    ImGui::TextWrapped("%s", p.message.c_str());
    if (ImGui::Button("Cancel")) extract_->cancel.store(true);
}

void Launcher::DrawReady() {
    ImGui::BeginChild("pages", ImVec2(150, 0), true);
    for (int i = 0; i < 6; i++)
        if (ImGui::Selectable(kPages[i], page_ == i)) page_ = i;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0, 0), false);
    switch (page_) {
        case 0: {
            ImGui::Spacing();
            if (!last_extract_summary_.empty()) ImGui::TextWrapped("%s", last_extract_summary_.c_str());
            ImGui::TextWrapped("Game files: %s", cfg_.paths.game_files.c_str());
            ImGui::TextWrapped("Disc image: %s", DiscPath().c_str());
            const GameFilesStamp::Check stamp = GameFilesStamp::Verify(cfg_.paths.game_files, build_id_);
            // The stamp is the extraction's completion marker (FirstRun writes it
            // LAST). Mismatch = another build's files; Absent = no completed
            // extraction (hand-supplied files, or a half-finished one) -- both
            // warn here, and a disc image that cannot be found is called out by
            // name, so Play is never the only hint that something is missing.
            const bool disc_missing = !FileExists(DiscPath());
            std::string warning;
            if (stamp.state == GameFilesStamp::State::Mismatch)
                warning = GameFilesStamp::Warning(stamp, build_id_);
            else if (stamp.state != GameFilesStamp::State::Match)
                warning = "game files under " + cfg_.paths.game_files + " carry no extraction stamp (" + GameFilesStamp::MetaValue(stamp) +
                          "): the extraction did not complete, or the files were supplied by hand";
            if (disc_missing) warning += (warning.empty() ? "" : "; ") + std::string("disc image not found: " + DiscPath());
            if (!warning.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, 1));
                ImGui::TextWrapped("%s -- re-extract from the Data page if files are missing or stale.", warning.c_str());
                ImGui::PopStyleColor();
                if (ImGui::Button("Go to Data page (re-extract)")) page_ = 5;
            }
            ImGui::Spacing();
            if (controllers_.empty()) ImGui::TextDisabled("Controller: none (keyboard + mouse)");
            for (const std::string& c : controllers_) ImGui::Text("Controller: %s", c.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Play", ImVec2(200, 48))) { outcome_ = Outcome::Play; running_ = false; std::fprintf(stderr, "launcher: Play\n"); }
            ImGui::SameLine();
            if (ImGui::Button("Quit", ImVec2(100, 48))) { outcome_ = Outcome::Quit; running_ = false; }
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("In-game: F1 dev panel, F5 save state, F8 restore. Options > Display / Controls pages in the game's menu.");
            ImGui::PopTextWrapPos();
            if (!verify_message_.empty() && !verify_ok_)
                ImGui::TextColored(ImVec4(1, 0.55f, 0.45f, 1), "%s", verify_message_.c_str());
            break;
        }
        case 1: settings_->DrawDisplayPage(); break;
        case 2: settings_->DrawControlsPage(controllers_); break;
        case 3: settings_->DrawSaveStatesPage(); break;
        case 4: settings_->DrawFixesPage(); break;
        case 5: {
            const GameFilesStamp::Check stamp = GameFilesStamp::Verify(cfg_.paths.game_files, build_id_);
            const std::string request = settings_->DrawDataPage(cfg_.paths.game_files, DiscPath(), GameFilesStamp::MetaValue(stamp));
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-130);
            ImGui::InputTextWithHint("##newdisc", "drop a disc image here or type its path", path_buffer_, sizeof path_buffer_);
            ImGui::SameLine();
            if (ImGui::Button("Verify & extract", ImVec2(120, 0)) && path_buffer_[0]) StartExtraction(path_buffer_);
            ImGui::Checkbox("Copy the disc image into the data folder", &copy_disc_);
            if (request == "open-folder") OpenDataFolder();
            else if (request == "re-extract" && !DiscPath().empty()) StartExtraction(DiscPath());
            if (!verify_message_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, verify_ok_ ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.55f, 0.45f, 1));
                ImGui::TextWrapped("%s", verify_message_.c_str());
                ImGui::PopStyleColor();
            }
            break;
        }
    }
    if (!settings_->Log().empty()) {
        ImGui::Separator();
        for (const std::string& l : settings_->Log()) ImGui::TextDisabled("%s", l.c_str());
    }
    ImGui::EndChild();
}

}  // namespace ps2r::app
