// gs/hle/present/aspect_selection.h -- AspectSelection: the ONE display aspect
// ratio of a session (video.aspect), shared by the title's aspect model (the
// game thread: the projection, the culling frustum, the HUD fit and the movie
// picture read it per call) and the presenter (the main thread fits the
// anamorphic frame into the window at the same ratio). `auto` follows the
// window: its drawable size is snapped to the nearest of the supported
// shapes (4:3, 16:10, 16:9, 21:9, 32:9) at the geometric midpoints, and a session
// without a window (headless) is 4:3 -- the console's TV, the deterministic
// lanes' identity. Lock-free: one atomic per field. Its own tiny library
// (ps2r_gs_aspect): the session presenter links no other GS library.
#pragma once

#include <atomic>
#include <cstdint>

namespace ps2r {
namespace gs {

class AspectSelection {
public:
    // The selectable shapes, in video.aspect's order (auto|4:3|16:9|16:10|21:9|32:9);
    // the composition root maps the Config enum onto this one (this layer
    // includes no Config).
    enum class Shape : uint8_t { Auto = 0, Narrow, Wide16_9, Wide16_10, Wide21_9, Wide32_9 };
    static constexpr double kNarrow = 4.0 / 3.0;   // the title's NarrowAspect (0x3faaaaab)
    static constexpr double kWide = 16.0 / 9.0;    // the title's WideAspect (0x3fe38e39)
    static constexpr double kWide16_10 = 16.0 / 10.0;
    static constexpr double kWide21_9 = 21.0 / 9.0;
    static constexpr double kWide32_9 = 32.0 / 9.0;

    explicit AspectSelection(Shape selection = Shape::Auto) : selection_(selection) {}

    Shape Selection() const { return selection_.load(std::memory_order_relaxed); }
    // A live change (the in-game Display page, Charter 2 track A): the model
    // answers the new ratio at its next call, the presenter at its next frame.
    // The model must have been composed live (AspectSelection::Live at install)
    // for a change away from 4:3 to show; the menu owner persists video.aspect.
    void SetSelection(Shape selection) { selection_.store(selection, std::memory_order_relaxed); }
    // The window's drawable size (any thread); 0x0 = no window.
    void SetWindow(int width, int height);
    // The ratio in force now: the explicit selection, or the snapped window
    // shape under `auto` (4:3 without a window).
    double Ratio() const;
    // The nearest supported shape for a window ratio (<= 0 = 4:3).
    static double Snap(double window_ratio);
    static double RatioOf(Shape selection);
    // "4:3" | "16:9" | "16:10" | "21:9" | "32:9" for a supported ratio.
    static const char* Name(double ratio);
    static bool IsNarrow(double ratio) { return ratio <= kNarrow + 1e-6; }
    // Whether the model needs to be composed at all: without a window only an
    // explicit wide ratio can differ from 4:3; with one, any selection can (auto
    // follows the window, the Display page changes the selection live).
    static bool Live(Shape selection, bool has_window);

private:
    std::atomic<Shape> selection_;
    std::atomic<uint64_t> window_{0};   // (w << 32) | h
};

}  // namespace gs
}  // namespace ps2r
