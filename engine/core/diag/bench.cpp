// core/diag/bench.cpp -- see bench.h.
#include "engine/core/diag/bench.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace ps2r {
namespace diag {

Bench& Bench::Instance() {
    static Bench b;
    return b;
}

void Bench::Configure(uint32_t frames_target, uint32_t window_start, const std::string& report_path) {
    enabled_ = frames_target > 0;
    if (!enabled_) return;
    frames_target_ = frames_target;
    window_start_requested_ = window_start ? window_start : 1200;
    // Effective window start: clamp into [1, N] so short runs still produce a
    // non-empty window. The requested value is preserved in the report.
    window_start_effective_ = window_start_requested_;
    if (window_start_effective_ >= frames_target_) window_start_effective_ = 1;
    report_path_ = report_path;

    t_ns_.reset(new uint64_t[frames_target_ + 1]());
    t_ns_[0] = PerfProfiler::NowNs();
    draws_cum_.reset(new uint64_t[frames_target_ + 1]());
    draws_live_cum_.reset(new uint64_t[frames_target_ + 1]());
    bundles_cum_.reset(new uint64_t[frames_target_ + 1]());
    vticks_.reset(new uint64_t[frames_target_ + 1]());
    const uint32_t max_window = frames_target_ - window_start_effective_ + 1;
    render_cap_ = BenchReport::SuggestBufferSize(max_window);
    render_buf_.reset(new char[render_cap_]);

    // A bench run without the profiler would report an all-zero perf block
    // that reads like a healthy frame: arm it.
    PerfProfiler::SetEnabled(true);
}

void Bench::RegisterCensusCollector(std::function<void(Census&)> fn) {
    if (enabled_) census_fn_ = std::move(fn);
}
void Bench::RegisterGatesCollector(std::function<void(Gates&)> fn) {
    if (enabled_) gates_fn_ = std::move(fn);
}
void Bench::RegisterMemorySampler(std::function<uint64_t()> fn) {
    if (enabled_) footprint_fn_ = std::move(fn);
}
void Bench::RegisterAnchors(std::function<uint64_t()> draws_cum,
                            std::function<uint64_t()> draws_live_cum,
                            std::function<uint64_t()> bundles_cum,
                            std::function<uint64_t()> vticks) {
    if (!enabled_) return;
    draws_fn_ = std::move(draws_cum);
    draws_live_fn_ = std::move(draws_live_cum);
    bundles_fn_ = std::move(bundles_cum);
    vticks_fn_ = std::move(vticks);
}

uint64_t Bench::SampleFootprint() const { return footprint_fn_ ? footprint_fn_() : 0; }

bool Bench::OnVsyncFrame() {
    if (!enabled_) return false;
    const uint32_t tick = ++bench_tick_;
    const uint64_t now = PerfProfiler::NowNs();
    if (tick <= frames_target_) {
        t_ns_[tick] = now;
        if (draws_fn_) draws_cum_[tick] = draws_fn_();
        if (draws_live_fn_) draws_live_cum_[tick] = draws_live_fn_();
        if (bundles_fn_) bundles_cum_[tick] = bundles_fn_();
        if (vticks_fn_) vticks_[tick] = vticks_fn_();
    }
    last_frame_.store(tick, std::memory_order_release);

    // Memory sampling every 8 frames.
    if (tick == 1 || (tick & 7) == 0) {
        const uint64_t b = SampleFootprint();
        if (tick == 1 || rss_start_b_.load(std::memory_order_relaxed) == 0)
            rss_start_b_.store(b, std::memory_order_relaxed);
        rss_end_b_.store(b, std::memory_order_relaxed);
        if (b > rss_peak_b_.load(std::memory_order_relaxed)) rss_peak_b_.store(b, std::memory_order_relaxed);
    }

    if (!gates_done_.load(std::memory_order_relaxed) && gates_fn_) {
        gates_fn_(gates_);
        gates_.collected = true;
        gates_done_.store(true, std::memory_order_release);
    }

    // Periodic census sample for the early-exit report path (seqlock).
    if (census_fn_ && ((tick & 31) == 0 || tick == 1)) {
        census_seq_.fetch_add(1, std::memory_order_acquire);
        census_fn_(census_latest_);
        census_seq_.fetch_add(1, std::memory_order_release);
    }

    // Window-start snapshot at tick == window_start-1 so the deltas cover
    // ticks [window_start, N] exactly.
    if (!window_snapped_.load(std::memory_order_relaxed) && window_start_effective_ >= 2 &&
        tick >= window_start_effective_ - 1) {
        perf_start_ = PerfProfiler::TakeSnapshot();
        if (census_fn_) census_fn_(census_start_);
        window_snapped_.store(true, std::memory_order_release);
    }

    if (tick >= frames_target_ && !finalized_.load(std::memory_order_relaxed)) {
        perf_final_ = PerfProfiler::TakeSnapshot();
        if (census_fn_) census_fn_(census_final_);
        const uint64_t b = SampleFootprint();
        rss_end_b_.store(b, std::memory_order_relaxed);
        if (b > rss_peak_b_.load(std::memory_order_relaxed)) rss_peak_b_.store(b, std::memory_order_relaxed);
        finalized_.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void Bench::FillInput(ReportInput& in, const char* exit_kind, bool have_finals) {
    const uint32_t last = last_frame_.load(std::memory_order_acquire);
    in.frames_target = frames_target_;
    in.frames_run = last < frames_target_ ? last : frames_target_;
    in.window_start_requested = window_start_requested_;
    in.window_start = window_start_effective_;
    in.exit_kind = exit_kind;
    if (gates_done_.load(std::memory_order_acquire)) in.gates = gates_;
    in.t_ns = t_ns_.get();
    in.draws_cum = draws_cum_.get();
    in.draws_live_cum = draws_live_cum_.get();
    in.bundles_cum = bundles_cum_.get();
    in.vticks = vticks_.get();

    const TotalsSnapshot pf = have_finals ? perf_final_ : PerfProfiler::TakeSnapshot();
    uint32_t wf = 0;
    if (in.frames_run >= in.window_start) wf = in.frames_run - in.window_start + 1;
    for (int i = 0; i < kSubsysCount; i++) {
        const uint64_t d = pf.ns[i] >= perf_start_.ns[i] ? pf.ns[i] - perf_start_.ns[i] : 0;
        in.perf_ms[i] = wf ? (double)d / 1e6 / (double)wf : 0.0;
    }

    if (have_finals) {
        in.census_final = census_final_;
    } else {
        for (int attempt = 0; attempt < 8; attempt++) {
            const uint32_t s0 = census_seq_.load(std::memory_order_acquire);
            if (s0 & 1u) continue;
            in.census_final = census_latest_;
            const uint32_t s1 = census_seq_.load(std::memory_order_acquire);
            if (s0 == s1) break;
        }
    }
    in.census_window = BenchReport::CensusDelta(census_start_, in.census_final);

    in.rss_start_mb = (double)rss_start_b_.load(std::memory_order_relaxed) / (1024.0 * 1024.0);
    uint64_t end_b = SampleFootprint();
    if (end_b == 0) end_b = rss_end_b_.load(std::memory_order_relaxed);
    uint64_t peak = rss_peak_b_.load(std::memory_order_relaxed);
    if (end_b > peak) peak = end_b;
    in.rss_end_mb = (double)end_b / (1024.0 * 1024.0);
    in.rss_peak_mb = (double)peak / (1024.0 * 1024.0);
}

bool Bench::WriteReportNow(const char* exit_kind, bool have_finals) {
    if (!enabled_ || render_cap_ == 0 || report_path_.empty()) return false;
    bool expected = false;
    if (!writing_.compare_exchange_strong(expected, true)) return false;

    ReportInput in;
    FillInput(in, exit_kind, have_finals);
    const size_t len = BenchReport::Render(in, render_buf_.get(), render_cap_);

    int fd = ::open(report_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = false;
    if (fd >= 0) {
        size_t off = 0;
        while (off < len) {
            ssize_t w = ::write(fd, render_buf_.get() + off, len - off);
            if (w <= 0) break;
            off += (size_t)w;
        }
        ::close(fd);
        ok = (off == len);
    }
    char line[512];
    BenchReport::SummaryLine(in, line, sizeof(line));
    std::fprintf(stderr, "\n%s\n", line);
    std::fprintf(stderr, "[R127-BENCH] report %s: %s\n", ok ? "written" : "WRITE FAILED",
                 report_path_.c_str());
    report_written_.store(ok, std::memory_order_release);
    writing_.store(false, std::memory_order_release);
    return ok;
}

bool Bench::WriteReportAtExit() {
    if (!enabled_) return false;
    const bool have_finals = finalized_.load(std::memory_order_acquire);
    return WriteReportNow(have_finals ? "frames_reached" : "early_exit", have_finals);
}

}  // namespace diag
}  // namespace ps2r
