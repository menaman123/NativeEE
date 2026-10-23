#include "engine/vu/tiers/mtvu_backpressure.h"

#include <thread>

#include "engine/ee/timing/tick_profile.h"
#include "engine/vu/tiers/mtvu_clock.h"
#include "engine/vu/tiers/mtvu_pipeline.h"
#include "engine/vu/tiers/runahead_guard.h"

MtvuBackpressure::MtvuBackpressure(RunaheadGuard& guard, const MtvuClock& clock)
    : guard_(guard), clock_(clock) {}

void MtvuBackpressure::Bind(MtvuPipeline* pipeline) { pipeline_ = pipeline; }

void MtvuBackpressure::SetFrameCounters(const std::atomic<uint64_t>* declared,
                                        const std::atomic<uint64_t>* presented) {
    declared_ = declared;
    presented_ = presented;
}

void MtvuBackpressure::SetShutdownFlag(const std::atomic<bool>* running) { running_ = running; }

uint64_t MtvuBackpressure::Declared() const {
    return declared_ ? declared_->load(std::memory_order_relaxed) : 0u;
}

uint64_t MtvuBackpressure::Presented() const {
    return presented_ ? presented_->load(std::memory_order_relaxed) : 0u;
}

uint64_t MtvuBackpressure::DepthFrames() const {
    return RunaheadGuard::DepthFrames(Declared(), Presented());
}

bool MtvuBackpressure::EngageDrop() {
    // Never a park. The frame's verdict stands; the bytes bound is the valve
    // that cuts the rest of a frame whose pipeline bytes still pass it.
    if (!dropping_frame_ && pipeline_->PipelineBytes() > kPipelineHighBytes) {
        dropping_frame_ = true;
        ++drop_valve_frames_;
        ++drop_frames_;
        ++drop_episodes_;
    }
    if (dropping_frame_) ++drop_segments_;
    return dropping_frame_;
}

bool MtvuBackpressure::NoteFrameSync(uint64_t lag_frames) {
    if (lag_frames > lag_max_frames_) lag_max_frames_ = lag_frames;
    if (!drop_mode_) return false;
    const bool was = dropping_frame_;
    dropping_frame_ = lag_frames > kDropLagFrames;
    if (dropping_frame_) {
        ++drop_frames_;
        if (!was) ++drop_episodes_;
    }
    return dropping_frame_;
}

bool MtvuBackpressure::Engage() {
    if (pipeline_ == nullptr) return false;
    if (!pipeline_->MayPark() || !pipeline_->Running()) return false;
    if (drop_mode_) return EngageDrop();
    uint64_t total = pipeline_->PipelineBytes();
    const bool bytes_over = total > kPipelineHighBytes;
    const bool frames_over = guard_.Over(Declared(), Presented());
    if (!bytes_over && !frames_over) return false;

    // The park is the game thread waiting on the worker: booked to the tick
    // census as `vu1wait` (the boss stall of 2026-10-13 read as 560 ms of
    // `other` every ~25 ticks until this scope named it).
    ps2r::ee::TickProfile::Scope scope(ps2r::ee::TickProfile::Vu1Wait);
    const uint64_t t0 = clock_.NowNs();
    int stalled = 0;
    bool bytes_need = bytes_over;
    bool frame_valve_hit = false;
    ++engagements_;
    // A park under the HOLD cap (the EE one frame ahead, the worker not done with the
    // previous frame): counted; past kHoldLiftNs the guard lifts the cap for the
    // catch-up window and the park ends at the lifted bound (NoteHoldPark).
    const bool hold_park = frames_over && guard_.HoldFrames() != 0 && !guard_.HoldLifted(Declared()) &&
                           guard_.EffMaxFrames(Declared()) < guard_.EffMaxFrames();
    if (hold_park) ++hold_parks_;
    pipeline_->EnterSyncWait();
    for (;;) {
        if (running_ != nullptr && !running_->load(std::memory_order_acquire)) break;
        const uint64_t before = total;
        const uint64_t seen = pipeline_->SealCount();
        pipeline_->EagerDrain();
        // E431: a worker parked on a VIFcode i-bit stall is quiescent -- no seal,
        // no shrink, no present can come until the EE serves the VIF1 interrupt
        // (FBRST.STC), and the EE is THIS thread, inside a chain walk: its next
        // dispatch point is after the walk. Waiting here waits for ourselves
        // (every span of the walk then rode the 100 ms frame valve: 25 s of
        // Engage per watchdog on the title->attract load with the kernel-wait
        // probe on). The park ends; the bound is exceeded by the rest of this
        // walk, no more.
        if (pipeline_->IrqStalled()) {
            ++irq_stall_breaks_;
            break;
        }
        total = pipeline_->PipelineBytes();
        if (bytes_need) {
            if (total <= kLowWaterBytes) {
                bytes_need = false;
            } else if (pipeline_->WorkerIdle() && total >= before) {
                // Idle worker and no shrink: nothing left that could free bytes.
                if (++stalled >= kStallValveCount) {
                    bytes_need = false;
                    ++stall_valve_hits_;
                }
            } else {
                stalled = 0;
            }
        }
        if (hold_park) guard_.NoteHoldPark(Declared(), t0);
        const RunaheadFramePark fp = guard_.FramePark(Declared(), Presented(), t0);
        if (fp.valve_hit) frame_valve_hit = true;
        if (!bytes_need && !fp.park) break;
        if (bytes_need) {
            pipeline_->WaitIdleOrSeal(kPollNs, seen);
        } else {
            pipeline_->WaitFrameProgress(kPollNs);
        }
    }
    pipeline_->LeaveSyncWait();
    park_ns_ += clock_.NowNs() - t0;
    if (frame_valve_hit) ++frame_valve_hits_;
    return false;
}

uint64_t MtvuBackpressure::HoldLifts() const { return guard_.HoldLifts(); }

uint64_t MtvuBackpressure::ClampSpinBudgetNs(uint64_t ns) {
    if (ns < kSpinBudgetMinNs) return kSpinBudgetMinNs;
    if (ns > kSpinBudgetMaxNs) return kSpinBudgetMaxNs;
    return ns;
}

bool MtvuBackpressure::SpinArmed(uint64_t prev_work_ns, uint64_t now_ns) {
    return prev_work_ns != 0 && now_ns - prev_work_ns <= kRecentNs;
}

void MtvuBackpressure::CpuRelax() { std::this_thread::yield(); }
