// core/diag/iopsched_trace.cpp -- IopschedTrace: the [IOPSCHED] line writer.
#include "engine/core/diag/iopsched_trace.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include "engine/core/diag/diag_registry.h"

namespace ps2r {
namespace diag {

uint64_t IopschedTrace::lines_ = 0;

namespace {
struct WatchState {
    std::mutex m;
    std::condition_variable cv;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<uint64_t> lines{0};
};
WatchState& Watch() {
    static WatchState w;
    return w;
}
}  // namespace

bool IopschedTrace::StartWatch(WatchFormatter format_line, std::chrono::milliseconds period, std::FILE* out) {
    WatchState& w = Watch();
    if (!format_line || w.running.exchange(true, std::memory_order_acq_rel)) return false;
    if (!out) out = stderr;
    w.thread = std::thread([format_line, period, out] {
        WatchState& ws = Watch();
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(ws.m);
                if (ws.cv.wait_for(lk, period, [&] { return !ws.running.load(std::memory_order_acquire); })) return;
            }
            char buf[256];
            const int n = format_line(buf, sizeof(buf));
            if (n > 0) {
                std::fprintf(out, "%s\n", buf);
                std::fflush(out);
                ws.lines.fetch_add(1, std::memory_order_acq_rel);
            }
        }
    });
    return true;
}

void IopschedTrace::StopWatch() {
    WatchState& w = Watch();
    if (!w.running.exchange(false, std::memory_order_acq_rel)) return;
    {
        std::lock_guard<std::mutex> lk(w.m);
        w.cv.notify_all();
    }
    if (w.thread.joinable()) w.thread.join();
}

bool IopschedTrace::WatchRunning() { return Watch().running.load(std::memory_order_acquire); }
uint64_t IopschedTrace::WatchLines() { return Watch().lines.load(std::memory_order_acquire); }

void IopschedTrace::Format(char* buf, size_t n, uint64_t vf, const char* evt, int64_t a, int64_t b,
                           uint64_t t_us) {
    std::snprintf(buf, n, "[IOPSCHED] vf=%llu %s %lld %lld t=%llu", (unsigned long long)vf, evt,
                  (long long)a, (long long)b, (unsigned long long)t_us);
}

IopschedTrace::Sink IopschedTrace::MakeHook(bool enabled, std::function<uint64_t(uint64_t)> frame_of,
                                            std::FILE* out) {
    DiagRegistry::Instance().Register("iopsched",
                                      "IOP kernel scheduling event stream ([IOPSCHED] vf= evt a b t=)",
                                      enabled);
    if (!enabled) return nullptr;
    if (!out) out = stderr;
    // The evq probe is rate-limited (one line per 65,536 occurrences), as the
    // paired census expects.
    auto evq_count = std::make_shared<uint64_t>(0);
    return [out, frame_of, evq_count](const char* evt, int64_t a, int64_t b, uint64_t t_us) {
        if (std::strcmp(evt, "evq") == 0 && ((*evq_count)++ & 0xFFFF) != 0) return;
        char line[160];
        Format(line, sizeof(line), frame_of ? frame_of(t_us) : 0, evt, a, b, t_us);
        std::fprintf(out, "%s\n", line);
        lines_++;
    };
}

}  // namespace diag
}  // namespace ps2r
