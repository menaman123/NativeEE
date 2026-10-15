#pragma once
// CameraTrack -- the presenter's transform source for display-rate rendering
// (Charter 2 A3, video.render_rate=display). The game thread publishes the
// title's camera once per 60 Hz tick (RenderView::Update's world->camera matrix,
// TR include/game/view.h `toCamera`; the owned projection entry hands it over);
// the track keeps the TWO LATEST ticks and, for each presented frame, answers
// the camera interpolated between them at the presenter's phase -- the position
// the frame SHOULD show at that wall instant: p(t) = p_prev + (p_latest - p_prev) *
// phase, phase = (now - t_latest) / (t_latest - t_prev) clamped to 0..1. One tick of
// presentation latency, the OpenGOAL-style decoupling: the logic tick is never
// touched, the render rate is the display's.
//
// The trace (SetTrace): one line per presented frame -- `wall_ns tick phase
// x y z xn yn zn dup` -- the interpolated position, the latest tick's own
// position (what a tick-rate duplicate would show) and whether this present
// repeats the previous tick (the judder a 120 Hz display shows at 60 Hz
// content). RootHighFrameRate judges the trace: the interpolated track's
// per-present deltas have a bounded variance where the tick-rate columns
// alternate between zero and a full step.
//
// Threads: NoteCamera on the game thread, Interpolate/NotePresented on the
// presenter; a short lock orders them. Census (Record): hfr_camera=samples=,
// presents=,interpolated=,duplicates=,unsampled= (presents before the first sample),phase_mean_x1000=.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace ps2r::app {

class CameraTrack {
public:
    using Clock = std::chrono::steady_clock;
    struct Sample {
        bool valid = false;
        uint64_t ordinal = 0;           // the camera sample's own count
        Clock::time_point at{};
        float position[3] = {0, 0, 0};  // the camera's world position
    };
    struct Presented {
        bool valid = false;             // false before two samples exist
        bool interpolated = false;      // phase strictly inside (0,1) with distinct samples
        bool duplicate = false;         // the same latest tick as the previous present
        double phase = 0;
        uint64_t ordinal = 0;
        float position[3] = {0, 0, 0};
        float latest[3] = {0, 0, 0};
    };

    ~CameraTrack();
    // Game thread: the tick's world->camera matrix (16 floats, row-major as the
    // title stores it: rows are the basis vectors, row 3 the translation). The
    // camera's world position is the inverse's translation. The sample is
    // PENDING until the frame's boundary promotes it (NoteBoundary): the
    // presenter interpolates between boundary-stamped samples, whose times are
    // the pacer's regular edges, never the camera update's position inside the
    // frame's work.
    void NoteCamera(const float* to_camera16);
    // Game thread, the frame boundary: the pending sample becomes the latest
    // (stamped `now`), the former latest the previous. Nothing pending = no change.
    void NoteBoundary() { NoteBoundaryAt(Clock::now()); }
    void NoteBoundaryAt(Clock::time_point now);
    // Both at once (tests): a sample stamped `now`.
    void NoteCameraAt(const float* to_camera16, Clock::time_point now);
    // Presenter: the camera for a frame presented at `now`; records the present.
    Presented Present(Clock::time_point now);
    // Presenter: the trace file (empty = off). Opened lazily; closed at destruction.
    void SetTrace(std::string path);
    // The census row (any thread).
    std::string Census() const;
    uint64_t Samples() const;
    uint64_t Presents() const;
    uint64_t Interpolated() const;
    uint64_t Duplicates() const;

    // The world position of a world->camera matrix (the inverse's translation);
    // false when the matrix is singular.
    static bool CameraPosition(const float* to_camera16, float out[3]);

private:
    mutable std::mutex mu_;
    Sample previous_{}, latest_{}, pending_{};
    uint64_t samples_ = 0, presents_ = 0, interpolated_ = 0, duplicates_ = 0, unsampled_ = 0;
    uint64_t last_presented_ordinal_ = 0;
    double phase_sum_ = 0;
    std::string trace_path_;
    std::FILE* trace_ = nullptr;
    bool trace_failed_ = false;
};

}  // namespace ps2r::app
