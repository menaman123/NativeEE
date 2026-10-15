#include "app/host/session_window.h"

#include <SDL.h>
#include <cstdio>

#include "engine/platform/host/platform.h"
#include PS2R_GL_HEADER

namespace ps2r::app {

SessionWindow::SessionWindow(SessionControls& controls) : gui_(controls) {}

SessionWindow::~SessionWindow() {
    if (context_) {
        SDL_GL_MakeCurrent(window_, context_);
        if (texture_) glDeleteTextures(1, &texture_);
        std::string error;
        if (gui_ready_) gui_.Shutdown(error);
        SDL_GL_DeleteContext(context_);
    }
    if (window_) SDL_DestroyWindow(window_);
}

bool SessionWindow::Open(std::string& error) {
    // The composition root has initialized SDL video and requested GL 4.1.
    window_ = SDL_CreateWindow("the title", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 672, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!window_) { error = SDL_GetError(); return false; }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) { error = SDL_GetError(); return false; }
    // The main pump schedules presentation; SetVSync applies video.vsync.
    SDL_GL_SetSwapInterval(vsync_ ? 1 : 0);
    gui_ready_ = gui_.Initialize(window_, context_, error);
    // The game window is the one the user types into: ask for the keyboard
    // focus now (the renderer's own window is hidden a moment later and a
    // process started from a terminal is not the active application until
    // asked). A refusal is not an error: the key route accepts a press stamped
    // with no focus window as well (SessionGui::ProcessEvent).
    if (gui_ready_) {
        SDL_RaiseWindow(window_);
        SDL_SetWindowInputFocus(window_);
    }
    return gui_ready_;
}

uint32_t SessionWindow::WindowId() const {
    return window_ ? SDL_GetWindowID(window_) : 0u;
}

bool SessionWindow::SetVSync(bool enabled) {
    vsync_ = enabled;
    if (!context_) return true;
    SDL_GL_MakeCurrent(window_, context_);
    return SDL_GL_SetSwapInterval(enabled ? 1 : 0) == 0;
}

void SessionWindow::DrawableSize(int& width, int& height) const {
    width = height = 0;
    if (window_) SDL_GL_GetDrawableSize(window_, &width, &height);
}

SessionGui::EventResult SessionWindow::HandleEvent(const SDL_Event& event) {
    return gui_.ProcessEvent(event);
}

bool SessionWindow::SetFullscreen(bool enabled, std::string& error) {
    // The F1 panel's former checkbox: on = borderless (the desktop-covering window).
    return SetDisplayMode(enabled ? 2 : 0, error);
}

bool SessionWindow::SetDisplayMode(int mode, std::string& error) {
    if (!window_) { error = "no window"; return false; }
    if (mode < 0 || mode > 2) { error = "display mode out of range"; return false; }
    const Uint32 flags = mode == 1 ? SDL_WINDOW_FULLSCREEN : mode == 2 ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u;
    if (mode == 1) {
        // Exclusive fullscreen takes the display's current mode (no mode switch:
        // the picture is fit at the display's shape, as the borderless mode is).
        SDL_DisplayMode current{};
        const int index = SDL_GetWindowDisplayIndex(window_);
        if (index >= 0 && SDL_GetCurrentDisplayMode(index, &current) == 0) SDL_SetWindowDisplayMode(window_, &current);
    }
    if (SDL_SetWindowFullscreen(window_, flags) != 0) {
        error = SDL_GetError();
        return false;
    }
    const Uint32 now = SDL_GetWindowFlags(window_);
    fullscreen_ = (now & SDL_WINDOW_FULLSCREEN) != 0;   // FULLSCREEN_DESKTOP carries the FULLSCREEN bit
    display_mode_ = (now & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP ? 2 : fullscreen_ ? 1 : 0;
    if (display_mode_ != mode) { error = "the window did not take the requested mode"; return false; }
    return true;
}

int SessionWindow::DisplayRefreshHz() const {
    if (!window_) return 0;
    SDL_DisplayMode current{};
    const int index = SDL_GetWindowDisplayIndex(window_);
    if (index < 0 || SDL_GetCurrentDisplayMode(index, &current) != 0) return 0;
    return current.refresh_rate;
}

bool SessionWindow::Upload(const std::vector<uint8_t>& rgba, int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        rgba.size() != static_cast<size_t>(width) * height * 4 || rgba.size() > (64u << 20)) return false;
    SDL_GL_MakeCurrent(window_, context_);
    if (!texture_) glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texture_width_ != width || texture_height_ != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        texture_width_ = width;
        texture_height_ = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    gui_.SetPicture(texture_, width, height);
    return true;
}

void SessionWindow::Render(int& width, int& height) {
    SDL_GL_MakeCurrent(window_, context_);
    gui_.BuildFrame();
    width = height = 0;
    SDL_GL_GetDrawableSize(window_, &width, &height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    gui_.Render();
}

void SessionWindow::Draw() {
    int width = 0, height = 0;
    Render(width, height);
    SDL_GL_SwapWindow(window_);
}

bool SessionWindow::DumpPresented(const std::string& path, std::string& error) {
    if (!window_ || !context_) { error = "no window"; return false; }
    int width = 0, height = 0;
    Render(width, height);   // the back buffer holds the frame about to be presented
    if (width <= 0 || height <= 0) { error = "empty drawable"; return false; }
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    SDL_GL_SwapWindow(window_);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "cannot write " + path; return false; }
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y)
        for (int x = 0; x < width; ++x) std::fwrite(rgba.data() + (static_cast<size_t>(y) * width + x) * 4, 1, 3, f);
    std::fclose(f);
    return true;
}

}  // namespace ps2r::app
