#include "engine/vu/tiers/mtvu_worker.h"

#include <chrono>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_vif_feed.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_unit.h"

namespace {

// Set once at the top of the worker thread: the routing question "is the
// caller the worker" is answered without a lock.
thread_local const MtvuWorker* t_worker = nullptr;
// This thread is inside a guarded drain (Sync's eager loop, the try-drains,
// the backpressure park): a nested Sync must not drain again.
thread_local bool t_in_eager_drain = false;

}  // namespace

MtvuWorker::MtvuWorker(MtvuVifFeed& vif, VuUnit& worker_vu1, GifArbiter& arbiter,
                       MtvuBackpressure& backpressure, MtvuSettings settings)
    : vif_(vif), worker_vu1_(worker_vu1), arbiter_(arbiter), backpressure_(backpressure),
      settings_(settings) {}

MtvuWorker::~MtvuWorker() { Stop(); }

// ---- lifecycle ---------------------------------------------------------------

void MtvuWorker::ThreadMain(void* self) {
    auto* w = static_cast<MtvuWorker*>(self);
    if (w->on_start_) w->on_start_(w->on_start_user_);
    w->WorkerLoop();
}

void MtvuWorker::Start(void (*on_start)(void* user), void* user) {
    if (running_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = false;
    }
    on_start_ = on_start;
    on_start_user_ = user;
    // VU1 powers on with VF0 = {0,0,0,1} and Q = 1.0.
    worker_vu1_.regs().Reset();
    vif_.BindVu1(&worker_vu1_);
    arbiter_.SetMarkerOwner(this);
    backpressure_.Bind(this);

    // The host thread on the worker's stack; the default stack if the host refuses it.
    bool ok = ps2r::host::ThreadCreate(&thread_, &MtvuWorker::ThreadMain, this, kStackBytes);
    if (!ok) ok = ps2r::host::ThreadCreate(&thread_, &MtvuWorker::ThreadMain, this, 0);
    if (!ok) {
        // No thread: undo the binding so the inline path stays in force.
        backpressure_.Bind(nullptr);
        arbiter_.SetMarkerOwner(nullptr);
        vif_.BindVu1(nullptr);
        running_.store(false);
        return;
    }
    thread_started_ = true;
}

void MtvuWorker::Stop() {
    if (!running_.load(std::memory_order_acquire)) return;
    if (OnWorkerThread()) return;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    cv_work_.notify_all();
    if (thread_started_) {
        ps2r::host::ThreadJoin(thread_);
        thread_started_ = false;
    }
    running_.store(false, std::memory_order_release);
    backpressure_.Bind(nullptr);
    arbiter_.SetMarkerOwner(nullptr);
    vif_.BindVu1(nullptr);
}

bool MtvuWorker::IsRunning() const { return running_.load(std::memory_order_acquire); }
bool MtvuWorker::Running() const { return IsRunning(); }
bool MtvuWorker::OnWorkerThread() const { return t_worker == this; }

std::shared_ptr<GifMtvuMarker> MtvuWorker::ActiveMarker() const {
    return OnWorkerThread() ? active_marker_ : nullptr;
}

// ---- producer edge -------------------------------------------------------------

void MtvuWorker::Push(Segment&& seg) {
    // The count goes up BEFORE the segment becomes visible.
    inflight_.fetch_add(1, std::memory_order_relaxed);
    bool wake;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // Batched wake: only an empty -> non-empty transition with the worker parked.
        wake = queue_.empty() && !processing_;
        queue_.push_back(std::move(seg));
        submit_seq_.fetch_add(1, std::memory_order_release);
    }
    if (wake) cv_work_.notify_one();
}

void MtvuWorker::SetStageObserver(StageObserver span, StageResetObserver reset, void* user) {
    stage_observer_ = span;
    stage_reset_observer_ = reset;
    stage_observer_user_ = user;
}

void MtvuWorker::SubmitVif1(const uint8_t* data, size_t size, bool tte) {
    if (!IsRunning() || data == nullptr || size == 0) return;
    if (stage_observer_) stage_observer_(stage_observer_user_, data, size, tte);
    const bool drop = backpressure_.Engage();
    Segment seg;
    seg.op = Segment::Op::Data;
    seg.tte = tte;
    seg.drop_kicks = drop;
    seg.bytes.assign(data, data + size);
    pending_bytes_.fetch_add(size, std::memory_order_relaxed);
    pending_segs_.fetch_add(1, std::memory_order_relaxed);
    // The marker takes the stream position the inline path would have produced the packets at.
    seg.marker = arbiter_.openMarker();
    Push(std::move(seg));
}

void MtvuWorker::SubmitResetLatch() {
    if (!IsRunning()) return;
    if (stage_reset_observer_) stage_reset_observer_(stage_observer_user_);
    Segment seg;
    seg.op = Segment::Op::ResetLatch;
    Push(std::move(seg));
}

void MtvuWorker::SubmitFrameMark() {
    if (!IsRunning()) return;
    FlushVif1Staged();
    Segment seg;
    seg.op = Segment::Op::FrameMark;
    ++frame_marks_pushed_;
    Push(std::move(seg));
}

uint64_t MtvuWorker::FrameLag() const {
    const uint64_t passed = FrameMarksPassed();
    return frame_marks_pushed_ > passed ? frame_marks_pushed_ - passed : 0;
}

uint64_t MtvuWorker::DroppedKicks() const { return worker_vu1_.DroppedKicks(); }

void MtvuWorker::SubmitVif1Staged(const uint8_t* data, size_t size, bool tte) {
    if (!IsRunning() || data == nullptr || size == 0) return;
    if (stage_observer_) stage_observer_(stage_observer_user_, data, size, tte);
    // The verdict of the batch is the verdict at its FIRST record (a batch is
    // one walk's spans, flushed before the frame mark; a verdict flip inside a
    // frame -- the valve -- takes effect at the next record's batch).
    const bool drop = backpressure_.Engage();
    if (stage_records_ == 0) stage_drop_kicks_ = drop;
    else if (drop != stage_drop_kicks_) {
        FlushVif1Staged();
        stage_drop_kicks_ = drop;
    }
    uint8_t hdr[kBatchRecordHeader] = {0};
    const uint32_t len = static_cast<uint32_t>(size);
    std::memcpy(hdr, &len, sizeof(len));
    hdr[4] = tte ? 1 : 0;
    stage_bytes_.insert(stage_bytes_.end(), hdr, hdr + kBatchRecordHeader);
    stage_bytes_.insert(stage_bytes_.end(), data, data + size);
    stage_records_++;
    if (stage_bytes_.size() >= kBatchFlushBytes || stage_records_ >= kBatchFlushRecords) FlushVif1Staged();
}

void MtvuWorker::FlushVif1Staged() {
    if (stage_records_ == 0) return;
    if (!IsRunning()) {
        // Worker torn down mid-stage: dropped, like a SubmitVif1 on a stopped worker.
        stage_bytes_.clear();
        stage_records_ = 0;
        return;
    }
    Segment seg;
    seg.op = Segment::Op::Batch;
    seg.drop_kicks = stage_drop_kicks_;
    // The segment carries exactly its bytes; the staging buffer keeps its
    // capacity for the next batch (clear(), no reallocation while staging).
    // Moving the staging vector handed every batch the all-time high-water
    // CAPACITY (176 KB) while the ledger counted payload: a slow-tier route
    // (every kick raw) held 13 k small live batches = 2.3 GB under a
    // backpressure that saw a few MB (LAGRANGE_TRACK2A2D.md A.2).
    seg.bytes.assign(stage_bytes_.begin(), stage_bytes_.end());
    stage_bytes_.clear();
    stage_records_ = 0;
    if (stage_bytes_.capacity() > stage_capacity_) stage_capacity_ = stage_bytes_.capacity();
    pending_bytes_.fetch_add(seg.bytes.size(), std::memory_order_relaxed);
    pending_segs_.fetch_add(1, std::memory_order_relaxed);
    // Flush-time marker = the position the first span's marker would have had
    // (mid-walk the producer is the only arbiter producer).
    seg.marker = arbiter_.openMarker();
    Push(std::move(seg));
}

void MtvuWorker::StagedFeedThunk(void* user, const uint8_t* data, size_t size, bool tte) {
    static_cast<MtvuWorker*>(user)->SubmitVif1Staged(data, size, tte);
}
void MtvuWorker::StagedFlushThunk(void* user) { static_cast<MtvuWorker*>(user)->FlushVif1Staged(); }
void MtvuWorker::StagedResetThunk(void* user) { static_cast<MtvuWorker*>(user)->SubmitResetLatch(); }

bool MtvuWorker::IsBusy() const {
    return IsRunning() && inflight_.load(std::memory_order_acquire) != 0;
}

uint64_t MtvuWorker::BatchSegmentsProcessed() const {
    return batch_segments_.load(std::memory_order_relaxed);
}

MtvuWorker::Ledger MtvuWorker::ledger() const {
    Ledger l;
    l.busy_ns = busy_ns_.load(std::memory_order_relaxed);
    l.wait_ns = wait_ns_.load(std::memory_order_relaxed);
    l.spin_ns = spin_ns_.load(std::memory_order_relaxed);
    l.segments = segments_.load(std::memory_order_relaxed);
    l.records = records_.load(std::memory_order_relaxed);
    l.pops = pops_.load(std::memory_order_relaxed);
    l.depth_max = depth_max_.load(std::memory_order_relaxed);
    l.seal_wakes_skipped = seal_wakes_skipped_.load(std::memory_order_relaxed);
    return l;
}

// ---- worker thread -----------------------------------------------------------

void MtvuWorker::ReplaySpan(const uint8_t* data, size_t size, bool tte) {
    if (tte) vif_.SetVif1TteFeed(true);
    vif_.ProcessVif1(data, size);
    if (tte) vif_.SetVif1TteFeed(false);
}

void MtvuWorker::SealAndRetire(Segment& seg) {
    pending_bytes_.fetch_sub(seg.bytes.size(), std::memory_order_relaxed);
    pending_segs_.fetch_sub(1, std::memory_order_relaxed);
    if (seg.marker) {
        arbiter_.sealMarker(seg.marker);   // kicks the drain thread itself when one runs
        seal_count_.fetch_add(1, std::memory_order_seq_cst);
        if (sync_waiters_.load(std::memory_order_seq_cst) > 0) {
            // A parked producer (Sync's eager loop, WaitProgress, the backpressure
            // park) wants progress. With the drain thread running the seal's
            // delivery does not depend on the EE's wake (sealMarker kicked the
            // thread) and every one of those waits is a kPollNs poll: the futex
            // wake is spent on the LAST in-flight segment only (its idle is what
            // the EE's predicate waits for), not on the ~137 others of a Paris
            // frame. Without a drain thread the EE's wake IS the delivery: every
            // seal wakes it, as before.
            const bool last_inflight = inflight_.load(std::memory_order_acquire) == 1;
            if (last_inflight || !arbiter_.DrainThreadRunning()) {
                { std::lock_guard<std::mutex> lk(mtx_); }
                cv_idle_.notify_all();
            } else {
                seal_wakes_skipped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    // Completion is published LAST.
    inflight_.fetch_sub(1, std::memory_order_release);
}

void MtvuWorker::ProcessSegment(Segment& seg) {
    // The VU rounding mode for the whole segment: every kick's own guard
    // (VuBlockTier::Run and the other tiers) then finds FE_TOWARDZERO set and
    // writes nothing -- the two FPCR writes per kick (2790 a Paris frame) were
    // the worker's per-kick fixed cost. Nothing else this thread runs between
    // kicks computes in floating point (VIF decode and the GIF hand-off are
    // integer; the GIF -> DrawIR parse runs on the EE or the drain thread).
    VuChopRoundGuard round_guard;
    switch (seg.op) {
        case Segment::Op::ResetLatch:
            vif_.ResetVif1Latch();
            inflight_.fetch_sub(1, std::memory_order_release);
            return;
        case Segment::Op::FrameMark:
            frame_marks_passed_.fetch_add(1, std::memory_order_relaxed);
            inflight_.fetch_sub(1, std::memory_order_release);
            return;
        case Segment::Op::Batch: {
            active_marker_ = seg.marker;
            worker_vu1_.SetKickDrop(seg.drop_kicks);
            const uint8_t* p = seg.bytes.data();
            const uint8_t* end = p + seg.bytes.size();
            uint64_t records = 0;
            while (p + kBatchRecordHeader <= end) {
                uint32_t len = 0;
                std::memcpy(&len, p, sizeof(len));
                const bool tte = p[4] != 0;
                p += kBatchRecordHeader;
                if (p + len > end) break;
                ReplaySpan(p, len, tte);
                p += len;
                ++records;
            }
            worker_vu1_.SetKickDrop(false);
            active_marker_.reset();
            batch_segments_.fetch_add(1, std::memory_order_relaxed);
            records_.fetch_add(records, std::memory_order_relaxed);
            break;
        }
        case Segment::Op::Data:
            active_marker_ = seg.marker;
            worker_vu1_.SetKickDrop(seg.drop_kicks);
            ReplaySpan(seg.bytes.data(), seg.bytes.size(), seg.tte);
            worker_vu1_.SetKickDrop(false);
            active_marker_.reset();
            records_.fetch_add(1, std::memory_order_relaxed);
            break;
    }
    SealAndRetire(seg);
}

void MtvuWorker::WorkerLoop() {
    t_worker = this;
    std::deque<Segment> local;
    bool spin_armed = false;
    uint64_t seen_seq = 0;
    for (;;) {
        if (spin_armed) {
            // Spin tier: a bounded poll of submit_seq_ only, armed by recency.
            spin_armed = false;
            const uint64_t t0 = ps2r::Park::NowNs();
            const uint64_t budget = MtvuBackpressure::ClampSpinBudgetNs(MtvuBackpressure::kSpinBudgetNs);
            uint32_t polls = 0;
            for (;;) {
                if (submit_seq_.load(std::memory_order_acquire) != seen_seq) break;
                if ((++polls & 63u) == 0) {
                    if (ps2r::Park::NowNs() - t0 >= budget) break;
                    MtvuBackpressure::CpuRelax();
                } else {
                    MtvuBackpressure::CpuRelax();
                }
            }
            spin_ns_.fetch_add(ps2r::Park::NowNs() - t0, std::memory_order_relaxed);
        }
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!stop_ && queue_.empty()) {
                const uint64_t t0 = ps2r::Park::NowNs();
                cv_work_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                wait_ns_.fetch_add(ps2r::Park::NowNs() - t0, std::memory_order_relaxed);
            }
            if (stop_ && queue_.empty()) break;
            // Batched pop: the whole queue under one lock.
            local.swap(queue_);
            processing_ = true;
        }
        const uint64_t busy_t0 = ps2r::Park::NowNs();
        const uint64_t depth = local.size();
        pops_.fetch_add(1, std::memory_order_relaxed);
        if (depth > depth_max_.load(std::memory_order_relaxed)) depth_max_.store(depth, std::memory_order_relaxed);
        while (!local.empty()) {
            ProcessSegment(local.front());
            local.pop_front();
        }
        segments_.fetch_add(depth, std::memory_order_relaxed);
        busy_ns_.fetch_add(ps2r::Park::NowNs() - busy_t0, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            processing_ = false;
            if (queue_.empty()) {
                cv_idle_.notify_all();
                seen_seq = submit_seq_.load(std::memory_order_relaxed);
                const uint64_t now = ps2r::Park::NowNs();
                const uint64_t prev = last_work_ns_.exchange(now);
                spin_armed = MtvuBackpressure::SpinArmed(prev, now);
            }
        }
    }
    t_worker = nullptr;
}

// ---- MtvuPipeline / the wait surface ---------------------------------------------

bool MtvuWorker::MayPark() const { return !OnWorkerThread() && !t_in_eager_drain; }

uint64_t MtvuWorker::PipelineBytes() const {
    return pending_bytes_.load(std::memory_order_relaxed) + arbiter_.PendingBytes();
}

uint64_t MtvuWorker::PendingBytes() const { return pending_bytes_.load(std::memory_order_relaxed); }

bool MtvuWorker::WorkerIdle() const { return inflight_.load(std::memory_order_acquire) == 0; }

uint64_t MtvuWorker::SealCount() const { return seal_count_.load(std::memory_order_relaxed); }

void MtvuWorker::EagerDrain() { TryDrainGuarded(); }

bool MtvuWorker::TryDrainGuarded() {
    if (drain_busy_.exchange(true, std::memory_order_acquire)) return false;
    t_in_eager_drain = true;
    arbiter_.drain();
    t_in_eager_drain = false;
    drain_busy_.store(false, std::memory_order_release);
    return true;
}

bool MtvuWorker::InEagerDrain() const { return t_in_eager_drain; }

void MtvuWorker::WaitIdleOrSeal(uint64_t timeout_ns, uint64_t seen_seal) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_idle_.wait_for(lk, std::chrono::nanoseconds(timeout_ns), [&] {
        return (queue_.empty() && !processing_ && inflight_.load(std::memory_order_acquire) == 0) ||
               seal_count_.load(std::memory_order_seq_cst) != seen_seal;
    });
}

void MtvuWorker::WaitIdle() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_idle_.wait(lk, [&] {
        return queue_.empty() && !processing_ && inflight_.load(std::memory_order_acquire) == 0;
    });
}

void MtvuWorker::WaitFrameProgress(uint64_t timeout_ns) {
    frame_park_.WaitFor(timeout_ns, frame_park_.Generation());
}

void MtvuWorker::NotifyFrameProgress() { frame_park_.Notify(); }

void MtvuWorker::EnterSyncWait() { sync_waiters_.fetch_add(1, std::memory_order_seq_cst); }
void MtvuWorker::LeaveSyncWait() { sync_waiters_.fetch_sub(1, std::memory_order_seq_cst); }
