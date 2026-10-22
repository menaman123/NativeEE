#include "app/host/tick_thread.h"

#include <chrono>

#include "engine/ee/timing/boundary_protocol.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/ee/timing/tick_sync.h"
#include "engine/platform/host/platform.h"

TickThread::TickThread(const TickSource& source, TickSync& ticks, BoundaryProtocol& protocol)
    : source_(source), ticks_(ticks), protocol_(protocol) {}

TickThread::~TickThread() { Stop(); }

void TickThread::Start() {
    if (thread_.joinable()) return;
    stop_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { Loop(); });
}

void TickThread::Stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void TickThread::Loop() {
    using Clock = std::chrono::steady_clock;
    // The pacer IS the wall clock the game runs on: the same interactive QoS the
    // GS consumer and the MTVU worker hold, so a loaded machine does not park the
    // edge train for a period (a re-anchor loses edges; the census read a 58/58
    // window from exactly that).
    ps2r::platform::host::PinCurrentThreadInteractive("ps2r-tick");
    // The edge train: anchor + k * period. The base period is EXACTLY 1/60 s
    // (a perfect 60 Hz console; the clock contract's wall edge), kept as a
    // rational in nanoseconds so no rounding accumulates; turbo periods
    // (PeriodUs = 16667 / K) take the microsecond value. The base train is the
    // RATIONAL anchor + k * 1e9 / 60 ns, evaluated per k: edge 60 is exactly one
    // second after edge 0 (a truncated 16,666,666 ns period times 60 is 40 ns
    // short, and a 1 s census window would then count 61 edges once).
    auto period_now = [this]() -> Clock::duration {
        const uint32_t us = source_.PeriodUs();
        if (us == 0 || us == TickSource::kBasePeriodUs)
            return std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(1000000000LL / 60));
        return std::chrono::duration_cast<Clock::duration>(std::chrono::microseconds(us));
    };
    auto edge_at = [](Clock::time_point anchor, Clock::duration period, uint64_t k) -> Clock::time_point {
        const auto base = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(1000000000LL / 60));
        if (period == base)
            return anchor + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::nanoseconds((static_cast<int64_t>(k) * 1000000000LL) / 60));
        return anchor + std::chrono::duration_cast<Clock::duration>(period * static_cast<int64_t>(k));
    };
    auto anchor = Clock::now();
    auto period = period_now();
    uint64_t k = 1;
    auto next = edge_at(anchor, period, k);
    while (!stop_.load(std::memory_order_acquire)) {
        // The host's precise timer (mach_wait_until / clock_nanosleep ABSTIME / a high-
        // resolution waitable timer): sleep_until landed 0.1-1 ms late under load and the
        // edge's lateness went straight into every boundary's wall time (round 3, L4). The
        // deadline is carried as "steady now + remaining" so the two clocks' epochs never
        // have to agree.
        {
            const auto remaining = next - Clock::now();
            if (remaining.count() > 0)
                ps2r::host::PreciseSleepUntilNs(ps2r::host::MonotonicNowNs() +
                                                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()));
        }
        if (stop_.load(std::memory_order_acquire)) break;
        // The edge this tick stands for, published before the tick is noted.
        const uint64_t tick = ticks_noted_.load(std::memory_order_relaxed) + 1;
        edge_ns_[tick % kEdgeRing].store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(next.time_since_epoch()).count(),
            std::memory_order_relaxed);
        ticks_.NotePending(TickSync::Cause::Demand);
        protocol_.NotifyTick();
        ticks_noted_.store(tick, std::memory_order_release);
        const auto now = Clock::now();
        const auto new_period = period_now();
        if (new_period != period) {   // mode change (turbo): a new train from now
            anchor = next;
            period = new_period;
            k = 0;
        }
        ++k;
        next = edge_at(anchor, period, k);
        if (next < now) {   // more than one period behind: re-anchor, no burst
            anchor = now;
            k = 1;
            next = edge_at(anchor, period, k);
            resyncs_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool TickThread::EdgeAt(uint64_t tick, int64_t* ns) const {
    const uint64_t noted = ticks_noted_.load(std::memory_order_acquire);
    if (tick == 0 || tick > noted || noted - tick >= kEdgeRing) return false;
    *ns = edge_ns_[tick % kEdgeRing].load(std::memory_order_relaxed);
    return true;
}
