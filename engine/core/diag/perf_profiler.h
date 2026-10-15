// core/diag/perf_profiler.h -- per-subsystem wall-time profiler (diagnostics
// builds only). Scoped timers accumulate nanoseconds into named buckets;
// FrameTick emits one "[PERF] f=... fps=... ms/f: ee=... vu1=..." line per
// second; TakeSnapshot exposes the monotonic totals the bench window deltas.
// Bucket order and names are FROZEN: the bench report keys and the offline
// tooling read them.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace ps2r {
namespace diag {

enum Subsys : int {
    EE = 0,
    VU1,
    VIF_GIF,
    DMAC,
    TEXDECODE,
    GL_SUBMIT,
    READBACK,
    PRESENT_WAIT,
    GIF_PARSE,
    GS_KICK,
    BATCH_FLUSH,
    SNAPSHOT,
    QUEUE_PUSH,
    GS_XFER,
    VU0,
    SNAP_MAT,
    WALK_TAG,
    WALK_COPY,
    GL_TEX,
    GL_UNIF,
    GL_DRAW,
    GL_BLENDCP,
    GL_FBO,
    MTVU_SYNC,
    EE_GAME,
    FILE_IO,
    MPEG,
    DMAC_PARK,
    DMAC_FEED,
    GIF_PARSE_OTH,
    GIF_P2COPY,
    kSubsysCount
};

struct TotalsSnapshot {
    uint64_t ns[kSubsysCount];
    uint64_t frames;
};

class PerfProfiler {
public:
    // RAII scope: books wall time into `subsys` when the profiler is enabled
    // (a disabled profiler costs one cached-bool branch per scope).
    class Scope {
    public:
        explicit Scope(int subsys);
        Scope(int subsys, bool enable);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        int      subsys_;
        uint64_t t0_;
    };

    static const char* Name(int subsys);
    static uint64_t NowNs();

    // Enable/disable (diag.perf); read on every scope entry.
    static void SetEnabled(bool on) { s_enabled.store(on, std::memory_order_relaxed); }
    static bool Enabled() { return s_enabled.load(std::memory_order_relaxed); }

    static void Book(int subsys, uint64_t dt_ns);

    // Once per presented frame: emits the [PERF] line to `out` (and `log_line`
    // when non-null) after >= 1 s since the last emit.
    static void FrameTick(std::FILE* out, void (*log_line)(const char*));
    // Cumulative summary line (frames + total ms per bucket).
    static void ExitSummary(std::FILE* out, void (*log_line)(const char*));
    // Monotonic totals (never reset by the per-second emitter).
    static TotalsSnapshot TakeSnapshot();
    static void Reset();

private:
    static std::atomic<bool>     s_enabled;
    static std::atomic<uint64_t> s_ns[kSubsysCount];
    static std::atomic<uint64_t> s_total_ns[kSubsysCount];
    static std::atomic<uint64_t> s_frames;
    static std::atomic<uint64_t> s_total_frames;
    static std::atomic<uint64_t> s_last_emit_ns;
};

}  // namespace diag
}  // namespace ps2r

#define PS2R_PERF_CONCAT2(a, b) a##b
#define PS2R_PERF_CONCAT(a, b) PS2R_PERF_CONCAT2(a, b)
#define PS2R_PERF_SCOPE(subsys) \
    ::ps2r::diag::PerfProfiler::Scope PS2R_PERF_CONCAT(perf_scope_, __LINE__)(::ps2r::diag::subsys)
