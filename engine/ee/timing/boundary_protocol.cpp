#include "engine/ee/timing/boundary_protocol.h"

#include "engine/ee/timing/tick_profile.h"

#include <chrono>
#include <string>

BoundaryProtocol::BoundaryProtocol(TickSource& source, TickSync& ticks) : source_(source), ticks_(ticks) {}

void BoundaryProtocol::SetRunahead(uint32_t lag) {
    if (lag < kRunaheadMin) lag = kRunaheadMin;
    if (lag > kRunaheadMax) lag = kRunaheadMax;
    runahead_ = lag;
}

void BoundaryProtocol::Wait(uint64_t) {
    ++waits_;
    if (source_.policy() == TickSource::Policy::Deterministic) {
        if (is_guest_ && !is_guest_()) {
            ticks_.NotePending(TickSync::Cause::Demand);  // fired at the next boundary
            ++deferred_;
            return;
        }
        ticks_.FireInline(TickSync::Cause::Demand);  // one field, demanded in-line
        return;
    }
    // Play: park until the pacer's next tick, bounded by one field period
    // (an absolute deadline, never a poll quantum). The pacer is a non-guest
    // thread and only NOTES its ticks; the waiter is the guest fixed point
    // that fires them, so the clock it re-reads has advanced (a non-guest
    // waiter leaves them for the next boundary).
    std::unique_lock<std::mutex> lk(mu_);
    const uint64_t seen = tick_generation_;
    const uint32_t period_us = source_.PeriodUs() ? source_.PeriodUs() : TickSource::kBasePeriodUs;
    cv_.wait_for(lk, std::chrono::microseconds(period_us), [&] { return tick_generation_ != seen; });
    lk.unlock();
    if (!is_guest_ || is_guest_()) ticks_.FirePending();
}

void BoundaryProtocol::MarkWorkWindow() {
    work_marked_ = true;
    work_marked_max_us_ = 0;
}

void BoundaryProtocol::NoteWork(uint64_t ordinal, uint64_t work_us, uint64_t pending, uint32_t period_us) {
    // The tick's host-site breakdown (TickProfile, the game thread's accumulators
    // since the previous boundary), read and reset whether or not the tick was long.
    const auto sample = ps2r::ee::TickProfile::Take();
    for (size_t i = 0; i < ps2r::ee::TickProfile::kSiteCount && i < 16; ++i) {
        site_total_us_[i] += sample.us[i];
        site_total_calls_[i] += sample.calls[i];
    }
    if (sample.chunk_events != 0) {
        ++chunk_ticks_;
        if (sample.chunk_kinds & ps2r::ee::TickProfile::ChunkCut) {
            ++chunk_cut_count_;
            if (chunk_cut_count_ <= kCutTraceMax) {
                if (!chunk_cuts_.empty()) chunk_cuts_ += ',';
                chunk_cuts_ += std::to_string(ordinal);
            }
        }
    }
    if (sample.load_events != 0 || sample.chunk_events != 0) {
        ++load_ticks_;
        if (!load_windows_.empty() && ordinal - load_windows_.back().last <= kLoadGapTicks) load_windows_.back().last = ordinal;
        else load_windows_.push_back({ordinal, ordinal});
    }
    if (work_us >= work_max_us_) {
        work_max_us_ = work_us;
        work_max_breakdown_ = std::to_string(ordinal) + ':' + std::to_string(work_us) + ':' + std::to_string(pending) + ':' +
                              ps2r::ee::TickProfile::Format(sample, work_us);
    }
    if (work_us > period_us) {
        ++long_trace_count_;
        if (long_trace_count_ <= kLongTraceMax) {
            if (!long_trace_.empty()) long_trace_ += ';';
            long_trace_ += std::to_string(ordinal) + ':' + std::to_string(work_us) + ':' + std::to_string(pending) + ':' +
                           ps2r::ee::TickProfile::Format(sample, work_us);
        }
    }
}

std::string BoundaryProtocol::SiteTotals() const {
    std::string out;
    for (size_t i = 0; i < ps2r::ee::TickProfile::kSiteCount && i < 16; ++i) {
        if (!out.empty()) out += ',';
        out += ps2r::ee::TickProfile::Name(static_cast<ps2r::ee::TickProfile::Site>(i));
        out += '=';
        out += std::to_string(site_total_us_[i] / 1000);
        out += '/';
        out += std::to_string(site_total_calls_[i]);
    }
    return out;
}

std::string BoundaryProtocol::LoadWindows() const {
    std::string out;
    for (const auto& w : load_windows_) {
        if (!out.empty()) out += ',';
        out += std::to_string(w.first) + '-' + std::to_string(w.last);
    }
    return out.empty() ? "none" : out;
}

bool BoundaryProtocol::ParkForPacerTick() {
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t period_us = source_.PeriodUs() ? source_.PeriodUs() : TickSource::kBasePeriodUs;
    // The frame's own work: from the previous boundary's release to here.
    uint64_t work_us = 0;
    const uint64_t pending = ticks_.Pending();
    if (work_from_valid_) {
        work_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t0 - work_from_).count());
        if (work_us > work_max_us_) work_max_us_ = work_us;
        if (work_marked_ && work_us > work_marked_max_us_) work_marked_max_us_ = work_us;
        NoteWork(pace_parks_ + pace_late_, work_us, pending, period_us);   // the late trace's ordinal
    }
    if (pending != 0) {   // the pacer ticked during the frame's work: late, no park
        ++pace_late_;
        if (pending >= 2) {
            ++late_trace_count_;
            if (late_trace_count_ <= kLateTraceMax) {
                if (!late_trace_.empty()) late_trace_ += ',';
                late_trace_ += std::to_string(pace_parks_ + pace_late_) + ':' + std::to_string(work_us) + ':' +
                               std::to_string(pending);
            }
        }
        work_from_ = t0;
        work_from_valid_ = true;
        ps2r::ee::TickProfile::ArmOverrun(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count()), period_us);
        return false;
    }
    ++pace_parks_;
    std::unique_lock<std::mutex> lk(mu_);
    const uint64_t seen = tick_generation_;
    const bool ticked = cv_.wait_for(lk, std::chrono::microseconds(period_us) * kBoundaryParkPeriods,
                                     [&] { return tick_generation_ != seen || ticks_.Pending() != 0; });
    lk.unlock();
    const auto t1 = std::chrono::steady_clock::now();
    pace_slept_us_ += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    if (!ticked) ++pace_timeouts_;
    work_from_ = t1;
    work_from_valid_ = true;
    ps2r::ee::TickProfile::ArmOverrun(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1.time_since_epoch()).count()), period_us);
    return true;
}

std::function<void(uint64_t)> BoundaryProtocol::WaitHook() {
    return [this](uint64_t deadline) { Wait(deadline); };
}

void BoundaryProtocol::NotifyTick() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++tick_generation_;
    }
    cv_.notify_all();
}
