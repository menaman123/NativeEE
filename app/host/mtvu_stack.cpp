#include "app/host/mtvu_stack.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "engine/gif/gif_arbiter.h"
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
      inline_dispatch_(inline_vu1_dispatch) {
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
}

MtvuStack::~MtvuStack() {
    Stop();
    MtvuSync::UninstallFrom(inline_dispatch_);
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

bool MtvuStack::Quiesce(std::string* why) {
    sync_.Sync(SyncSite::Other);
    for (int lap = 0;; ++lap) {
        const bool pipeline_idle = !worker_.IsBusy() && worker_.PendingBytes() == 0;
        if (pipeline_idle && !arbiter_.hasPending()) {
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
        std::this_thread::sleep_for(std::chrono::nanoseconds(kQuiesceLapNs));
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
        if (stack_.sync_.TryVif1StatBusyView(&stat)) return stat;
    } else {
        stack_.sync_.Sync(SyncSite::Vif1RegRead);
    }
    return inner_.Read(addr, size);
}

void MtvuStack::Vif1Gate::Write(uint32_t addr, unsigned size, uint64_t value) {
    stack_.sync_.Sync(SyncSite::Vif1RegWrite);
    inner_.Write(addr, size, value);
}

std::string MtvuStack::Census() const {
    std::string s = "on,worker=" + std::string(worker_.IsRunning() ? "1" : "0") +
                    ",drain=" + std::to_string(drain_.PassGeneration()) +
                    ",drain_thread=" + (drain_.Running() ? "on" : "inline") +
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
         ",idle_waits=" + std::to_string(sync_.ProgressWaits()) + "/" + std::to_string(sync_.ProgressIdle()) +
         ",drop=" + std::to_string(backpressure_.DropFrames()) + "/" + std::to_string(backpressure_.DropSegments()) + "/" +
         std::to_string(backpressure_.DropEpisodes()) + "/" + std::to_string(backpressure_.DropValveFrames()) +
         ",kicks_dropped=" + std::to_string(worker_.DroppedKicks()) + ",lag_max=" + std::to_string(backpressure_.LagMaxFrames()) +
         ",frame_marks=" + std::to_string(worker_.FrameMarksPushed()) + "/" + std::to_string(worker_.FrameMarksPassed());
    // The worker thread's time ledger: busy / parked / spinning ns, segments and
    // spans it replayed, queue pops and the deepest pop.
    const MtvuWorker::Ledger l = worker_.ledger();
    s += ",worker_ns=" + std::to_string(l.busy_ns) + "/" + std::to_string(l.wait_ns) + "/" + std::to_string(l.spin_ns) +
         ",worker_segs=" + std::to_string(l.segments) + "/" + std::to_string(l.records) + ",worker_pops=" + std::to_string(l.pops) +
         "/" + std::to_string(l.depth_max);
    return s;
}
