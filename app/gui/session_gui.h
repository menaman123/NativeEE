#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "app/gui/session_panel.h"

struct ImGuiContext;
struct SDL_Window;
union SDL_Event;

namespace ps2r::app {

// Owns the sole ImGui context. Main builds UI and receives SDL events; the
// current GL owner renders it. A short lock serializes context access, never
// game work, IO, swaps or waiting for another owner.
class SessionGui {
public:
    struct EventResult {
        bool accepted = false;
        bool consumed = false;
        bool capture_keyboard = false;
        bool capture_mouse = false;
    };

    explicit SessionGui(SessionControls& controls);
    ~SessionGui();
    SessionGui(const SessionGui&) = delete;
    SessionGui& operator=(const SessionGui&) = delete;

    // Main thread, with this window's GL context current. Does not own SDL,
    // the window or the GL context. No settings file or extra windows created.
    bool Initialize(SDL_Window* window, void* gl_context, std::string& error);
    // Main thread only. Does not poll SDL; root forwards each event once.
    // Root must also suppress cached keyboard/mouse sampling while captured.
    EventResult ProcessEvent(const SDL_Event& event);
    // Main thread, no GL calls. False means no overlay; game presentation
    // still runs. Closed menu with no pending/recent notice: no ImGui frame or
    // backend polling. Notices show requests without capturing game input.
    bool BuildFrame();
    // Main-owned presentation texture; rows are bottom-up. Its owner keeps it
    // alive through Render. The game renderer never shares this GL context.
    void SetPicture(uint32_t texture, int width, int height);
    // The display shape the picture is fit at (video.aspect): the frame is
    // anamorphic (512x448 for any TV), so the rect's shape is the ratio's, not
    // the pixels'. <= 0 keeps the pixel shape (the pre-A1 presentation).
    void SetPictureAspect(double ratio);
    // The picture's rect in a display of (width, height): aspect > 0 fits a box
    // of that shape, else a box of the picture's pixel shape; centred.
    static void PictureRect(float display_w, float display_h, int picture_w, int picture_h, double aspect,
                            float& x, float& y, float& w, float& h);
    // Current GL owner, after game composition/capture and before swap.
    // Caller binds the presentation target; the backend restores GL state.
    bool Render();
    // Main thread after renderer quiescence, with this GL context current.
    // Explicit failure leaves ownership intact so root can retry safely.
    bool Shutdown(std::string& error);

private:
    bool OnMainThread() const;
    bool ContextCurrent() const;
    EventResult CaptureResult(bool consumed) const;

    SessionControls& controls_;
    SessionPanel panel_;
    mutable std::mutex mutex_;
    std::thread::id main_thread_;
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    ImGuiContext* context_ = nullptr;
    unsigned window_id_ = 0;
    bool frame_ready_ = false;
    bool was_open_ = false;
    uint32_t picture_ = 0;
    int picture_width_ = 0, picture_height_ = 0;
    double picture_aspect_ = 0.0;
};

}  // namespace ps2r::app
