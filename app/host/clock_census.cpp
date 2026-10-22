#include "app/host/clock_census.h"

#include <ctime>

namespace ps2r::app {

uint64_t ClockCensus::ThreadCpuNs() {
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

void ClockCensus::RollLocked(Clock::time_point now) {
    // Close every whole second that elapsed (an idle stretch yields zero windows).
    while (now - window_start_ >= std::chrono::seconds(1)) {
        done_.push_back(current_);
        current_ = Window{};
        window_start_ += std::chrono::seconds(1);
    }
}

void ClockCensus::SetEdgeLookup(EdgeLookup lookup) {
    std::lock_guard<std::mutex> lk(mu_);
    edge_lookup_ = std::move(lookup);
}

void ClockCensus::NoteBoundaryAt(uint64_t domain_tick, Clock::time_point now) {
    NoteBoundaryAt(domain_tick, now, ThreadCpuNs());
}

void ClockCensus::NoteBoundaryAt(uint64_t domain_tick, Clock::time_point now, uint64_t thread_cpu_ns, uint64_t fast_forward_fields) {
    std::lock_guard<std::mutex> lk(mu_);
    // The pacer's tick: the domain tick less the fast-forward fields the pacer
    // never ticked -- the key the edge lookup understands.
    const uint64_t pacer_tick = domain_tick >= fast_forward_fields ? domain_tick - fast_forward_fields : 0;
    // The boundary's time: its first field's wall edge when the pacer knows it
    // (never later than now, never before the current window), else now.
    Clock::time_point at = now;
    if (edge_lookup_ && (!started_ || pacer_tick > last_tick_)) {
        Clock::time_point edge;
        const uint64_t first_field = started_ ? last_tick_ + 1 : pacer_tick;
        if (edge_lookup_(first_field, &edge) && edge <= now) at = edge;
    }
    if (!started_) {
        started_ = true;
        window_start_ = at;
        last_tick_ = pacer_tick;
        last_domain_tick_ = domain_tick;
        last_cpu_ns_ = thread_cpu_ns;
    }
    if (at < window_start_) at = window_start_;
    RollLocked(at);
    current_.boundaries++;
    // The game thread's CPU since the previous boundary, credited to this window
    // in whole ms (the remainder carries, so the series sums to the total).
    if (thread_cpu_ns >= last_cpu_ns_) {
        const uint64_t delta = thread_cpu_ns - last_cpu_ns_ + cpu_carry_ns_;
        current_.cpu_ms += static_cast<uint32_t>(delta / 1000000ull);
        cpu_carry_ns_ = delta % 1000000ull;
        cpu_total_ns_ += thread_cpu_ns - last_cpu_ns_;
    }
    last_cpu_ns_ = thread_cpu_ns;
    if (domain_tick > last_domain_tick_) current_.fields += static_cast<uint32_t>(domain_tick - last_domain_tick_);
    last_domain_tick_ = domain_tick;
    if (pacer_tick > last_tick_) last_tick_ = pacer_tick;
}

void ClockCensus::NotePresentAt(Clock::time_point now) { NotePresentAt(now, 0); }

void ClockCensus::NotePresentAt(Clock::time_point now, uint64_t thread_cpu_ns) {
    std::lock_guard<std::mutex> lk(mu_);
    // The presenter's CPU clock is tracked from its first present on, so the
    // first credited delta never spans the run-up before the first boundary.
    const bool have_cpu = thread_cpu_ns != 0;
    const bool first_cpu = have_cpu && gs_last_cpu_ns_ == 0;
    if (first_cpu) gs_last_cpu_ns_ = thread_cpu_ns;
    if (!started_) return;   // presents before the first boundary belong to no window
    RollLocked(now);
    current_.presents++;
    if (have_cpu && !first_cpu && thread_cpu_ns >= gs_last_cpu_ns_) {
        const uint64_t delta = thread_cpu_ns - gs_last_cpu_ns_ + gs_cpu_carry_ns_;
        current_.gs_cpu_ms += static_cast<uint32_t>(delta / 1000000ull);
        gs_cpu_carry_ns_ = delta % 1000000ull;
    }
    if (have_cpu) gs_last_cpu_ns_ = thread_cpu_ns;
}

void ClockCensus::NoteMemory(uint64_t page_faults, uint64_t footprint_bytes) {
    std::lock_guard<std::mutex> lk(mu_);
    if (have_memory_ && started_) {
        if (page_faults >= last_faults_) current_.faults += static_cast<uint32_t>(page_faults - last_faults_);
        const int64_t d = static_cast<int64_t>(footprint_bytes) - static_cast<int64_t>(last_footprint_);
        current_.footprint_kb += static_cast<int32_t>(d / 1024);
    }
    have_memory_ = true;
    last_faults_ = page_faults;
    last_footprint_ = footprint_bytes;
}

std::string ClockCensus::SeriesLocked(uint32_t Window::*field) const {
    std::string s;
    for (const Window& w : done_) {
        if (!s.empty()) s += ',';
        s += std::to_string(w.*field);
    }
    return s;
}

std::string ClockCensus::GameHzSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::boundaries);
}

std::string ClockCensus::FieldHzSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::fields);
}

std::string ClockCensus::RenderFpsSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::presents);
}

std::string ClockCensus::EeCpuMsSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::cpu_ms);
}

std::string ClockCensus::GsCpuMsSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::gs_cpu_ms);
}

std::string ClockCensus::FaultsSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    return SeriesLocked(&Window::faults);
}

std::string ClockCensus::FootprintKbSeries() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string s;
    for (const Window& w : done_) {
        if (!s.empty()) s += ',';
        s += std::to_string(w.footprint_kb);
    }
    return s;
}

uint64_t ClockCensus::EeCpuTotalMs() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cpu_total_ns_ / 1000000ull;
}

std::vector<ClockCensus::Window> ClockCensus::Windows() const {
    std::lock_guard<std::mutex> lk(mu_);
    return done_;
}

uint32_t ClockCensus::LatestGameHz() const {
    std::lock_guard<std::mutex> lk(mu_);
    return done_.empty() ? 0u : done_.back().boundaries;
}

uint32_t ClockCensus::LatestRenderFps() const {
    std::lock_guard<std::mutex> lk(mu_);
    return done_.empty() ? 0u : done_.back().presents;
}

}  // namespace ps2r::app
