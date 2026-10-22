#include "engine/ee/timing/tick_profile.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace ps2r::ee {
namespace {

// The game thread's accumulators: written and read by the bound thread only
// (Take runs at the boundary on that thread), so plain storage behind the
// thread-local gate is enough.
struct Accumulators {
    uint64_t ns[TickProfile::kSiteCount] = {};
    uint32_t calls[TickProfile::kSiteCount] = {};
    uint64_t load_bytes = 0;
    uint32_t load_events = 0;
    uint32_t chunk_events = 0;
    uint8_t chunk_kinds = 0;
    uint32_t marks_overrun = 0;
    uint32_t marks_total = 0;
};
Accumulators g_acc;
// The tick's budget: marks / samples after this steady_clock ns are overruns.
std::atomic<uint64_t> g_overrun_at_ns{~uint64_t{0}};
// The tick in progress (its work origin; 0 = parked, nothing to sample).
std::atomic<uint64_t> g_tick_from_ns{0};
bool g_census = false;
// Published by SetFrameReader for a reader that is not wired yet (kept for the API; clang-cl
// -Wunused-but-set-global would otherwise fail the Windows build).
[[maybe_unused]] TickProfile::FrameReader g_frame_reader = nullptr;
thread_local bool t_game_thread = false;
// The nested scopes' wall time of the innermost open scope (exclusive booking).
thread_local uint64_t t_child_ns = 0;

// The SAMPLER (diag.tick_census): the running game thread's shadow call
// stack, viewed through the pointers its BindGameThread published (the
// provider), is read by a host thread every ~kSamplePeriodUs while a tick is
// in progress; the innermost kChainDepth frames form the sample's chain. The
// reads are racy by design (a torn chain is one wrong sample of hundreds);
// the game thread never waits for or writes to the sampler. g_view is the
// view of the bound thread that last touched the profile (one relaxed store).
constexpr uint32_t kSamplePeriodUs = 50;
TickProfile::StackViewProvider g_view_provider = nullptr;
thread_local TickProfile::StackView t_view;
std::atomic<const TickProfile::StackView*> g_view{nullptr};
inline void NoteRunning() { if (g_census) g_view.store(&t_view, std::memory_order_relaxed); }
struct Histogram {
    TickProfile::Chain chains[TickProfile::kCallerSlots];
    TickProfile::Self selfs[TickProfile::kCallerSlots];
    TickProfile::Self inclusive[TickProfile::kInclusiveSlots];
    uint32_t used = 0;
    uint32_t selfs_used = 0;
    uint32_t inclusive_used = 0;
    uint32_t samples = 0;
    uint32_t samples_overrun = 0;
};
std::mutex g_hist_mu;
Histogram g_hist;
std::atomic<bool> g_sampler_run{false};
std::thread g_sampler;

void SamplerLoop() {
    while (g_sampler_run.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::microseconds(kSamplePeriodUs));
        if (g_tick_from_ns.load(std::memory_order_relaxed) == 0) continue;
        const TickProfile::StackView* view = g_view.load(std::memory_order_relaxed);
        if (!view || !view->depth) continue;
        const uint64_t now = TickProfile::NowNs();
        const size_t depth = *view->depth;
        if (depth == 0 || depth > view->capacity) continue;
        uint32_t f[TickProfile::kChainDepth] = {};
        for (size_t n = 0; n < TickProfile::kChainDepth && n < depth; ++n)
            f[n] = view->func_addrs[(depth - 1 - n) * view->stride_u32];
        // The inclusive set: every distinct function among the innermost kInclusiveDepth frames.
        uint32_t incl[TickProfile::kInclusiveDepth];
        size_t incl_n = 0;
        for (size_t n = 0; n < TickProfile::kInclusiveDepth && n < depth; ++n) {
            const uint32_t fa = view->func_addrs[(depth - 1 - n) * view->stride_u32];
            bool dup = false;
            for (size_t i = 0; i < incl_n; ++i) if (incl[i] == fa) { dup = true; break; }
            if (!dup) incl[incl_n++] = fa;
        }
        const bool over = now > g_overrun_at_ns.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(g_hist_mu);
        ++g_hist.samples;
        if (over) ++g_hist.samples_overrun;
        for (size_t i = 0; i < incl_n; ++i) {
            bool incl_found = false;
            for (uint32_t k = 0; k < g_hist.inclusive_used; ++k)
                if (g_hist.inclusive[k].f == incl[i]) { ++g_hist.inclusive[k].samples; incl_found = true; break; }
            if (!incl_found && g_hist.inclusive_used < TickProfile::kInclusiveSlots) {
                g_hist.inclusive[g_hist.inclusive_used].f = incl[i];
                g_hist.inclusive[g_hist.inclusive_used++].samples = 1;
            }
        }
        bool found = false;
        for (uint32_t i = 0; i < g_hist.used; ++i) {
            TickProfile::Chain& c = g_hist.chains[i];
            if (c.f[0] == f[0] && c.f[1] == f[1] && c.f[2] == f[2]) {
                ++c.samples;
                if (over) ++c.samples_overrun;
                found = true;
                break;
            }
        }
        if (!found && g_hist.used < TickProfile::kCallerSlots) {
            TickProfile::Chain& c = g_hist.chains[g_hist.used++];
            for (size_t n = 0; n < TickProfile::kChainDepth; ++n) c.f[n] = f[n];
            c.samples = 1;
            c.samples_overrun = over ? 1 : 0;
        }
        // Table full: the sample is counted in `samples`, unattributed.
        bool self_found = false;
        for (uint32_t i = 0; i < g_hist.selfs_used; ++i)
            if (g_hist.selfs[i].f == f[0]) { ++g_hist.selfs[i].samples; self_found = true; break; }
        if (!self_found && g_hist.selfs_used < TickProfile::kCallerSlots) {
            g_hist.selfs[g_hist.selfs_used].f = f[0];
            g_hist.selfs[g_hist.selfs_used++].samples = 1;
        }
    }
}

// Stops the sampler thread at exit, BEFORE g_sampler's destructor runs (a joinable
// std::thread terminates in its destructor): std::atexit registered after g_sampler's
// initialization completes is sequenced before that destructor. A free function, not a
// destructor-only record (LintOneClass: one primary type per file, TickProfile).
void StopSampler() {
    if (g_sampler.joinable()) {
        g_sampler_run.store(false);
        g_sampler.join();
    }
}
const int g_sampler_stopper_registered = (std::atexit(StopSampler), 0);

constexpr const char* kNames[TickProfile::kSiteCount] = {
    "loader", "tex", "mesh", "shadow", "sky", "save", "alloc", "gs", "boundary", "vu1wait", "gifdrain",
};

}  // namespace

const char* TickProfile::Name(Site site) {
    return static_cast<size_t>(site) < kSiteCount ? kNames[site] : "?";
}

void TickProfile::BindGameThread() {
    t_game_thread = true;
    if (g_view_provider && !g_view_provider(&t_view)) t_view = StackView{};
}
bool TickProfile::IsGameThread() { return t_game_thread; }

TickProfile::Scope::Scope(Site site) : site_(site), active_(t_game_thread) {
    if (!active_) return;
    NoteRunning();
    t0_ns_ = NowNs();
    saved_child_ns_ = t_child_ns;
    t_child_ns = 0;
}

TickProfile::Scope::~Scope() {
    if (!active_) return;
    const uint64_t elapsed = NowNs() - t0_ns_;
    const uint64_t self = elapsed > t_child_ns ? elapsed - t_child_ns : 0;
    g_acc.ns[site_] += self;
    ++g_acc.calls[site_];
    t_child_ns = saved_child_ns_ + elapsed;
}

void TickProfile::NoteLoad(uint64_t bytes) {
    if (!t_game_thread) return;
    g_acc.load_bytes += bytes;
    ++g_acc.load_events;
}

void TickProfile::NoteChunkEvent(ChunkEvent kind) {
    if (!t_game_thread) return;
    ++g_acc.chunk_events;
    g_acc.chunk_kinds |= static_cast<uint8_t>(kind);
}

void TickProfile::SetFrameReader(FrameReader reader) { g_frame_reader = reader; }

void TickProfile::SetStackViewProvider(StackViewProvider provider) { g_view_provider = provider; }

void TickProfile::EnableCensus(bool on) {
    g_census = on;
    if (on && !g_sampler.joinable()) {
        g_sampler_run.store(true);
        g_sampler = std::thread(SamplerLoop);
    }
}

void TickProfile::ArmOverrun(uint64_t work_from_ns, uint32_t period_us) {
    g_overrun_at_ns.store(work_from_ns + uint64_t{period_us} * 1000, std::memory_order_relaxed);
    if (t_game_thread) NoteRunning();
    g_tick_from_ns.store(work_from_ns, std::memory_order_relaxed);
}

void TickProfile::Mark() {
    if (!t_game_thread) return;
    ++g_acc.marks_total;
    if (!g_census) return;
    NoteRunning();
    if (NowNs() > g_overrun_at_ns.load(std::memory_order_relaxed)) ++g_acc.marks_overrun;
}

uint64_t TickProfile::Sample::Sum() const {
    uint64_t total = 0;
    for (size_t i = 0; i < kSiteCount; ++i) total += us[i];
    return total;
}

TickProfile::Sample TickProfile::Take() {
    Sample s;
    if (!t_game_thread) return s;
    g_tick_from_ns.store(0, std::memory_order_relaxed);   // the sampler idles until the next arm
    for (size_t i = 0; i < kSiteCount; ++i) {
        s.us[i] = g_acc.ns[i] / 1000;
        s.calls[i] = g_acc.calls[i];
    }
    s.load_bytes = g_acc.load_bytes;
    s.load_events = g_acc.load_events;
    s.chunk_events = g_acc.chunk_events;
    s.chunk_kinds = g_acc.chunk_kinds;
    s.marks_overrun = g_acc.marks_overrun;
    s.marks_total = g_acc.marks_total;
    g_acc = Accumulators{};
    g_overrun_at_ns.store(~uint64_t{0}, std::memory_order_relaxed);
    if (g_census) {
        std::lock_guard<std::mutex> lk(g_hist_mu);
        for (size_t i = 0; i < kCallerSlots; ++i) {
            s.chains[i] = g_hist.chains[i];
            s.selfs[i] = g_hist.selfs[i];
        }
        for (size_t i = 0; i < kInclusiveSlots; ++i) s.inclusive[i] = g_hist.inclusive[i];
        s.samples = g_hist.samples;
        s.samples_overrun = g_hist.samples_overrun;
        g_hist = Histogram{};
    }
    return s;
}

std::string TickProfile::Format(const Sample& sample, uint64_t work_us) {
    std::string out;
    for (size_t i = 0; i < kSiteCount; ++i) {
        if (!out.empty()) out += ',';
        out += kNames[i];
        out += '=';
        out += std::to_string(sample.us[i]);
        out += '/';
        out += std::to_string(sample.calls[i]);
    }
    const uint64_t sum = sample.Sum();
    out += ",other=";
    out += std::to_string(work_us > sum ? work_us - sum : 0);
    // The census: the hook marks past the budget / total, then the sampled chains, most
    // sampled first (up to kChainsPrinted): "samples=over/total:f2>f1>f0:us:n:over,..."
    // (us = the chain's share of the tick's work by sample count).
    if (sample.marks_total != 0 || sample.samples != 0) {
        out += ",marks=" + std::to_string(sample.marks_overrun) + '/' + std::to_string(sample.marks_total);
    }
    if (sample.samples != 0) {
        out += ",samples=" + std::to_string(sample.samples_overrun) + '/' + std::to_string(sample.samples);
        bool used[kCallerSlots] = {};
        for (size_t k = 0; k < kChainsPrinted; ++k) {
            size_t best = kCallerSlots;
            for (size_t i = 0; i < kCallerSlots; ++i)
                if (!used[i] && sample.chains[i].samples != 0 &&
                    (best == kCallerSlots || sample.chains[i].samples > sample.chains[best].samples)) best = i;
            if (best == kCallerSlots) break;
            used[best] = true;
            const Chain& c = sample.chains[best];
            char buf[96];
            std::snprintf(buf, sizeof buf, ":%x>%x>%x:%llu:%u:%u", c.f[2], c.f[1], c.f[0],
                          static_cast<unsigned long long>(work_us * c.samples / sample.samples), c.samples, c.samples_overrun);
            out += buf;
        }
        out += ",self=";
        bool used_self[kCallerSlots] = {};
        for (size_t k = 0; k < kChainsPrinted; ++k) {
            size_t best = kCallerSlots;
            for (size_t i = 0; i < kCallerSlots; ++i)
                if (!used_self[i] && sample.selfs[i].samples != 0 &&
                    (best == kCallerSlots || sample.selfs[i].samples > sample.selfs[best].samples)) best = i;
            if (best == kCallerSlots) break;
            used_self[best] = true;
            char buf[48];
            std::snprintf(buf, sizeof buf, "%s%x:%llu:%u", k ? ":" : "", sample.selfs[best].f,
                          static_cast<unsigned long long>(work_us * sample.selfs[best].samples / sample.samples), sample.selfs[best].samples);
            out += buf;
        }
        // The inclusive owners: "incl=f:us:n,..." most inclusive first (a frame
        // whose share is ~the tick is the title update the tick belongs to).
        out += ",incl=";
        bool used_incl[kInclusiveSlots] = {};
        for (size_t k = 0; k < kInclusivePrinted; ++k) {
            size_t best = kInclusiveSlots;
            for (size_t i = 0; i < kInclusiveSlots; ++i)
                if (!used_incl[i] && sample.inclusive[i].samples != 0 &&
                    (best == kInclusiveSlots || sample.inclusive[i].samples > sample.inclusive[best].samples)) best = i;
            if (best == kInclusiveSlots) break;
            used_incl[best] = true;
            char buf[48];
            std::snprintf(buf, sizeof buf, "%s%x:%llu:%u", k ? ":" : "", sample.inclusive[best].f,
                          static_cast<unsigned long long>(work_us * sample.inclusive[best].samples / sample.samples),
                          sample.inclusive[best].samples);
            out += buf;
        }
    }
    return out;
}

}  // namespace ps2r::ee
