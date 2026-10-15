#pragma once
// ClockCensus: the clock contract's census (rule 5) -- per 1 s WALL window,
// how many game boundaries fired (game_hz), how many fields the time domain
// delivered (field_hz) and how many frames the presenter showed (render_fps),
// as series over the run. Game speed is the boundaries' and fields' business;
// the render rate is whatever the renderer achieved -- the two series are
// published side by side so a heavy spot reads as render_fps dropping with
// game_hz steady. The latest completed window feeds the session panel (F1).
//
// Window attribution: a boundary belongs to the window of its first field's
// WALL EDGE (the pacer's tick time, SetEdgeLookup -- TickThread::EdgeAt), not
// of the moment it completed. The edges are exactly 1/60 s apart, so a window
// counts the edges the game answered in that second: 60 when every frame was
// answered, 59 when one frame ran two fields -- never the 59/61 pair a boundary
// crossing a window edge by its answer latency would print. Without a lookup
// (tests, no pacer) the completion time is used.
//
// ee_cpu_ms (track 1d): the game thread's own CPU time per window, in ms --
// NoteBoundary reads the calling thread's CPU clock (CLOCK_THREAD_CPUTIME_ID)
// and credits the delta since the previous boundary to the window; with game_hz
// it reads as CPU ms per frame, the EE's cost independent of what the machine's
// other processes did to the wall (a loaded machine stretches a frame's wall
// time, not its CPU time). 1000 in a window = the thread was never off-CPU.
//
// Fast-forward (qol.fast_forward): an engaged cutscene fast-forward delivers
// extra Deferred fields per boundary that the PACER never ticked -- the domain
// tick runs ahead of the pacer's by BoundarySequencer::FastForwardFields().
// The caller passes that running total; the census subtracts it to recover the
// pacer tick the edge lookup is keyed by, so attribution stays by wall edge
// through and after a fast-forward (without it the lookup would miss -- tick >
// noted -- and fall back to completion time for the rest of the run). The
// `fields` count keeps every delivered field, fast-forward ones included:
// field_hz reads 60*N during an engaged fast-forward while game_hz stays 60.
//
// Threads: NoteBoundary runs on the game thread, NotePresent on the presenter's
// thread; a short lock orders them. Windows open at the first boundary; the
// partial last window is not part of the series.
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::app {

class ClockCensus {
public:
    using Clock = std::chrono::steady_clock;

    struct Window {
        uint32_t boundaries = 0;
        uint32_t fields = 0;
        uint32_t presents = 0;
        uint32_t cpu_ms = 0;     // the game thread's CPU time credited to this window
    };

    // The wall edge of pacer tick `tick` (1-based; == the domain tick it became),
    // false when unknown. Set once by the host before the first boundary.
    using EdgeLookup = std::function<bool(uint64_t tick, Clock::time_point* edge)>;
    void SetEdgeLookup(EdgeLookup lookup);

    // Game thread: a boundary completed with the domain at `domain_tick`, of
    // which `fast_forward_fields` (running total) were never pacer ticks.
    void NoteBoundary(uint64_t domain_tick, uint64_t fast_forward_fields = 0) {
        NoteBoundaryAt(domain_tick, Clock::now(), ThreadCpuNs(), fast_forward_fields);
    }
    // Presenter thread: one frame reached the window.
    void NotePresent() { NotePresentAt(Clock::now()); }
    // Explicit-time forms (tests).
    void NoteBoundaryAt(uint64_t domain_tick, Clock::time_point now);
    // ... with the game thread's CPU clock reading at the boundary (ns; the
    // default form reads it from the calling thread) and the fast-forward total.
    void NoteBoundaryAt(uint64_t domain_tick, Clock::time_point now, uint64_t thread_cpu_ns, uint64_t fast_forward_fields = 0);
    void NotePresentAt(Clock::time_point now);

    // The completed windows' series as "n,n,n" (empty before the first completes).
    std::string GameHzSeries() const;
    std::string FieldHzSeries() const;
    std::string RenderFpsSeries() const;
    std::string EeCpuMsSeries() const;
    // The game thread's CPU time over every boundary noted so far (ms).
    uint64_t EeCpuTotalMs() const;
    std::vector<Window> Windows() const;
    // The latest completed window's values (0 before the first completes).
    uint32_t LatestGameHz() const;
    uint32_t LatestRenderFps() const;

private:
    void RollLocked(Clock::time_point now);
    std::string SeriesLocked(uint32_t Window::*field) const;
    static uint64_t ThreadCpuNs();

    mutable std::mutex mu_;
    EdgeLookup edge_lookup_;
    bool started_ = false;
    Clock::time_point window_start_{};
    uint64_t last_tick_ = 0;          // the PACER tick of the previous boundary (domain tick - fast-forward fields)
    uint64_t last_domain_tick_ = 0;   // the domain tick of the previous boundary (the fields count)
    uint64_t last_cpu_ns_ = 0;      // the game thread's CPU clock at the previous boundary
    uint64_t cpu_carry_ns_ = 0;     // sub-millisecond remainder carried into the next credit
    uint64_t cpu_total_ns_ = 0;
    Window current_{};
    std::vector<Window> done_;
};

}  // namespace ps2r::app
