// core/diag/bench.h -- the bench harness runtime (diagnostics builds only):
// per-frame timestamp ring, analysis-window snapshots of the profiler totals /
// census / memory, and the report write. Frame indexing is a PROCESS-LOCAL
// present tick (a savestate restore that reseeds the frame counter can neither
// trip the target early nor poison the interval statistics).
//
// The report is written ONLY to the configured path (--bench-report /
// --harness-out), never to the working directory.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "engine/core/diag/bench_report.h"
#include "engine/core/diag/perf_profiler.h"

namespace ps2r {
namespace diag {

class Bench {
public:
    static Bench& Instance();

    // Arms the bench: N frames, analysis window start (clamped into [1, N]),
    // report path. Enables the profiler so the perf block is never all-zero.
    void Configure(uint32_t frames_target, uint32_t window_start, const std::string& report_path);
    bool Enabled() const { return enabled_; }

    // Collectors (registered before the first frame).
    void RegisterCensusCollector(std::function<void(Census&)> fn);
    void RegisterGatesCollector(std::function<void(Gates&)> fn);
    void RegisterMemorySampler(std::function<uint64_t()> footprint_bytes_fn);
    // Per-frame anchor sources (cumulative counters sampled at each tick).
    void RegisterAnchors(std::function<uint64_t()> draws_cum, std::function<uint64_t()> draws_live_cum,
                         std::function<uint64_t()> bundles_cum, std::function<uint64_t()> vticks);

    // Present-path hook (one thread). Returns true when the target was reached
    // on this tick (the caller requests the clean shutdown).
    bool OnVsyncFrame();

    // Report writes: at exit (finals when the target was reached, otherwise an
    // early-exit snapshot). Returns true when the file was written.
    bool WriteReportAtExit();
    bool WriteReportNow(const char* exit_kind, bool have_finals);
    void FillInput(ReportInput& in, const char* exit_kind, bool have_finals);

    uint32_t FramesTarget() const { return frames_target_; }
    uint32_t LastTick() const { return last_frame_.load(std::memory_order_acquire); }
    bool Finalized() const { return finalized_.load(std::memory_order_acquire); }
    const std::string& ReportPath() const { return report_path_; }

private:
    Bench() = default;
    uint64_t SampleFootprint() const;

    bool        enabled_ = false;
    uint32_t    frames_target_ = 0;
    uint32_t    window_start_requested_ = 1200;
    uint32_t    window_start_effective_ = 1200;
    std::string report_path_;

    std::unique_ptr<uint64_t[]> t_ns_;
    std::unique_ptr<uint64_t[]> draws_cum_;
    std::unique_ptr<uint64_t[]> draws_live_cum_;
    std::unique_ptr<uint64_t[]> bundles_cum_;
    std::unique_ptr<uint64_t[]> vticks_;
    uint32_t                    bench_tick_ = 0;
    std::atomic<uint32_t>       last_frame_{0};

    std::unique_ptr<char[]> render_buf_;
    size_t                  render_cap_ = 0;

    std::function<void(Census&)>  census_fn_;
    std::function<void(Gates&)>   gates_fn_;
    std::function<uint64_t()>     footprint_fn_;
    std::function<uint64_t()>     draws_fn_, draws_live_fn_, bundles_fn_, vticks_fn_;

    Gates             gates_;
    std::atomic<bool> gates_done_{false};
    TotalsSnapshot    perf_start_{};
    TotalsSnapshot    perf_final_{};
    Census            census_start_{};
    Census            census_final_{};
    Census            census_latest_{};
    std::atomic<uint32_t> census_seq_{0};
    std::atomic<bool> window_snapped_{false};
    std::atomic<bool> finalized_{false};

    std::atomic<uint64_t> rss_start_b_{0};
    std::atomic<uint64_t> rss_end_b_{0};
    std::atomic<uint64_t> rss_peak_b_{0};

    std::atomic<bool> report_written_{false};
    std::atomic<bool> writing_{false};
};

}  // namespace diag
}  // namespace ps2r
