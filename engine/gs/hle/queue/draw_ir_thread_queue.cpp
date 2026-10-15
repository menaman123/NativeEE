// gs/hle/queue/draw_ir_thread_queue.cpp -- the cross-thread DrawIR ring (see .h).
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"

#include <chrono>
#include <cstring>

namespace ps2r {
namespace gs {

DrawIrThreadQueue::DrawIrThreadQueue(DrawIrSink& next, Hooks hooks, Policy policy, size_t capacity,
                                     VramImage* mirror_target)
    : next_(next), hooks_(std::move(hooks)), policy_(policy), capacity_(capacity == 0 ? 1 : capacity),
      mirror_target_(mirror_target) {
    pending_.kind = Kind::Job;
}

DrawIrThreadQueue::~DrawIrThreadQueue() { Stop(); }

void DrawIrThreadQueue::Start(std::function<void()> on_thread_start) {
    std::lock_guard<std::mutex> lk(mu_);
    if (running_) return;
    on_thread_start_ = std::move(on_thread_start);
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { ThreadMain(); });
}

void DrawIrThreadQueue::Stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return;
        stop_ = true;
    }
    cv_work_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lk(mu_);
    running_ = false;
}

// ---- producer side ----------------------------------------------------------------

// The EE image's calls since the last record travel as one record, delivered
// before the next record: the in-thread consumer saw them at exactly that point.
void DrawIrThreadQueue::FlushVramOps() {
    if (ops_.Empty()) return;
    Record r;
    r.kind = Kind::VramOps;
    r.ops = std::move(ops_);
    ops_ = VramOpLog{};
    n_.vram_ops++;
    n_.vram_op_bytes += r.ops.Bytes();
    Push(std::move(r));
}

// The UploadVRAMTexture record waits for its payload: attached (copied) now, at
// the producer's next event -- the point the in-thread consumer applies it. The
// EE's VRAM calls since the previous record are flushed first (they precede the
// next record either way; the held upload's delivery only parks it in the
// consumer, which applies it at ITS next event).
void DrawIrThreadQueue::AttachPendingPayload() {
    FlushVramOps();
    if (!pending_valid_) return;
    pending_valid_ = false;
    const std::vector<uint8_t>* px = hooks_.upload_pixels ? hooks_.upload_pixels() : nullptr;
    if (px && !px->empty()) {
        pending_.payload = *px;
        pending_.has_payload = true;
        n_.payload_bytes += px->size();
    }
    Push(std::move(pending_));
    pending_ = Record{};
    pending_.kind = Kind::Job;
}

bool DrawIrThreadQueue::Faulted() const {
    std::lock_guard<std::mutex> lk(mu_);
    return faulted_;
}

// The consumer's exception surfaces on the producer thread once; the queue stays
// faulted (the later pushes are dropped by the consumer, the waits return).
void DrawIrThreadQueue::RethrowPendingFault() {
    std::exception_ptr e;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!fault_) return;
        e = fault_;
        fault_ = nullptr;
    }
    std::rethrow_exception(e);
}

void DrawIrThreadQueue::SetFrameSkip(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    frame_skip_ = on && policy_ == Policy::Pipelined;
}

bool DrawIrThreadQueue::FrameSkip() const {
    std::lock_guard<std::mutex> lk(mu_);
    return frame_skip_;
}

// Frame skip (rule 2), at the push of frame record `arriving`: the previous frame
// is dropped iff it is still WHOLE in the ring -- its frame record present and the
// consumer has not popped any of its records (it is busy with an older frame).
// Its draws go; its jobs, VRAM op-logs and ordered work stay in stream order; its
// frame record becomes a drop marker (delivered, counted, never presented).
void DrawIrThreadQueue::DropOlderPendingFrameLocked(uint64_t arriving) {
    if (arriving < 2) return;
    const uint64_t older = arriving - 1;
    if (consuming_frame_ >= older) return;   // the consumer already started (or finished) it
    bool whole = false;
    for (const Record& r : ring_)
        if (r.frame == older && r.kind == Kind::Frame) { whole = true; break; }
    if (!whole) return;
    std::deque<Record> kept;
    uint64_t erased = 0;
    for (Record& r : ring_) {
        if (r.frame == older) {
            if (r.kind == Kind::Draw) { ++erased; continue; }
            if (r.kind == Kind::Frame) r.kind = Kind::FrameDropped;
        }
        kept.push_back(std::move(r));
    }
    ring_.swap(kept);
    consumed_ += erased;   // an erased draw counts as consumed (Sync waits on consumed >= enqueued)
    n_.consumed = consumed_;
    n_.frames_dropped++;
}

void DrawIrThreadQueue::Push(Record&& r) {
    RethrowPendingFault();
    uint64_t my_seq;
    bool wake = false;
    {
        std::unique_lock<std::mutex> lk(mu_);
        if (!running_) {
            // No consumer thread: deliver inline (the queue is transparent).
            lk.unlock();
            Deliver(r);
            return;
        }
        if (r.kind == Kind::Frame) {
            r.frame = frames_pushed_;
            if (frame_skip_) DropOlderPendingFrameLocked(frames_pushed_);
        } else {
            r.frame = frames_pushed_ + 1;
        }
        if (!frame_skip_) WaitDoneLocked(lk, [this] { return ring_.size() < capacity_; });
        ring_.push_back(std::move(r));
        my_seq = ++enqueued_;
        n_.enqueued = enqueued_;
        if (ring_.size() > n_.max_depth) n_.max_depth = ring_.size();
        wake = consumer_sleeping_;
    }
    if (wake) cv_work_.notify_one();
    if (policy_ == Policy::Lockstep) {
        std::unique_lock<std::mutex> lk(mu_);
        WaitDoneLocked(lk, [this, my_seq] { return consumed_ >= my_seq; });
    }
}

void DrawIrThreadQueue::OnDraw(const DrawIr& draw) {
    AttachPendingPayload();
    Record r;
    r.kind = Kind::Draw;
    r.draw = draw;
    Push(std::move(r));
}

void DrawIrThreadQueue::OnDrawOwned(DrawIr&& draw) {
    AttachPendingPayload();
    Record r;
    r.kind = Kind::Draw;
    r.draw = std::move(draw);
    Push(std::move(r));
}

void DrawIrThreadQueue::OnJob(const JobRec& job) {
    AttachPendingPayload();
    Record r;
    r.kind = Kind::Job;
    r.job = job;
    if (static_cast<JobKind>(job.type) == JobKind::UploadVRAMTexture && job.payload_bytes > 0) {
        pending_ = std::move(r);
        pending_valid_ = true;
        return;
    }
    Push(std::move(r));
}

void DrawIrThreadQueue::OnFrame(const JobRec& vsync) {
    AttachPendingPayload();
    Record r;
    r.kind = Kind::Frame;
    r.job = vsync;
    if (hooks_.crtc) r.crtc = hooks_.crtc();
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++frames_pushed_;
        n_.frames = frames_pushed_;
    }
    Push(std::move(r));
}

void DrawIrThreadQueue::WaitDoneLocked(std::unique_lock<std::mutex>& lk, const std::function<bool()>& pred) {
    ++done_waiters_;
    cv_done_.wait(lk, pred);
    --done_waiters_;
}

void DrawIrThreadQueue::WaitDrainedLocked(std::unique_lock<std::mutex>& lk) {
    WaitDoneLocked(lk, [this] { return consumed_ >= enqueued_; });
}

void DrawIrThreadQueue::Sync() {
    FlushVramOps();
    RethrowPendingFault();
    std::unique_lock<std::mutex> lk(mu_);
    if (!running_ || consumed_ >= enqueued_) return;
    n_.syncs++;
    const auto t0 = std::chrono::steady_clock::now();
    WaitDrainedLocked(lk);
    n_.sync_wait_us += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
}

void DrawIrThreadQueue::Quiesce() {
    FlushVramOps();
    RethrowPendingFault();
    std::unique_lock<std::mutex> lk(mu_);
    if (!running_ || consumed_ >= enqueued_) return;
    WaitDrainedLocked(lk);
}

// The consumer's last completed frame, read on the producer: a source page the
// GPU authored there is brought into the EE image unless the EE wrote that page
// since (its upload epoch moved past the snapshot's -- the two images stamp the
// same EE call sequence, so the epochs compare). Bytes only, as the drained
// wholesale copy did: the EE image's marks stay the EE's.
bool DrawIrThreadQueue::ServeLocalToLocalSource(VramImage::Span source, VramImage& image) {
    bool over_gpu = false;
    uint32_t first, count;
    VramImage::PagesOf(source, &first, &count);
    std::lock_guard<std::mutex> lk(snap_mu_);
    if (!snap_.valid) {
        snapshot_armed_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> ln(mu_);
        n_.l2l_unserved++;
        return false;
    }
    uint64_t stale;
    {
        std::lock_guard<std::mutex> ln(mu_);
        stale = frames_pushed_ > snap_.frame ? frames_pushed_ - snap_.frame : 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t page = (first + i) % VramImage::kPageCount;
        if (snap_.author[page] != PageRecord::Author::GpuTarget) continue;
        over_gpu = true;
        if (image.Page(page).upload_epoch > snap_.upload_epoch[page]) continue;   // the EE wrote it since
        std::memcpy(image.MutableBytes() + static_cast<size_t>(page) * VramImage::kPageBytes,
                    snap_.bytes.data() + static_cast<size_t>(page) * VramImage::kPageBytes, VramImage::kPageBytes);
    }
    std::lock_guard<std::mutex> ln(mu_);
    if (over_gpu) {
        n_.l2l_snapshot_served++;
        n_.l2l_stale_frames += stale;
        if (stale > n_.l2l_stale_max) n_.l2l_stale_max = stale;
    } else {
        n_.l2l_ee_served++;
    }
    return over_gpu;
}

bool DrawIrThreadQueue::RunOrdered(std::function<bool()> work) {
    if (!work) return false;
    AttachPendingPayload();
    RethrowPendingFault();
    auto result = std::make_shared<std::promise<bool>>();
    auto done = result->get_future();
    Record record;
    record.kind = Kind::Work;
    record.work = std::move(work);
    record.done = result;
    bool outstanding = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        outstanding = running_ && !ring_.empty();
    }
    const auto t0 = std::chrono::steady_clock::now();
    Push(std::move(record));
    const bool ok = done.get();   // a dropped (post-fault) work record resolves false
    {
        // The producer waited for the consumer here (the owned graphics output's
        // ordered deliveries): the census row the clock contract's rule 2 reads
        // beside frame_waits.
        std::lock_guard<std::mutex> lk(mu_);
        if (outstanding) n_.ordered_waits++;
        n_.ordered_wait_us += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    }
    RethrowPendingFault();        // the fault that dropped it surfaces here
    return ok;
}

void DrawIrThreadQueue::RunOrderedAsync(std::function<bool()> work) {
    if (!work) return;
    AttachPendingPayload();
    RethrowPendingFault();
    Record record;
    record.kind = Kind::Work;
    record.work = std::move(work);
    Push(std::move(record));
}

bool DrawIrThreadQueue::PostPresenterWork(std::function<bool()> work) {
    if (!work) return false;
    bool wake = false;
    {
        std::unique_lock<std::mutex> lk(mu_);
        if (!running_ || faulted_) return false;
        presenter_work_.push_back(std::move(work));
        n_.presenter_work++;
        wake = consumer_sleeping_;
    }
    if (wake) cv_work_.notify_one();
    return true;
}

bool DrawIrThreadQueue::ConsumerIdleWithin(std::chrono::nanoseconds within) const {
    const int64_t last = last_idle_ns_.load(std::memory_order_relaxed);
    if (last == 0) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now - last <= within.count();
}

size_t DrawIrThreadQueue::Depth() const {
    std::lock_guard<std::mutex> lk(mu_);
    return ring_.size();
}

uint64_t DrawIrThreadQueue::FrameSeq() const {
    std::lock_guard<std::mutex> lk(mu_);
    return frames_pushed_;
}

void DrawIrThreadQueue::WaitFrame(uint64_t seq) {
    FlushVramOps();
    RethrowPendingFault();
    std::unique_lock<std::mutex> lk(mu_);
    if (!running_ || frames_consumed_ >= seq) return;
    n_.frame_waits++;
    WaitDoneLocked(lk, [this, seq] { return frames_consumed_ >= seq; });
}

DrawIrThreadQueue::Counters DrawIrThreadQueue::Count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return n_;
}

// ---- consumer side ----------------------------------------------------------------

void DrawIrThreadQueue::Deliver(Record& r) {
    switch (r.kind) {
        case Kind::Draw:
            next_.OnDraw(r.draw);
            break;
        case Kind::Job:
            if (r.has_payload) {
                last_payload_ = std::move(r.payload);
                last_payload_valid_ = true;
            } else if (static_cast<JobKind>(r.job.type) == JobKind::UploadVRAMTexture) {
                last_payload_valid_ = false;   // an upload whose payload never existed
            }
            next_.OnJob(r.job);
            break;
        case Kind::Frame:
            current_crtc_ = r.crtc;
            next_.OnFrame(r.job);
            if (snapshot_armed_.load(std::memory_order_acquire)) PublishSnapshot(r.frame);
            break;
        case Kind::Work: {
            bool ok = false;
            try { ok = r.work(); }
            catch (...) {}
            if (r.done) r.done->set_value(ok);
            break;
        }
        case Kind::VramOps:
            if (mirror_target_) r.ops.Replay(*mirror_target_);
            break;
        case Kind::FrameDropped:
            // Rule 2: the frame's draws were skipped; nothing is presented for it.
            // Its jobs and VRAM ops did land, so the image is still a completed point.
            if (snapshot_armed_.load(std::memory_order_acquire)) PublishSnapshot(r.frame);
            break;
    }
}

// Consumer side, after a frame record: the pipeline image as of this frame --
// its committed bytes, page authors and EE upload epochs -- for the producer's
// local->local source. ~4 MiB per frame, only once a copy armed it.
void DrawIrThreadQueue::PublishSnapshot(uint64_t frame) {
    if (!mirror_target_) return;
    std::lock_guard<std::mutex> lk(snap_mu_);
    if (snap_.bytes.size() != VramImage::kBytes) {
        snap_.bytes.resize(VramImage::kBytes);
        snap_.author.resize(VramImage::kPageCount);
        snap_.upload_epoch.resize(VramImage::kPageCount);
    }
    std::memcpy(snap_.bytes.data(), mirror_target_->Bytes(), VramImage::kBytes);
    for (uint32_t p = 0; p < VramImage::kPageCount; ++p) {
        const PageRecord& rec = mirror_target_->Page(p);
        snap_.author[p] = rec.author;
        snap_.upload_epoch[p] = rec.upload_epoch;
    }
    snap_.valid = true;
    snap_.frame = frame;
    std::lock_guard<std::mutex> ln(mu_);
    n_.snapshots++;
}

void DrawIrThreadQueue::ThreadMain() {
    if (on_thread_start_) on_thread_start_();
    for (;;) {
        Record r;
        bool faulted;
        {
            std::unique_lock<std::mutex> lk(mu_);
            while (!stop_ && ring_.empty() && presenter_work_.empty()) {
                consumer_sleeping_ = true;
                cv_work_.wait(lk);
                consumer_sleeping_ = false;
                last_idle_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
            }
            if (!presenter_work_.empty()) {
                // The presenter's job: ahead of the backlog, between two records.
                std::function<bool()> work = std::move(presenter_work_.front());
                presenter_work_.pop_front();
                lk.unlock();
                try { work(); } catch (...) {}
                continue;
            }
            if (ring_.empty()) return;   // stop_ with nothing left: drained
            r = std::move(ring_.front());
            ring_.pop_front();
            if (r.frame > consuming_frame_) consuming_frame_ = r.frame;
            faulted = faulted_;
        }
        if (!faulted) {
            try {
                Deliver(r);
            } catch (...) {
                std::lock_guard<std::mutex> lk(mu_);
                faulted_ = true;
                fault_ = std::current_exception();
                faulted = true;
            }
        }
        if (faulted && r.kind == Kind::Work && r.done) r.done->set_value(false);   // dropped: its waiter resolves
        bool wake;
        {
            std::lock_guard<std::mutex> lk(mu_);
            ++consumed_;
            n_.consumed = consumed_;
            if (r.kind == Kind::Frame || r.kind == Kind::FrameDropped) ++frames_consumed_;
            wake = done_waiters_ > 0;
        }
        if (wake) cv_done_.notify_all();
    }
}

}  // namespace gs
}  // namespace ps2r
