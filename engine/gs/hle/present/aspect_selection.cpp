// gs/hle/present/aspect_selection.cpp -- see aspect_selection.h.
#include "engine/gs/hle/present/aspect_selection.h"

#include <cmath>

namespace ps2r {
namespace gs {

void AspectSelection::SetWindow(int width, int height) {
    if (width <= 0 || height <= 0) { window_.store(0, std::memory_order_relaxed); return; }
    window_.store((static_cast<uint64_t>(static_cast<uint32_t>(width)) << 32) | static_cast<uint32_t>(height),
                  std::memory_order_relaxed);
}

double AspectSelection::RatioOf(Shape selection) {
    switch (selection) {
        case Shape::Wide16_9: return kWide;
        case Shape::Wide16_10: return kWide16_10;
        case Shape::Wide21_9: return kWide21_9;
        case Shape::Wide32_9: return kWide32_9;
        case Shape::Narrow:
        case Shape::Auto:
        default: return kNarrow;
    }
}

double AspectSelection::Snap(double window_ratio) {
    if (!(window_ratio > 0.0)) return kNarrow;
    // Geometric midpoints between neighbouring shapes.
    if (window_ratio < std::sqrt(kNarrow * kWide16_10)) return kNarrow;
    if (window_ratio < std::sqrt(kWide16_10 * kWide)) return kWide16_10;
    if (window_ratio < std::sqrt(kWide * kWide21_9)) return kWide;
    if (window_ratio < std::sqrt(kWide21_9 * kWide32_9)) return kWide21_9;
    return kWide32_9;
}

double AspectSelection::Ratio() const {
    const Shape selection = Selection();
    if (selection != Shape::Auto) return RatioOf(selection);
    const uint64_t packed = window_.load(std::memory_order_relaxed);
    if (packed == 0) return kNarrow;
    const double w = static_cast<double>(static_cast<uint32_t>(packed >> 32));
    const double h = static_cast<double>(static_cast<uint32_t>(packed & 0xFFFFFFFFu));
    return Snap(w / h);
}

const char* AspectSelection::Name(double ratio) {
    if (IsNarrow(ratio)) return "4:3";
    if (ratio < std::sqrt(kWide16_10 * kWide)) return "16:10";
    if (ratio < std::sqrt(kWide * kWide21_9)) return "16:9";
    if (ratio < std::sqrt(kWide21_9 * kWide32_9)) return "21:9";
    return "32:9";
}

bool AspectSelection::Live(Shape selection, bool has_window) {
    // A window can change the ratio (auto follows it; the Display page may
    // change the selection live), so a windowed session always composes the
    // model (it passes 4:3 calls through untouched); without a window only an
    // explicit wide ratio can differ from 4:3.
    if (selection == Shape::Narrow || selection == Shape::Auto) return has_window;
    return true;
}

}  // namespace gs
}  // namespace ps2r
