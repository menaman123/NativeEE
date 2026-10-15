#include "app/gui/session_gui.h"

#include <SDL.h>
#include <cassert>
#include <chrono>
#include <algorithm>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

namespace ps2r::app {

SessionGui::SessionGui(SessionControls& controls)
    : controls_(controls), main_thread_(std::this_thread::get_id()) {}

SessionGui::~SessionGui() {
    // GL resources cannot be destroyed by a foreign thread or after the window.
    // Root explicitly shuts the GUI down before destroying its video stack.
    assert(context_ == nullptr && "Call SessionGui::Shutdown before window teardown");
}

bool SessionGui::OnMainThread() const {
    return std::this_thread::get_id() == main_thread_;
}

bool SessionGui::ContextCurrent() const {
    return SDL_GL_GetCurrentContext() == gl_context_ && SDL_GL_GetCurrentWindow() == window_;
}

bool SessionGui::Initialize(SDL_Window* window, void* gl_context, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!OnMainThread() || !window || !gl_context || context_) {
        error = "Create the menu once on the window's main thread.";
        return false;
    }
    if (SDL_GL_GetCurrentContext() != gl_context || SDL_GL_GetCurrentWindow() != window) {
        error = "The window's graphics context must be current while creating the menu.";
        return false;
    }
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(16, 14);
    style.ItemSpacing = ImVec2(10, 8);
    const bool sdl_ready = ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    const bool gl_ready = sdl_ready && ImGui_ImplOpenGL3_Init("#version 410 core");
    const bool objects_ready = gl_ready && ImGui_ImplOpenGL3_CreateDeviceObjects();
    if (!objects_ready) {
        if (gl_ready) ImGui_ImplOpenGL3_Shutdown();
        if (sdl_ready) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        error = "The session menu could not initialize its graphics resources.";
        return false;
    }
    window_ = window;
    gl_context_ = gl_context;
    window_id_ = SDL_GetWindowID(window);
    error.clear();
    return true;
}

SessionGui::EventResult SessionGui::CaptureResult(bool consumed) const {
    const bool menu = controls_.MenuOpen();
    return {true, consumed, menu, menu};
}

SessionGui::EventResult SessionGui::ProcessEvent(const SDL_Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!OnMainThread() || !context_) return {};
    ImGui::SetCurrentContext(context_);
    if (event.type == SDL_QUIT ||
        (event.type == SDL_WINDOWEVENT && event.window.windowID == window_id_ &&
         event.window.event == SDL_WINDOWEVENT_CLOSE)) {
        controls_.Submit(SessionControls::Action::Close);
        return CaptureResult(true);
    }
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        // Whose press is this? SDL stamps a key event with its keyboard-focus
        // window's id, and 0 when it recorded no focus window at all (the
        // s5 F5: a --harness-window play session whose two windows never saw
        // SDL_WINDOWEVENT_FOCUS_GAINED -- every press arrived as windowID=0 and
        // the strict `windowID == ours` test below discarded it before
        // HandleFunctionKey). This process owns every SDL window there is, so a
        // press is ours unless SDL names a window that is not ours; the hotkeys
        // F1/F5/F8/F9/F10 are taken from the game window in every play mode,
        // whatever the focus bookkeeping or the renderer's hidden window say.
        const bool ours = event.key.windowID == window_id_ || event.key.windowID == 0 ||
                          SDL_GetWindowFromID(event.key.windowID) != nullptr;
        if (!ours) return CaptureResult(false);
        const auto key = event.key.keysym.sym;
        const bool text_input = controls_.MenuOpen() && ImGui::GetIO().WantTextInput;
        if (key >= SDLK_F1 && key <= SDLK_F12 &&
            controls_.HandleFunctionKey(static_cast<int>(key - SDLK_F1 + 1),
                                        event.type == SDL_KEYDOWN, event.key.repeat != 0, text_input))
        {
            if (!controls_.MenuOpen()) frame_ready_ = false;
            return CaptureResult(true);
        }
        // A press for another window of ours (the renderer's hidden one) is not
        // menu text; the pad mapper still sees it.
        if (event.key.windowID != window_id_ && event.key.windowID != 0) return CaptureResult(false);
    }
    // Closed menus do not accumulate an unbounded backend input queue. On
    // reopening, the main thread provides fresh events and a fresh UI frame.
    if (!controls_.MenuOpen()) return CaptureResult(false);
    const bool processed = ImGui_ImplSDL2_ProcessEvent(&event);
    return CaptureResult(processed);
}

bool SessionGui::BuildFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!OnMainThread() || !context_) return false;
    frame_ready_ = false;
    ImGui::SetCurrentContext(context_);
    const auto snapshot = controls_.Read();
    if (!snapshot.menu_open && was_open_) {
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearEventsQueue();
    }
    was_open_ = snapshot.menu_open;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool panel = panel_.WantsFrame(snapshot, now);
    if (!panel && !picture_) return false;
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (picture_ && picture_width_ > 0 && picture_height_ > 0) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        float x = 0, y = 0, w = 0, h = 0;
        PictureRect(display.x, display.y, picture_width_, picture_height_, picture_aspect_, x, y, w, h);
        ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(picture_), ImVec2(x, y),
            ImVec2(x + w, y + h), ImVec2(0, 1), ImVec2(1, 0));
    }
    if (panel) panel_.Draw(controls_, snapshot);
    ImGui::Render();
    frame_ready_ = true;
    return true;
}

void SessionGui::PictureRect(float display_w, float display_h, int picture_w, int picture_h, double aspect,
                             float& x, float& y, float& w, float& h) {
    if (aspect > 0.0) {
        // A box of the display shape: the anamorphic frame stretched to it.
        const float shape = static_cast<float>(aspect);
        h = std::min(display_h, display_w / shape);
        w = h * shape;
    } else {
        const float scale = std::min(display_w / picture_w, display_h / picture_h);
        w = picture_w * scale;
        h = picture_h * scale;
    }
    x = (display_w - w) * 0.5f;
    y = (display_h - h) * 0.5f;
}

void SessionGui::SetPictureAspect(double ratio) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!OnMainThread()) return;
    picture_aspect_ = ratio;
}

void SessionGui::SetPicture(uint32_t texture, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!OnMainThread()) return;
    picture_ = texture;
    picture_width_ = width;
    picture_height_ = height;
}

bool SessionGui::Render() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_ || !frame_ready_ || !ContextCurrent()) return false;
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return true;
}

bool SessionGui::Shutdown(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_) { error.clear(); return true; }
    if (!OnMainThread() || !ContextCurrent()) {
        error = "Return the graphics context to main before closing the session menu.";
        return false;
    }
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    window_ = nullptr;
    gl_context_ = nullptr;
    frame_ready_ = false;
    was_open_ = false;
    error.clear();
    return true;
}

}  // namespace ps2r::app
