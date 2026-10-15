// core/diag/perf_profiler.cpp -- see perf_profiler.h.
#include "engine/core/diag/perf_profiler.h"

#include <chrono>

namespace ps2r {
namespace diag {

std::atomic<bool>     PerfProfiler::s_enabled{false};
std::atomic<uint64_t> PerfProfiler::s_ns[kSubsysCount];
std::atomic<uint64_t> PerfProfiler::s_total_ns[kSubsysCount];
std::atomic<uint64_t> PerfProfiler::s_frames{0};
std::atomic<uint64_t> PerfProfiler::s_total_frames{0};
std::atomic<uint64_t> PerfProfiler::s_last_emit_ns{0};

const char* PerfProfiler::Name(int s) {
    static const char* const n[kSubsysCount] = {
        "ee",       "vu1",      "vif_gif",  "dmac",      "texdec",    "gl_submit", "readback",
        "present",  "gif_parse", "gs_kick", "flush",     "snap",      "qpush",     "gs_xfer",
        "vu0",      "snap_mat", "walk_tag", "walk_copy", "gl_tex",    "gl_unif",   "gl_draw",
        "gl_blendcp", "gl_fbo", "mtvu_sync", "ee_game",  "file_io",   "mpeg",      "dmac_park",
        "dmac_feed", "parse_oth", "p2copy"};
    return (s >= 0 && s < kSubsysCount) ? n[s] : "?";
}

uint64_t PerfProfiler::NowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

PerfProfiler::Scope::Scope(int subsys) : subsys_(subsys), t0_(Enabled() ? NowNs() : 0) {}
PerfProfiler::Scope::Scope(int subsys, bool enable)
    : subsys_(subsys), t0_((enable && Enabled()) ? NowNs() : 0) {}
PerfProfiler::Scope::~Scope() {
    if (t0_ == 0) return;
    Book(subsys_, NowNs() - t0_);
}

void PerfProfiler::Book(int subsys, uint64_t dt) {
    if (subsys < 0 || subsys >= kSubsysCount) return;
    s_ns[subsys].fetch_add(dt, std::memory_order_relaxed);
    s_total_ns[subsys].fetch_add(dt, std::memory_order_relaxed);
}

void PerfProfiler::FrameTick(std::FILE* out, void (*log_line)(const char*)) {
    if (!Enabled()) return;
    s_frames.fetch_add(1, std::memory_order_relaxed);
    s_total_frames.fetch_add(1, std::memory_order_relaxed);
    const uint64_t now = NowNs();
    uint64_t last = s_last_emit_ns.load(std::memory_order_relaxed);
    if (last != 0 && now - last < 1000000000ull) return;
    if (!s_last_emit_ns.compare_exchange_strong(last, now)) return;
    if (last == 0) return;   // first tick: establish the window only
    const double dt_s = (double)(now - last) / 1e9;
    const uint64_t frames = s_frames.exchange(0, std::memory_order_relaxed);
    const double fps = frames ? (double)frames / dt_s : 0.0;
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf), "[PERF] f=%llu fps=%.1f ms/f:",
                          (unsigned long long)s_total_frames.load(), fps);
    for (int i = 0; i < kSubsysCount && n > 0 && n < (int)sizeof(buf) - 32; i++) {
        const double ms = (double)s_ns[i].exchange(0, std::memory_order_relaxed) / 1e6 /
                          (frames ? (double)frames : 1.0);
        n += std::snprintf(buf + n, sizeof(buf) - n, " %s=%.2f", Name(i), ms);
    }
    if (out) std::fprintf(out, "%s\n", buf);
    if (log_line) log_line(buf);
}

void PerfProfiler::ExitSummary(std::FILE* out, void (*log_line)(const char*)) {
    if (!Enabled()) return;
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf), "[PERF-SUMMARY] frames=%llu total_ms:",
                          (unsigned long long)s_total_frames.load());
    for (int i = 0; i < kSubsysCount && n > 0 && n < (int)sizeof(buf) - 32; i++) {
        n += std::snprintf(buf + n, sizeof(buf) - n, " %s=%.0f", Name(i),
                           (double)s_total_ns[i].load() / 1e6);
    }
    if (out) std::fprintf(out, "%s\n", buf);
    if (log_line) log_line(buf);
}

TotalsSnapshot PerfProfiler::TakeSnapshot() {
    TotalsSnapshot t{};
    for (int i = 0; i < kSubsysCount; i++) t.ns[i] = s_total_ns[i].load(std::memory_order_relaxed);
    t.frames = s_total_frames.load(std::memory_order_relaxed);
    return t;
}

void PerfProfiler::Reset() {
    for (int i = 0; i < kSubsysCount; i++) {
        s_ns[i].store(0);
        s_total_ns[i].store(0);
    }
    s_frames.store(0);
    s_total_frames.store(0);
    s_last_emit_ns.store(0);
}

}  // namespace diag
}  // namespace ps2r
