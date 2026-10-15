#include "engine/ee/cpu/guest_park.h"

#include <condition_variable>
#include <mutex>

namespace {
ParkRecord g_last;
uint32_t g_count = 0;
GuestPark::Sink g_sink;
std::mutex g_mutex;
std::condition_variable g_never;  // never notified: the parked thread sleeps here
}  // namespace

void GuestPark::Forever(const char* reason, uint32_t pc, const char* detail) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last.reason = reason;
        g_last.pc = pc;
        g_last.detail = detail;
        ++g_count;
    }
    if (g_sink) g_sink(g_last);  // may throw: that is the only way out
    std::unique_lock<std::mutex> lock(g_mutex);
    for (;;) g_never.wait(lock);
}

void GuestPark::SetSink(Sink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

const ParkRecord& GuestPark::Last() { return g_last; }
uint32_t GuestPark::Count() { return g_count; }
