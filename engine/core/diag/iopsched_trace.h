// core/diag/iopsched_trace.h -- the `iopsched` diagnostics category (log-only,
// diagnostics builds): one stderr line per IOP kernel scheduling event in the
// paired-census grammar the harness compares between trees under the FRAME
// policy:  [IOPSCHED] vf=<frame> <evt> <a> <b> t=<virtual us>
// Events come from IopKernel's trace hook (thread start/run/park/wake, sema
// wait/signal/handoff, delay, hb_beat, event schedule/fire, the rate-limited
// evq probe, rpc wait/wake). Mechanism only: no game address lives here.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>

namespace ps2r {
namespace diag {

class IopschedTrace {
public:
    using Sink = std::function<void(const char* evt, int64_t a, int64_t b, uint64_t t_us)>;

    // Registers the category with the DiagRegistry and returns a hook suitable
    // for IopKernel::SetTraceHook. `frame_of(t_us)` maps a kernel time to the
    // vf stamp (the FRAME clock's boundary count); `out` defaults to stderr.
    static Sink MakeHook(bool enabled, std::function<uint64_t(uint64_t)> frame_of,
                         std::FILE* out = nullptr);
    // The line grammar, exposed for the paired-census test.
    static void Format(char* buf, size_t n, uint64_t vf, const char* evt, int64_t a, int64_t b,
                       uint64_t t_us);
    static uint64_t Lines() { return lines_; }

    // The `watch` line (--diag iopsched=watch): a periodic kernel-state
    // watchdog on `out` -- one line every `period` from `format_line` (the
    // module table's FormatWatchLine: thread states/wait kinds + the unparked
    // census), the REALTIME stall discriminator. One watcher at a time; StopWatch
    // joins it. Returns false when a watcher is already running.
    using WatchFormatter = std::function<int(char* buf, size_t n)>;
    static bool StartWatch(WatchFormatter format_line, std::chrono::milliseconds period,
                           std::FILE* out = nullptr);
    static void StopWatch();
    static bool WatchRunning();
    static uint64_t WatchLines();

private:
    static uint64_t lines_;
};

}  // namespace diag
}  // namespace ps2r
