#pragma once
// The MTVU worker: VU1 (and the VIF1 stream that drives it) run on a second
// host thread. ONE producer (the EE / DMAC thread) submits VIF1 spans; the
// worker replays them through the VIF1 command processor into its own VU1
// unit in submission order, exactly as the inline path would have.
//
// PS2 rule the worker keeps: VIF1 consumes its stream serially and the
// programs it kicks (and the XGKICK packets they produce) belong to that
// stream position. So every span opens an order MARKER in the GIF arbiter at
// submit time, the worker's PATH1/PATH2 submits fill that marker, and the
// marker is sealed when the span completes. Nothing else may touch VU1 memory
// or the VIF1 unit while a segment is in flight (MtvuSync first).
//
// Segments: Data (one span, optional TTE bracket), ResetLatch (the walk-head
// latch reset), Batch (the DMAC's staged spans of one walk as records
// `[u32 len][u8 tte][pad3][bytes]`, one marker for the whole batch; flushed at
// kBatchFlushBytes / kBatchFlushRecords or at the walk exit), FrameMark (the
// producer's frame sync: the worker counts it passed -- the lag in frames the
// kick drop reads). A Data / Batch segment tagged `drop_kicks` (the clock
// contract's kick drop, MtvuBackpressure drop mode) is replayed with the
// worker unit's kick drop set: its VIF stream runs, its programs do not.
//
// Cross-thread contract:
//   queue_, processing_, stop_          under mtx_; cv_work_ wakes the worker, cv_idle_ wakes Sync / park waiters
//   inflight_        ++ relaxed before a segment is published, -- release after it is fully processed and sealed;
//                    the producer's acquire load of 0 makes every worker write (VU1 memory, VIF1 regs, seals) visible
//   pending_bytes_/pending_segs_   relaxed ledger of segment bytes not yet processed
//   submit_seq_      release per submit (after the push); the worker's spin tier polls it acquire
//   seal_count_      seq_cst bump after every seal -- a wake hint for the eager loops, not a publication
//   sync_waiters_    producers parked in Sync / backpressure; the worker takes the locked wake path only if > 0
//   active_marker_   worker thread only, between segment start and seal (read by the arbiter on that thread)
//   stage_*          producer thread only
//   drain_busy_      the one-drain-at-a-time exchange; in_eager_drain is thread-local
//   running_         Start publishes after the thread exists; Stop clears after the join
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "engine/core/threads/park.h"
#include "engine/platform/host/platform.h"
#include "engine/gif/gif_marker_owner.h"
#include "engine/vu/tiers/mtvu_pipeline.h"
#include "engine/vu/tiers/mtvu_settings.h"

class GifArbiter;
class MtvuBackpressure;
class MtvuVifFeed;
class VuUnit;

class MtvuWorker final : public GifMarkerOwner, public MtvuPipeline {
public:
    static constexpr size_t kBatchFlushBytes = 32 * 1024;
    static constexpr size_t kBatchFlushRecords = 32;
    static constexpr size_t kBatchRecordHeader = 8;  // [u32 len][u8 tte][pad3]
    static constexpr size_t kStackBytes = 32u << 20;

    MtvuWorker(MtvuVifFeed& vif, VuUnit& worker_vu1, GifArbiter& arbiter, MtvuBackpressure& backpressure,
               MtvuSettings settings);
    // Stops and joins if still running.
    ~MtvuWorker() override;
    MtvuWorker(const MtvuWorker&) = delete;
    MtvuWorker& operator=(const MtvuWorker&) = delete;

    // Spawn the worker (32 MiB stack), power on its VU1 register file, bind
    // it as VIF1's unit, register as the arbiter's marker owner and the
    // backpressure's pipeline. `on_start` runs on the new thread first (the
    // host names / pins it). Never from the worker thread.
    void Start(void (*on_start)(void* user) = nullptr, void* user = nullptr);
    // Drain everything queued, join, unbind. Never from the worker thread.
    void Stop();
    bool IsRunning() const;

    // ---- producer edge (one thread) ----
    void SubmitVif1(const uint8_t* data, size_t size, bool tte = false);
    void SubmitVif1Staged(const uint8_t* data, size_t size, bool tte = false);
    void FlushVif1Staged();
    void SubmitResetLatch();
    // The frame sync's mark (flushes the staged batch first). Marks pushed
    // minus marks the worker passed = the worker's lag in frames.
    void SubmitFrameMark();
    uint64_t FrameMarksPushed() const { return frame_marks_pushed_; }
    uint64_t FrameMarksPassed() const { return frame_marks_passed_.load(std::memory_order_relaxed); }
    uint64_t FrameLag() const;
    // Kicks the worker's unit dropped (VuUnit::DroppedKicks; relaxed census).
    uint64_t DroppedKicks() const;
    // The DMAC staged-feed seam (ChainWalker::SetVif1StagedFeed) in thunk form.
    static void StagedFeedThunk(void* user, const uint8_t* data, size_t size, bool tte);
    static void StagedFlushThunk(void* user);
    static void StagedResetThunk(void* user);

    // Segments in flight (acquire); backs the VIF1_STAT busy view.
    bool IsBusy() const;
    // Batch segments the worker has processed (relaxed census).
    uint64_t BatchSegmentsProcessed() const;
    // The worker thread's time ledger (relaxed census, written by the worker):
    // wall ns processing segments (the VIF stream, the programs, their kicks),
    // ns parked on the work condition, ns in the spin tier; segments and batch
    // records (spans) processed; queue pops and the deepest pop (segments
    // taken under one lock -- the feed's queue depth as the worker saw it).
    struct Ledger {
        uint64_t busy_ns = 0;
        uint64_t wait_ns = 0;
        uint64_t spin_ns = 0;
        uint64_t segments = 0;
        uint64_t records = 0;
        uint64_t pops = 0;
        uint64_t depth_max = 0;
    };
    Ledger ledger() const;
    uint64_t StagedRecords() const { return stage_records_; }
    size_t StagedBytes() const { return stage_bytes_.size(); }
    // The staging buffer's high-water capacity (the ONE buffer the producer
    // keeps; a flushed segment is right-sized, never this).
    size_t StagedCapacity() const { return stage_capacity_; }
    VuUnit& Vu1Unit() { return worker_vu1_; }
    const MtvuSettings& settings() const { return settings_; }
    MtvuBackpressure& backpressure() { return backpressure_; }

    // ---- GifMarkerOwner ----
    bool Running() const override;
    bool OnWorkerThread() const override;
    std::shared_ptr<GifMtvuMarker> ActiveMarker() const override;

    // ---- MtvuPipeline (producer thread) ----
    bool MayPark() const override;
    uint64_t PipelineBytes() const override;
    bool WorkerIdle() const override;
    uint64_t SealCount() const override;
    void EagerDrain() override;
    void WaitIdleOrSeal(uint64_t timeout_ns, uint64_t seen_seal) override;
    void WaitFrameProgress(uint64_t timeout_ns) override;
    void EnterSyncWait() override;
    void LeaveSyncWait() override;

    // ---- the wait surface MtvuSync uses ----
    // Park until no segment is queued, processing or in flight.
    void WaitIdle();
    // One guarded arbiter drain on this thread: false when another drain is
    // in progress (the loser skips -- drains are opportunistic).
    bool TryDrainGuarded();
    // This thread is inside a guarded drain (recursion guard).
    bool InEagerDrain() const;
    // Segment bytes queued for the worker.
    uint64_t PendingBytes() const;
    // The present thread wakes a frame-progress park (unwired = the timeout).
    void NotifyFrameProgress();

private:
    struct Segment {
        enum class Op : uint8_t { Data, ResetLatch, Batch, FrameMark };
        Op op = Op::Data;
        bool tte = false;
        bool drop_kicks = false;
        std::vector<uint8_t> bytes;
        std::shared_ptr<GifMtvuMarker> marker;
    };

    static void ThreadMain(void* self);
    void WorkerLoop();
    void ProcessSegment(Segment& seg);
    void ReplaySpan(const uint8_t* data, size_t size, bool tte);
    void SealAndRetire(Segment& seg);
    void Push(Segment&& seg);

    MtvuVifFeed& vif_;
    VuUnit& worker_vu1_;
    GifArbiter& arbiter_;
    MtvuBackpressure& backpressure_;
    MtvuSettings settings_;

    mutable std::mutex mtx_;
    std::condition_variable cv_work_;
    std::condition_variable cv_idle_;
    std::deque<Segment> queue_;
    bool processing_ = false;
    bool stop_ = false;

    std::atomic<bool> running_{false};
    ps2r::host::NativeThread thread_{};
    bool thread_started_ = false;
    void (*on_start_)(void*) = nullptr;
    void* on_start_user_ = nullptr;

    std::atomic<uint32_t> inflight_{0};
    std::atomic<uint64_t> pending_bytes_{0};
    std::atomic<uint64_t> pending_segs_{0};
    std::atomic<uint64_t> submit_seq_{0};
    std::atomic<uint64_t> last_work_ns_{0};
    std::atomic<uint64_t> seal_count_{0};
    std::atomic<int> sync_waiters_{0};
    std::atomic<uint64_t> batch_segments_{0};
    std::atomic<uint64_t> busy_ns_{0};
    std::atomic<uint64_t> wait_ns_{0};
    std::atomic<uint64_t> spin_ns_{0};
    std::atomic<uint64_t> segments_{0};
    std::atomic<uint64_t> records_{0};
    std::atomic<uint64_t> pops_{0};
    std::atomic<uint64_t> depth_max_{0};
    uint64_t frame_marks_pushed_ = 0;              // producer thread
    std::atomic<uint64_t> frame_marks_passed_{0};  // worker thread writes, producer reads
    std::atomic<bool> drain_busy_{false};
    std::shared_ptr<GifMtvuMarker> active_marker_;
    ps2r::Park frame_park_;

    std::vector<uint8_t> stage_bytes_;
    size_t stage_records_ = 0;
    bool stage_drop_kicks_ = false;
    size_t stage_capacity_ = 0;
};
