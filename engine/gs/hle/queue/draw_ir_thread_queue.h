// gs/hle/queue/draw_ir_thread_queue.h -- DrawIrThreadQueue: the DrawIrSink that
// carries the GS state machine's stream to a consumer sink running on its OWN
// host thread (the plan's RenderQueue + consumer loop: the EE pushes, the
// graphics thread pops), in stream order, one record at a time.
//
// Records are COPIED at enqueue: a DrawIr (its vertex array included), a JobRec,
// or the frame's VSync JobRec. Two inputs the in-thread consumer reads from the
// producer's state at consume time are captured here at enqueue instead, so the
// consumer thread never touches producer-owned objects:
//   * the upload payload -- the transfer engine publishes a completed upload's
//     RGBA8 copy only after its UploadVRAMTexture push returns, so the record is
//     held back and the payload is attached at the producer's NEXT event (the
//     same point in stream order the in-thread consumer applies it); the
//     consumer side reads it back through LastUploadPayload();
//   * the CRTC snapshot of a frame record (PMODE / DISPFB / DISPLAY at the push),
//     read back through CurrentFrameCrtc().
//
// Synchronisation (the frames lane must be byte-identical to the in-thread path):
//   * Policy::Lockstep (the default): every push waits until the consumer has
//     processed it. The interleaving of producer and consumer over the shared GS
//     local-memory image (the EE side writes and settles it at every flush and
//     transfer; the pipeline reads, stages deposits into it) is then exactly the
//     in-thread one, so the frames are identical by construction; what moves is
//     WHERE the pipeline runs (the GL context lives on the consumer thread).
//   * Policy::Pipelined: pushes return at once (bounded by `capacity`); the
//     producer calls Sync() where it needs the pipeline's results and WaitFrame()
//     at the frame boundary before reading the frame grab. Identity under this
//     policy needs the GS local-memory image SPLIT: the EE keeps its own image
//     (its writes, marks and the upload epoch, for its own reads), the pipeline
//     runs on a second image, and every EE-side call on the EE image is logged
//     (VramMirror -> VramOpLog, `ProducerMirror()`) and carried in the
//     stream as a record delivered right before the next record -- the point the
//     in-thread consumer would have observed those writes (nothing runs on the
//     consumer between two records). The consumer image then interleaves the
//     EE's calls with its own deposits in stream order, exactly as a shared
//     image did inline. The EE's remaining reads of pipeline-side state (a
//     local->local copy's source bytes, the savestate's VRAM section) go through
//     Sync() + the consumer image on the host side -- in the deterministic
//     profile. In play (frame skip on) the game thread never waits: the EE image
//     is authoritative for everything the EE wrote, and the only pipeline-side
//     state a local->local copy can need is a GPU target's rendered pixels,
//     which ServeLocalToLocalSource takes from the consumer's LAST COMPLETED
//     FRAME snapshot (published at each frame record once armed) with the
//     staleness counted -- never a block.
//   * Frame skip (SetFrameSkip, Pipelined only -- the clock contract, rule 2: the
//     renderer never blocks the game): the ring holds at most `pending_frames`
//     (1 or 2) complete pending frames. When a frame record is pushed while the
//     consumer is still busy with an earlier frame and that many older frames are
//     still whole in the ring (not started), the OLDEST of them is dropped: its
//     Draw records are erased and its frame record is delivered as a drop (no
//     present, no grab; counted `frames_dropped`), while its jobs (uploads,
//     window/front-buffer state, invalidations), VRAM op-logs and ordered work
//     stay in stream order so the consumer image and the texture state remain
//     current. Pushes never wait for ring room under frame skip (the drop rule
//     bounds the ring), and the host does not call WaitFrame at the boundary
//     (frame_waits = 0).
// The producer's lock traffic (pipeline2, Paris: ~5600 records a frame, two mu_
// round trips each, 36 % of the drain thread's busy time waiting on the
// consumer's 64-record pop):
//   * the EE image's op log is CARRIED BY the next record (Record::ops, replayed
//     right before the record's own delivery) instead of travelling as its own
//     record -- the same point in stream order, half the records (every flush
//     settles the EE image, so an op log preceded nearly every draw);
//   * Pipelined pushes are STAGED on the producer (kStage records, or until a
//     frame / ordered-work record, a wait, or EndOfPass -- the drain pass is over
//     and nothing more arrives until the next kick) and enter the ring under one
//     lock. Lockstep pushes are never staged (each one waits for its delivery).
//     The staged records are producer-owned: Depth() counts the ring alone.
//   * the fault check at every push reads an atomic first; mu_ is taken only
//     when a fault is pending.
// A fault on the consumer thread (an exception out of the next sink -- the GS
// pipeline's refusals and GXSL errors) is caught there, the remaining records
// are dropped (counted as consumed so no waiter hangs), and the exception is
// rethrown ONCE on the producer thread at its next push / Sync / WaitFrame /
// RunOrdered -- the EE thread's exception path (the park, the triage bundle)
// then runs as it does inline; Faulted() stays set.
// Stop() drains the queue and joins the thread; the destructor stops.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/vram/vram_op_log.h"

namespace ps2r {
namespace gs {

class DrawIrThreadQueue : public DrawIrSink {
public:
    enum class Policy : uint8_t { Lockstep = 0, Pipelined };
    struct Hooks {
        // Producer side, at the event after an UploadVRAMTexture push: the
        // engine's completed upload copy (nullptr / empty = none).
        std::function<const std::vector<uint8_t>*()> upload_pixels;
        // Producer side, at a frame push: the CRTC snapshot.
        std::function<CrtcClassifier::Snapshot()> crtc;
    };
    struct Counters {
        uint64_t enqueued = 0;      // records pushed to the ring
        uint64_t consumed = 0;      // records the consumer thread delivered
        uint64_t frames = 0;        // frame records pushed
        uint64_t syncs = 0;         // Sync() calls that found work outstanding
        uint64_t frame_waits = 0;   // WaitFrame() calls that waited
        uint64_t max_depth = 0;     // deepest ring occupancy observed
        uint64_t payload_bytes = 0; // upload payload bytes copied
        uint64_t vram_ops = 0;      // VRAM op-log records pushed (the split image)
        uint64_t vram_op_bytes = 0; // their encoded bytes
        uint64_t frames_dropped = 0; // frames whose draws were skipped under frame skip (rule 2)
        uint64_t ordered_waits = 0;   // RunOrdered calls that found records outstanding ahead of theirs
        uint64_t ordered_wait_us = 0; // wall time the producer spent inside RunOrdered
        uint64_t sync_wait_us = 0;    // wall time the producer spent inside Sync() (an EE read of the pipeline image)
        // ServeLocalToLocalSource (play): copies served from the EE image alone, copies that
        // took GPU pages from the completed-frame snapshot, the frames those pages lagged the
        // stream by (sum / max), copies that found no snapshot yet (the first arms it).
        uint64_t l2l_ee_served = 0, l2l_snapshot_served = 0, l2l_stale_frames = 0, l2l_stale_max = 0, l2l_unserved = 0;
        uint64_t snapshots = 0;       // completed-frame snapshots the consumer published
        uint64_t presenter_work = 0;  // PostPresenterWork records queued (the re-view's per-edge jobs)
        // The producer's two in-Push waits (perf60 census): the lockstep wait for the record's
        // consumption and the ring-room wait (pipelined without frame skip), counts and wall time.
        uint64_t lockstep_waits = 0, lockstep_wait_us = 0;
        uint64_t room_waits = 0, room_wait_us = 0;
        // The consumer thread's own time inside the next sink, by record kind (ns): the
        // per-draw cost of the GS pipeline independent of the game's pace.
        uint64_t deliver_draw_ns = 0, deliver_frame_ns = 0, deliver_other_ns = 0;
        uint64_t delivered_draws = 0;
    };

    // `mirror_target`: the consumer-side VramImage the EE's logged calls are
    // replayed on (the split image); nullptr = one shared image (lockstep).
    DrawIrThreadQueue(DrawIrSink& next, Hooks hooks, Policy policy, size_t capacity = 4096,
                      VramImage* mirror_target = nullptr);
    ~DrawIrThreadQueue() override;
    DrawIrThreadQueue(const DrawIrThreadQueue&) = delete;
    DrawIrThreadQueue& operator=(const DrawIrThreadQueue&) = delete;

    // Starts the consumer thread (idempotent). `on_thread_start` runs on the
    // consumer thread before its first record (the host binds its GL context).
    void Start(std::function<void()> on_thread_start = nullptr);
    // Drains every queued record, then joins. Idempotent.
    void Stop();
    bool Running() const { return running_; }
    Policy policy() const { return policy_; }
    // Producer side, Pipelined only: at most `pending_frames` complete, unstarted
    // frames in the ring; the push that would make one more drops the OLDEST (see
    // above). 1 = the original rule (the previous frame is dropped as soon as the
    // consumer is still on the one before it); 2 lets the consumer lag a whole
    // field behind the stream before a picture is lost -- the same draws, delivered
    // one field later, so the pictures are identical by construction.
    void SetFrameSkip(bool on, uint32_t pending_frames = 1);
    bool FrameSkip() const;
    uint32_t FrameSkipDepth() const;
    // The consumer thread threw out of the next sink (see the header comment).
    bool Faulted() const;

    // DrawIrSink (the producer side).
    void OnDraw(const DrawIr& draw) override;
    void OnDrawOwned(DrawIr&& draw) override;   // the record takes the vertices by move
    void OnJob(const JobRec& job) override;
    void OnFrame(const JobRec& vsync) override;

    // The EE image's mirror when a mirror target was given (nullptr otherwise):
    // the host sets it on the EE's VramImage; the logged calls travel as records.
    VramMirror* ProducerMirror() { return mirror_target_ ? &ops_ : nullptr; }
    VramImage* MirrorTarget() const { return mirror_target_; }

    // Producer side: block until every record pushed so far was consumed (the
    // pending op log is pushed first, so the consumer image is current after).
    // Counted (syncs / sync_wait_us): an EE read of pipeline-side state.
    void Sync();
    // The same wait at the leg's end / a stop (the census reads the consumer's
    // counters after it drained): not an EE wait in play, not counted.
    void Quiesce();
    // Play (frame skip on), the producer side: bring into `image` (the EE's) the
    // bytes a local->local copy of `source` needs that the EE image lacks -- the
    // pages a GPU target authored, taken from the consumer's last completed frame
    // snapshot where the EE has not written them since (the EE image stays
    // authoritative for its own writes). Never waits. Returns true when a GPU
    // page was in the source (the engine's local_local_over_gpu row). The first
    // call arms the snapshot (served from the EE image alone, counted unserved).
    bool ServeLocalToLocalSource(VramImage::Span source, VramImage& image);
    // Host-owned output crossing: publish the held upload, then run work on
    // this queue's consumer in stream order. Waits in both queue policies.
    // Work must drain the downstream consumer's pending upload before using
    // shared surfaces (DrawIrQueueConsumer::RunOrdered supplies that boundary).
    // Inputs are owned captures, never guest pointers read on delivery.
    bool RunOrdered(std::function<bool()> work);
    // The non-waiting form (the clock contract, rule 2 -- the game thread never
    // waits for the consumer): the work runs on the consumer in stream order; its
    // result is the work's own business (count it there). Inline when no consumer
    // thread runs. Nothing is returned to the producer.
    void RunOrderedAsync(std::function<bool()> work);
    // ANY thread (the presenter's): work for the consumer thread at its NEXT
    // record boundary -- ahead of the ring's backlog, never inside a record --
    // without touching the producer's held upload / op log / fault state (those
    // belong to the game thread). The display-rate re-view's per-edge job: it
    // draws into its own shadow surfaces, so it may run between any two stream
    // records; behind the backlog it would run a frame late. Never waits; false
    // when no consumer thread runs or it faulted (nothing queued).
    bool PostPresenterWork(std::function<bool()> work);
    // ANY thread: records queued and not yet popped (the consumer's backlog; the
    // producer's staged records are not in it).
    size_t Depth() const;
    // Producer side: the drain pass is over -- hand the staged records to the ring
    // now (nothing more arrives until the next kick). No-op when nothing is staged.
    void EndOfPass();
    // Producer side, before Start: how many Pipelined records may be staged before
    // the ring sees them (1 = every push enters the ring at once, the pre-staging
    // behaviour). Default kStage.
    void SetStageDepth(uint32_t records);
    uint32_t StageDepth() const { return stage_depth_; }
    // ANY thread: the consumer thread was idle (waiting for records) within the
    // last `within` -- it keeps up with the stream; a consumer busy for longer is
    // behind. False before the thread first slept.
    bool ConsumerIdleWithin(std::chrono::nanoseconds within) const;
    // Producer side: the ordinal of the last frame record pushed (0 = none).
    uint64_t FrameSeq() const;
    // Producer side: block until the consumer processed frame record `seq`.
    void WaitFrame(uint64_t seq);

    // Consumer side (valid on the consumer thread, during and after the
    // records' delivery): the payload of the most recent UploadVRAMTexture
    // record delivered (nullptr before any), the CRTC snapshot captured with
    // the frame record being delivered.
    const std::vector<uint8_t>* LastUploadPayload() const { return last_payload_valid_ ? &last_payload_ : nullptr; }
    const CrtcClassifier::Snapshot& CurrentFrameCrtc() const { return current_crtc_; }

    Counters Count() const;

private:
    enum class Kind : uint8_t { Draw, Job, Frame, Work, VramOps, FrameDropped };
    struct Record {
        Kind kind = Kind::Job;
        uint64_t frame = 0;                             // the frame ordinal this record belongs to
        DrawIr draw;
        JobRec job{};
        bool has_payload = false;
        std::vector<uint8_t> payload;
        CrtcClassifier::Snapshot crtc{};
        std::function<bool()> work;                     // Kind::Work body (RunOrdered)
        std::shared_ptr<std::promise<bool>> done;       // its waiter (false when dropped)
        bool has_ops = false;                           // ops carried: replayed right before this record
        VramOpLog ops;
    };
    void AttachPendingPayload();
    void FlushVramOps();                 // the pending op log as its own record (a wait follows)
    void CarryVramOps(Record& r);        // the pending op log rides on `r`
    void WaitDrainedLocked(std::unique_lock<std::mutex>& lk);
    void PublishSnapshot(uint64_t frame);   // consumer side, at a frame record, when armed
    void DropOlderPendingFrameLocked(uint64_t arriving);   // frame skip: mu_ held
    void RethrowPendingFault();   // producer side; throws at most once
    void Push(Record&& r);
    void FlushStaged();           // producer side: the staged records enter the ring under one lock
    void Deliver(Record& r);
    void ThreadMain();
    // The consumer's bookkeeping since its last lock (ThreadMain folds it in batches).
    struct Pending {
        uint64_t consumed = 0, frames = 0, draws = 0;
        uint64_t draw_ns = 0, frame_ns = 0, other_ns = 0;
    };
    static constexpr size_t kBatch = 64;   // records popped per lock, at most (a frame record ends a batch)
    static constexpr uint32_t kStage = 64; // records staged per lock on the producer, at most
    void FoldPendingLocked(Pending& p);

    DrawIrSink& next_;
    Hooks hooks_;
    const Policy policy_;
    const size_t capacity_;
    std::function<void()> on_thread_start_;

    // Producer-owned: an UploadVRAMTexture record held until its payload exists.
    bool pending_valid_ = false;
    Record pending_;
    // Producer-owned: the EE image's calls since the last record (split image).
    VramImage* const mirror_target_;
    VramOpLog ops_;
    // Producer-owned: the Pipelined records not yet in the ring (FlushStaged).
    std::vector<Record> staged_;
    uint32_t stage_depth_ = kStage;
    uint64_t frames_staged_ = 0;         // frames_pushed_ as the producer sees it (its own writes)

    mutable std::mutex mu_;
    std::condition_variable cv_work_;    // consumer waits for records
    std::condition_variable cv_done_;    // producer waits for consumption / room
    // Who is asleep (under mu_): the consumer on cv_work_, how many producer-side
    // waiters on cv_done_. A push signals cv_work_ only when the consumer sleeps and
    // the consumer signals cv_done_ only when someone waits -- the same wake-ups,
    // without a kernel call per record (track 1d: ~3 % of the game thread was
    // pthread_cond_signal with no waiter).
    bool consumer_sleeping_ = false;
    std::atomic<int64_t> last_idle_ns_{0};   // steady_clock ns the consumer last slept / woke
    // Written under mu_; the consumer also reads it lock-free between the records of
    // a batch to publish its pending counts as soon as someone waits.
    std::atomic<uint32_t> done_waiters_{0};
    // cv_done_.wait with the waiter count kept (the predicate form).
    void WaitDoneLocked(std::unique_lock<std::mutex>& lk, const std::function<bool()>& pred);
    std::deque<Record> ring_;
    std::deque<std::function<bool()>> presenter_work_;   // PostPresenterWork: served before the ring
    std::thread thread_;
    bool running_ = false;
    std::atomic<bool> running_flag_{false};   // running_ for the producer's lock-free read
    bool stop_ = false;
    uint64_t enqueued_ = 0, consumed_ = 0;
    uint64_t frames_pushed_ = 0, frames_consumed_ = 0;
    bool frame_skip_ = false;
    uint32_t frame_skip_depth_ = 1;     // complete pending frames the ring may hold (SetFrameSkip)
    uint64_t consuming_frame_ = 0;      // frame ordinal of the record the consumer popped last
    Counters n_;
    bool faulted_ = false;              // consumer threw; records are dropped from here on
    std::exception_ptr fault_;          // the exception, until the producer rethrew it
    std::atomic<bool> fault_pending_{false};   // fault_ is set (the producer's lock-free check)

    // The last completed frame's image (play, the local->local source): the
    // consumer copies the pipeline image's bytes and page authors/epochs here at
    // each frame record once armed; the producer reads it under snap_mu_.
    struct FrameSnapshot {
        std::vector<uint8_t> bytes;
        std::vector<PageRecord::Author> author;
        std::vector<uint64_t> upload_epoch;
        uint64_t frame = 0;   // the frame ordinal it completed
        bool valid = false;
    };
    std::atomic<bool> snapshot_armed_{false};
    mutable std::mutex snap_mu_;
    FrameSnapshot snap_;

    // Consumer-owned.
    bool last_payload_valid_ = false;
    std::vector<uint8_t> last_payload_;
    CrtcClassifier::Snapshot current_crtc_{};
};

}  // namespace gs
}  // namespace ps2r
