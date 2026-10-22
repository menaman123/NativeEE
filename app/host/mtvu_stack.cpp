#include "app/host/mtvu_stack.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "engine/ee/sched/ee_run_token.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_regs.h"
#include "engine/platform/host/platform.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/mtvu_clock.h"

namespace {
const char kWorkerThreadName[] = "vu1worker";
const char kDrainThreadName[] = "gifdrain";
}  // namespace

MtvuStack::MtvuStack(MtvuVifFeed& vif, ChainWalker& walker, GifArbiter& arbiter, VuTierDispatch& inline_vu1_dispatch,
                     const VuSettings& worker_vu_settings, bool xgkick_paced_drain, MtvuSettings settings,
                     const std::atomic<bool>* running)
    : worker_vu1_(1, &arbiter, xgkick_paced_drain),
      // No present pacer reports a cadence band in this root: the cap is the setting.
      guard_(settings.depth_frames, {}, MtvuClock::Steady()),
      backpressure_(guard_, MtvuClock::Steady()),
      worker_(vif, worker_vu1_, arbiter, backpressure_, settings),
      sync_(worker_, arbiter),
      feed_(walker, worker_),
      drain_(arbiter),
      arbiter_(arbiter),
      inline_dispatch_(inline_vu1_dispatch),
      boundary_presented_(*this) {
    // The worker's unit runs the same fallback tier as the inline one. The HLE
    // library consult is off until the composition root installs the worker's
    // own runtime on this unit (Vu1HleRuntime is per unit, driven from the
    // thread that executes it): without that hook the worker ran every VU1
    // program on the cached tier -- ~3x the library's cost -- and a play frame
    // of the first level kept the worker thread saturated (the s3 round).
    worker_vu1_.dispatch().settings() = worker_vu_settings;
    worker_vu1_.dispatch().settings().hle_enabled = false;
    backpressure_.SetFrameCounters(&declared_, &presented_);
    backpressure_.SetShutdownFlag(running);
    sync_.InstallInto(inline_dispatch_);
    // The deferred boundary's delivery is the frame's "present" for the
    // runahead guard when the root installed no consumer of its own.
    if (arbiter_.BoundaryHook() == nullptr) arbiter_.SetBoundaryHook(&boundary_presented_);
}

void MtvuStack::BoundaryPresented::OnBoundary(uint32_t /*seq*/, uint64_t /*enqueue_ns*/) { stack_.NotePresented(); }

MtvuStack::~MtvuStack() {
    Stop();
    MtvuSync::UninstallFrom(inline_dispatch_);
    if (arbiter_.BoundaryHook() == &boundary_presented_) arbiter_.SetBoundaryHook(nullptr);
}

void MtvuStack::OnThreadStart(void* name) {
    ps2r::platform::host::PinCurrentThreadInteractive(static_cast<const char*>(name));
}

void MtvuStack::Start(bool with_drain_thread) {
    if (worker_.IsRunning()) return;
    worker_.Start(&OnThreadStart, const_cast<char*>(kWorkerThreadName));
    if (with_drain_thread) drain_.Start(&OnThreadStart, const_cast<char*>(kDrainThreadName));
}

void MtvuStack::Stop() {
    if (worker_.IsRunning()) worker_.Stop();
    if (drain_.Running()) drain_.Stop();
}

void MtvuStack::SiteSync(SyncSite site) {
    if (sync_.DeferOn()) sync_.NoteSiteSkipped(site);
    else sync_.Sync(site);
}

void MtvuStack::FrameSync(uint32_t seq, uint64_t now_ns) {
    // This frame's VIF1 feed is complete (the next openBoundary consumes the
    // note; a boundary without one counts an order violation).
    arbiter_.noteFeedForBoundary();
    if (backpressure_.DropMode() && worker_.IsRunning()) {
        // The frame's mark behind its last span; the lag it reads decides the
        // next frame (dropped when the worker is > kDropLagFrames behind).
        worker_.SubmitFrameMark();
        backpressure_.NoteFrameSync(worker_.FrameLag());
    }
    if (defer_boundaries_ && sync_.DeferOn() && worker_.IsRunning()) {
        // Deferred boundary: an ordered slot behind every marker and packet of
        // the frame, then one non-blocking pass -- the sealed prefix (and the
        // boundary itself when nothing unsealed precedes it) is delivered now;
        // a heavy frame's boundary rides the later eager drains.
        declared_.fetch_add(1, std::memory_order_relaxed);
        arbiter_.openBoundary(seq, now_ns);
        arbiter_.drain();
        ++deferred_boundaries_;
    } else {
        // Terminal sync point: the worker finishes every queued span before the
        // frame's packet list is complete.
        sync_.Sync(SyncSite::SceGsSyncV);
        ++terminal_syncs_;
    }
}

void MtvuStack::NotePresented() {
    presented_.fetch_add(1, std::memory_order_relaxed);
    worker_.NotifyFrameProgress();
}

void MtvuStack::EnableEarlyFinish(FinishRaiser raiser, void* user) {
    early_finish_ = raiser != nullptr;
    arbiter_.SetEarlyFinish(raiser, user);
    if (early_finish_) worker_.SetStageObserver(&OnStagedSpan, &OnStagedReset, this);
    else worker_.SetStageObserver(nullptr, nullptr, nullptr);
}

void MtvuStack::ResyncEarlyFinish(uint32_t cl, uint32_t wl) {
    vif1_scan_.ResyncRegs(cl, wl);
    arbiter_.ResetEarlyFinish();
}

void MtvuStack::OnStagedSpan(void* self, const uint8_t* data, size_t size, bool tte) {
    auto* stack = static_cast<MtvuStack*>(self);
    const uint32_t found = stack->vif1_scan_.Feed(data, size, tte);
    if (found) stack->arbiter_.PreRaiseFinish(found);
}

void MtvuStack::OnStagedReset(void* self) { static_cast<MtvuStack*>(self)->vif1_scan_.ResetLatch(); }

bool MtvuStack::Quiesce(std::string* why) {
    sync_.Sync(SyncSite::Other);
    for (int lap = 0;; ++lap) {
        const bool pipeline_idle = !worker_.IsBusy() && worker_.PendingBytes() == 0;
        // A pre-raised FINISH not yet consumed means a staged packet the parser
        // has not reached: the INTC / GIFS sections must not be written before it.
        if (pipeline_idle && !arbiter_.hasPending() && arbiter_.PreRaisedFinish() == 0) {
            const uint64_t g1 = drain_.DrainIdleGeneration();
            const uint64_t g2 = drain_.DrainIdleGeneration();
            if (g1 != 0 && g1 == g2) return true;
        }
        if (lap >= kQuiesceLaps) {
            if (why) *why = "MTVU pipeline did not drain within the quiesce budget";
            return false;
        }
        ++quiesce_laps_;
        sync_.TryEagerDrain();
        ps2r::host::PreciseSleepUntilNs(ps2r::host::MonotonicNowNs() + kQuiesceLapNs);   // a 0.5 ms lap that is 0.5 ms
    }
}

void MtvuStack::MirrorVu1(const VuUnit& restored) {
    if (&restored == &worker_vu1_) return;
    std::memcpy(worker_vu1_.state().micro, restored.state().micro, Vu1State::kMemSize);
    std::memcpy(worker_vu1_.state().data, restored.state().data, Vu1State::kMemSize);
    worker_vu1_.regs() = restored.regs();
    worker_vu1_.BumpMicroEpoch();
    ++mirrors_;
}

MmioHandler& MtvuStack::Vif1Window(MmioHandler& inner) {
    vif1_gate_ = std::make_unique<Vif1Gate>(*this, inner);
    return *vif1_gate_;
}

uint64_t MtvuStack::Vif1Gate::Read(uint32_t addr, unsigned size) {
    if (addr == Vif::kVif1Base) {
        // VIF1_STAT: VPS=3 | VEW | FQC=16 while the worker has segments in flight.
        uint32_t stat = 0;
        if (stack_.sync_.TryVif1StatBusyView(&stat)) {
            // The poll's site (hazard census): which guest thread, from which function.
            Vif1PollCensus& pc = stack_.vif1_poll_census_;
            const int tid = EeRunToken::Instance().CurrentTid();
            if (tid >= 0 && tid < 8) ++pc.by_tid[tid];
            if (pc.first_tid < 0) {
                pc.first_func = stack_.vif1_poll_site_fn_ ? stack_.vif1_poll_site_fn_(stack_.vif1_poll_site_user_) : 0;
                pc.first_tid = tid;
            }
            if (stack_.vif1_idle_view_) {
                // E427: the EE-side register file (idle: the walk completed its staging).
                ++pc.idle_views;
                return inner_.Read(addr, size);
            }
            // Under the early FINISH the title's sync-path poll reaches here while the
            // worker still replays the frame: park on its progress instead of spinning.
            stack_.sync_.ParkVif1StatPoll(stack_.early_finish_);
            return stat;
        }
    } else {
        stack_.sync_.Sync(SyncSite::Vif1RegRead);
    }
    return inner_.Read(addr, size);
}

void MtvuStack::Vif1Gate::Write(uint32_t addr, unsigned size, uint64_t value) {
    stack_.sync_.Sync(SyncSite::Vif1RegWrite);
    inner_.Write(addr, size, value);
}

MmioHandler& MtvuStack::GifWindow(MmioHandler& inner) {
    gif_gate_ = std::make_unique<GifGate>(*this, inner);
    return *gif_gate_;
}

MmioHandler& MtvuStack::GifWindow(GifRegs& inner) {
    gif_inner_ = &inner;
    return GifWindow(static_cast<MmioHandler&>(inner));
}

void MtvuStack::FenceDrain() {
    if (!drain_.Running()) return;
    gif_fences_.fetch_add(1, std::memory_order_relaxed);
    arbiter_.DrainFence();
}

// GIF_STAT from the EE's side: M3R / IMT (MODE) and PSE (CTRL) are the EE's own
// writes (through this gate), M3P the VIF's atomic mask, FQC 0 (nothing fills
// the FIFO), OPH + APATH when the EE submitted a packet and has not closed it
// (the PATH3 chain scan, the PATH2 DIRECT scan of the staged VIF1 stream) --
// the state the fenced read reports once the parser has consumed everything.
uint32_t MtvuStack::ComposeGifStat() const {
    const GifRegs& regs = *gif_inner_;
    uint32_t status = 0;
    if (regs.Mode() & 1u) status |= 1u << 0;
    if (regs.Path3MaskedByVif()) status |= 1u << 1;
    if (regs.Mode() & 4u) status |= 1u << 2;
    if (regs.Ctrl() & 8u) status |= 1u << 3;
    if (arbiter_.Path3Scan().MidPacket()) status |= (1u << 9) | (3u << 10);
    else if (vif1_scan_.Path2().MidPacket()) status |= (1u << 9) | (2u << 10);
    return status;
}

uint64_t MtvuStack::GifGate::Read(uint32_t addr, unsigned size) {
    GifWindowCensus& c = stack_.gif_window_census_;
    const uint32_t slot = ((addr - GifRegs::kBase) >> 4) & 15u;
    ++c.reads[slot];
    const bool composable = slot == (GifRegs::kStat >> 4) && stack_.gif_inner_ != nullptr && stack_.drain_.Running();
    if (composable && stack_.composed_gif_stat_) {
        const uint32_t v = stack_.ComposeGifStat();
        ++c.stat_composed;
        if (v & (1u << 9)) ++c.stat_composed_busy;
        return v;
    }
    stack_.FenceDrain();
    const uint64_t v = inner_.Read(addr, size);
    if (slot == (GifRegs::kStat >> 4)) {
        if (v & (1u << 9)) ++c.stat_oph;
        if (v & (1u << 1)) ++c.stat_m3p;
        if ((v >> 24) & 0x1Fu) ++c.stat_fqc;
        // The proof lane (knob off): would the composed view have answered the same word?
        if (composable && stack_.ComposeGifStat() != static_cast<uint32_t>(v)) ++c.stat_composed_miss;
    }
    return v;
}

void MtvuStack::GifGate::Write(uint32_t addr, unsigned size, uint64_t value) {
    stack_.FenceDrain();
    ++stack_.gif_window_census_.writes[((addr - GifRegs::kBase) >> 4) & 15u];
    inner_.Write(addr, size, value);
}

// gif_reads=<slot>:<reads>/<writes>,... for the slots touched, then stat_oph/m3p/fqc.
std::string MtvuStack::GifReadsCensus() const {
    static const char* const kSlot[16] = {"ctrl", "mode", "stat", "r3", "tag0", "tag1", "tag2", "tag3",
                                          "cnt", "p3cnt", "p3tag", "rb", "rc", "rd", "re", "rf"};
    const GifWindowCensus& c = gif_window_census_;
    std::string s;
    for (int i = 0; i < 16; ++i) {
        if (c.reads[i] == 0 && c.writes[i] == 0) continue;
        if (!s.empty()) s += ":";
        s += std::string(kSlot[i]) + "=" + std::to_string(c.reads[i]) + "/" + std::to_string(c.writes[i]);
    }
    if (s.empty()) s = "none";
    s += ":stat_oph=" + std::to_string(c.stat_oph) + ":stat_m3p=" + std::to_string(c.stat_m3p) +
         ":stat_fqc=" + std::to_string(c.stat_fqc) + ":composed=" + std::to_string(c.stat_composed) + "/" +
         std::to_string(c.stat_composed_busy) + ":composed_miss=" + std::to_string(c.stat_composed_miss);
    return s;
}

// ",vif1_stat_poll_site=tid<n>@0x<func>:tid0=..:tid1=.." when any busy-view poll happened.
std::string MtvuStack::Vif1PollSiteCensus() const {
    const Vif1PollCensus& c = vif1_poll_census_;
    if (c.first_tid < 0) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "@0x%x", c.first_func);
    std::string s = ",vif1_stat_poll_site=tid" + std::to_string(c.first_tid) + buf + ":idle_views=" + std::to_string(c.idle_views);
    for (int t = 0; t < 8; ++t)
        if (c.by_tid[t]) s += ":tid" + std::to_string(t) + "=" + std::to_string(c.by_tid[t]);
    return s;
}

std::string MtvuStack::Census() const {
    std::string s = "on,worker=" + std::string(worker_.IsRunning() ? "1" : "0") +
                    ",drain=" + std::to_string(drain_.PassGeneration()) +
                    ",drain_thread=" + (drain_.Running() ? "on" : "inline") +
                    ",drain_ns=" + std::to_string(drain_.BusyNs()) + "/" + std::to_string(drain_.Passes()) +
                    ",gif_fences=" + std::to_string(GifFences()) + ",gif_reads=" + GifReadsCensus() +
                    ",redirects=" + std::to_string(sync_.SyncCount(SyncSite::Vu1EeKick)) + ",syncs=";
    for (size_t i = 0; i < static_cast<size_t>(SyncSite::Count); ++i)
        s += (i ? ":" : "") + std::to_string(sync_.SyncCount(static_cast<SyncSite>(i)));
    s += ",skipped=";
    for (size_t i = 0; i < static_cast<size_t>(SyncSite::Count); ++i)
        s += (i ? ":" : "") + std::to_string(sync_.SkippedCount(static_cast<SyncSite>(i)));
    s += ",boundaries=" + std::to_string(deferred_boundaries_) + "/" + std::to_string(terminal_syncs_) +
         ",declared=" + std::to_string(Declared()) + ",presented=" + std::to_string(Presented()) +
         ",batches=" + std::to_string(worker_.BatchSegmentsProcessed()) +
         ",stage_cap=" + std::to_string(worker_.StagedCapacity()) + ",pending_bytes=" + std::to_string(worker_.PendingBytes()) +
         ",bp=" + std::to_string(backpressure_.Engagements()) + "/" + std::to_string(backpressure_.FrameValveHits()) +
         "/" + std::to_string(backpressure_.StallValveHits()) + ",quiesce_laps=" + std::to_string(quiesce_laps_) +
         ",mirrors=" + std::to_string(mirrors_) +
         ",kick_drop=" + (backpressure_.DropMode() ? "on" : "off") +
         ",defer=" + (defer_boundaries_ ? "on" : "off") +
         ",early_finish=" + (early_finish_ ? "on" : "off") + ",vif1_idle_view=" + (vif1_idle_view_ ? "on" : "off") + ",pre_raised=" + std::to_string(arbiter_.PreRaisedFinishTotal()) +
         "/" + std::to_string(arbiter_.PreRaisedPath3Total()) + "/" + std::to_string(arbiter_.PreRaisedFinish()) +
         ",consumed=" + std::to_string(arbiter_.ConsumedFinishTotal()) +
         ",idle_waits=" + std::to_string(sync_.ProgressWaits()) + "/" + std::to_string(sync_.ProgressIdle()) +
         ",vif1_stat_polls=" + std::to_string(sync_.Vif1StatPolls()) + "/" + std::to_string(sync_.Vif1StatParks()) +
         Vif1PollSiteCensus() +
         ",drop=" + std::to_string(backpressure_.DropFrames()) + "/" + std::to_string(backpressure_.DropSegments()) + "/" +
         std::to_string(backpressure_.DropEpisodes()) + "/" + std::to_string(backpressure_.DropValveFrames()) +
         ",kicks_dropped=" + std::to_string(worker_.DroppedKicks()) + ",lag_max=" + std::to_string(backpressure_.LagMaxFrames()) +
         ",frame_marks=" + std::to_string(worker_.FrameMarksPushed()) + "/" + std::to_string(worker_.FrameMarksPassed());
    // The worker thread's time ledger: busy / parked / spinning ns, segments and
    // spans it replayed, queue pops and the deepest pop.
    const MtvuWorker::Ledger l = worker_.ledger();
    s += ",worker_ns=" + std::to_string(l.busy_ns) + "/" + std::to_string(l.wait_ns) + "/" + std::to_string(l.spin_ns) +
         ",worker_segs=" + std::to_string(l.segments) + "/" + std::to_string(l.records) + ",worker_pops=" + std::to_string(l.pops) +
         "/" + std::to_string(l.depth_max) + ",seal_wakes_skipped=" + std::to_string(l.seal_wakes_skipped);
    return s;
}
