// platform/sdl/window.h -- Window: the SDL host window an OpenGL context renders into.
//
// Headless runs (the replay tool, the GL tests) create a hidden 64x64 window so a
// context exists without anything on screen; the desktop host creates a visible
// window of the requested client size. The window owns SDL video initialisation for
// its lifetime and exposes swap / resize, and requests the GL 4.1 core attributes
// (GlContext::RequestAttributes) between video initialisation and window creation.
#pragma once

#include <cstdint>

struct SDL_Window;

namespace ps2r {
namespace platform {

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // SDL hints that must be set BEFORE the video subsystem initialises, the same on every
    // host (a hint the platform does not know is ignored): on Windows, per-monitor-v2 DPI
    // awareness -- without it a window on a scaled desktop is rendered at 96 dpi and bitmap-
    // stretched by DWM (blurry, and the stretch is paid at every present). Called by Create
    // and by the launcher's own video init.
    static void RequestVideoHints();
    // headless => hidden 64x64; else a visible, resizable w x h window titled `title`.
    bool Create(bool headless, int w, int h, const char* title);
    void Destroy();
    bool IsOpen() const { return window_ != nullptr; }
    bool Headless() const { return headless_; }

    void Resize(int w, int h);
    void Swap();
    int Width() const { return width_; }
    int Height() const { return height_; }
    SDL_Window* Handle() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    bool headless_ = false;
    bool video_inited_ = false;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace platform
}  // namespace ps2r
