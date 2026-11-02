#include "app/gui/fps_history.h"

namespace ps2r::app {

void FpsHistory::NoteShown(uint64_t now_ns) {
    times_[next_] = now_ns;
    next_ = (next_ + 1) % (kHistory + 1);
    if (count_ < kHistory + 1) ++count_;
    ++shown_;
}

FpsHistory::Stats FpsHistory::Compute(uint64_t now_ns) const {
    Stats s;
    if (count_ == 0) return s;
    // The ring's oldest entry first.
    const size_t oldest = count_ < kHistory + 1 ? 0 : next_;
    auto at = [&](size_t i) { return times_[(oldest + i) % (kHistory + 1)]; };
    // The gaps, oldest first, clamped to a visible maximum.
    for (size_t i = 1; i < count_; ++i) {
        const uint64_t a = at(i - 1), b = at(i);
        const float gap_ms = b > a ? static_cast<float>(b - a) * 1e-6f : 0.0f;
        s.gaps_ms[s.gap_count++] = gap_ms;
    }
    // The window: the pictures shown within the last kWindowNs. The figure is (n - 1) / span over
    // the pictures inside the window, so a steady 60 Hz reads 60.0 whatever the window's phase; a
    // window that holds one picture (or none) reads 0 -- and once the newest picture is older
    // than the window plus a period the counter is stale: 0.
    const uint64_t newest = at(count_ - 1);
    if (now_ns > newest && now_ns - newest > kWindowNs + 20000000) return s;
    size_t first = count_ - 1;
    while (first > 0 && newest - at(first - 1) <= kWindowNs) --first;
    const size_t n = count_ - first;
    if (n >= 2) {
        const uint64_t span = newest - at(first);
        if (span > 0) {
            s.fps = static_cast<float>(n - 1) * 1e9f / static_cast<float>(span);
            s.frame_ms = static_cast<float>(span) * 1e-6f / static_cast<float>(n - 1);
        }
    }
    return s;
}

}  // namespace ps2r::app
