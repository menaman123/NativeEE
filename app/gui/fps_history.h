#pragma once
// app/gui/fps_history.h -- the FPS counter's statistics (the renderer is fps_overlay.h): every picture SHOWN
// by the presenter is noted with its time; Compute() gives the pictures per second over the last 0.5 s, the
// mean frame time of that window and the per-picture gaps of the last ~2.5 s. Pure arithmetic, tested alone.
#include <cstddef>
#include <cstdint>

namespace ps2r::app {

class FpsHistory {
public:
    static constexpr size_t kHistory = 160;   // ~2.7 s of gaps at 60 Hz
    static constexpr uint64_t kWindowNs = 500000000;   // the fps figure's window: 0.5 s

    struct Stats {
        float fps = 0.0f;              // pictures shown per second over the window (0 = no pictures)
        float frame_ms = 0.0f;         // the window's mean gap between shown pictures (0 = unknown)
        size_t gap_count = 0;          // valid entries of gaps_ms (the oldest first)
        float gaps_ms[kHistory] = {};  // the last gap_count gaps, ms, oldest first
    };

    // A new picture was shown at `now_ns` (monotonic; the main thread).
    void NoteShown(uint64_t now_ns);
    // The statistics as of `now_ns`. A picture older than the window plus one period is not
    // counted as "shown within the window" -- the figure falls to 0 when the pictures stop.
    Stats Compute(uint64_t now_ns) const;
    uint64_t Shown() const { return shown_; }

private:
    uint64_t times_[kHistory + 1] = {};   // a ring of shown instants (kHistory gaps need kHistory + 1 instants)
    size_t count_ = 0;                    // valid entries, <= kHistory + 1
    size_t next_ = 0;                     // the ring's write position
    uint64_t shown_ = 0;
};

}  // namespace ps2r::app
