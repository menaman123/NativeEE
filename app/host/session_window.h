#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/gui/session_gui.h"

namespace ps2r::app {

// Main-thread presentation owner, with a context separate from game rendering.
// Receives owned pixels only; no game resource or register state crosses here.
class SessionWindow {
public:
    explicit SessionWindow(SessionControls& controls);
    ~SessionWindow();
    bool Open(std::string& error);
    SessionGui::EventResult HandleEvent(const SDL_Event& event);
    bool SetFullscreen(bool enabled, std::string& error);
    // video.fullscreen (A2): 0 windowed (the resizable window), 1 fullscreen
    // (the display's own mode), 2 borderless (a window covering the desktop).
    bool SetDisplayMode(int mode, std::string& error);
    int DisplayMode() const { return display_mode_; }
    // The refresh rate of the display the window is on (Hz; 0 unknown).
    int DisplayRefreshHz() const;
    // Presenter-only: swap interval follows video.vsync. Never changes the
    // logical render-target size the game draws into.
    bool SetVSync(bool enabled);
    bool Upload(const std::vector<uint8_t>& rgba, int width, int height);
    // The display shape the picture is fit at (video.aspect; <= 0 = pixel shape).
    void SetPictureAspect(double ratio) { gui_.SetPictureAspect(ratio); }
    void Draw();
    // Draws once more and writes the window's framebuffer (the picture plus the
    // GUI overlays: the PRESENTED frame) as a P6 PPM, top-down. Main thread.
    bool DumpPresented(const std::string& path, std::string& error);
    bool Fullscreen() const { return fullscreen_; }
    bool VSync() const { return vsync_; }
    // Current drawable size (after a resize); zeros before the window exists.
    void DrawableSize(int& width, int& height) const;
    // The SDL window id the GUI matches key events against (0 before Open).
    uint32_t WindowId() const;
    // Main thread, after Open: the window's title bar text (the title's name).
    void SetTitle(const std::string& title);

private:
    // The frame's draw without the swap (Draw and DumpPresented share it).
    void Render(int& width, int& height);
    SessionGui gui_;
    SDL_Window* window_ = nullptr;
    void* context_ = nullptr;
    uint32_t texture_ = 0;
    int texture_width_ = 0, texture_height_ = 0;
    bool fullscreen_ = false, gui_ready_ = false, vsync_ = false;
    int display_mode_ = 0;
};

}  // namespace ps2r::app
