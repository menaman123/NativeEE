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
    // Before Open: video.hidpi -- the drawable at the display's own pixels on a scaled display
    // (SDL_WINDOW_ALLOW_HIGHDPI); default off (a points-sized drawable).
    void SetHighDpi(bool on) { hidpi_ = on; }
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
    // Upload the picture into the window's texture and show it (the one-texture path).
    bool Upload(const std::vector<uint8_t>& rgba, int width, int height);
    // The staged path (the presenter's early upload): Stage() uploads the pixels into one of kStagedSlots
    // textures at the instant the picture ARRIVES (main thread, the pump) and returns its slot (-1 = refused);
    // Show(slot) only re-points the GUI's picture at it -- so the pacer's edge -> shown path carries no upload
    // (at video.scale=4 a 2048x1792 RGBA upload is milliseconds). Release(slot) frees the slot (an unshown picture
    // superseded). The slot of the shown picture stays taken until the next Show replaces it.
    static constexpr int kStagedSlots = 3;   // shown + the pacer's two waiting pictures
    int Stage(const std::vector<uint8_t>& rgba, int width, int height);
    void Show(int slot);
    void Release(int slot);
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
    // Main thread: the menu (its Controls page's binding model is set through it).
    SessionGui& Gui() { return gui_; }
    const SessionGui& Gui() const { return gui_; }

private:
    // The frame's draw without the swap (Draw and DumpPresented share it).
    void Render(int& width, int& height);
    SessionGui gui_;
    SDL_Window* window_ = nullptr;
    void* context_ = nullptr;
    uint32_t texture_ = 0;
    int texture_width_ = 0, texture_height_ = 0;
    struct Staged {
        uint32_t texture = 0;
        int width = 0, height = 0;
        bool taken = false;
    };
    Staged staged_[kStagedSlots];
    int shown_slot_ = -1;
    bool UploadInto(uint32_t& texture, int& tex_w, int& tex_h, const std::vector<uint8_t>& rgba, int width, int height);
    bool fullscreen_ = false, gui_ready_ = false, vsync_ = false;
    int display_mode_ = 0;
    bool hidpi_ = false;
};

}  // namespace ps2r::app
