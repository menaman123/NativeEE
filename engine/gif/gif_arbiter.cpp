#include "engine/gif/gif_arbiter.h"

#include "engine/ee/timing/tick_profile.h"

#include <cstring>
#include <utility>

#include "engine/gif/gif_boundary_hook.h"
#include "engine/gif/gif_drain_thread.h"
#include "engine/gif/gif_marker_owner.h"
#include "engine/gif/gif_parser.h"

namespace {

constexpr size_t kTagBytes = 16;

// A packet whose leading GIFtag is FLG=IMAGE.
bool IsImagePacket(const uint8_t* data, size_t size) {
    if (data == nullptr || size < kTagBytes) return false;
    uint64_t lo;
    std::memcpy(&lo, data, 8);
    return ((lo >> 58) & 3u) == static_cast<uint64_t>(GifFormat::Image);
}

}  // namespace

GifArbiter::GifArbiter(GifParser& parser, GifBoundaryHook* boundary_hook)
    : parser_(parser), boundary_hook_(boundary_hook) {}

void GifArbiter::SetSubmitObserver(SubmitObserver fn, void* user) {
    observer_ = fn;
    observer_user_ = user;
}

void GifArbiter::SetDrainObserver(DrainObserver fn, void* user) {
    drain_observer_ = fn;
    drain_observer_user_ = user;
}

void GifArbiter::SetMarkerOwner(GifMarkerOwner* owner) { owner_ = owner; }

void GifArbiter::AttachDrainThread(GifDrainThread* thread) { drain_thread_ = thread; }

bool GifArbiter::WorkerRunning() const { return owner_ != nullptr && owner_->Running(); }

bool GifArbiter::DrainThreadRunning() const {
    return drain_thread_ != nullptr && drain_thread_->Running();
}

bool GifArbiter::OnDrainThread() const {
    return drain_thread_ != nullptr && drain_thread_->OnDrainThread();
}

void GifArbiter::DrainFence() {
    if (drain_thread_ != nullptr) drain_thread_->DrainFence();
}

uint64_t GifArbiter::PendingBytes() const { return pending_bytes_.load(std::memory_order_relaxed); }
uint64_t GifArbiter::PendingSlots() const { return inbox_slots_.load(std::memory_order_relaxed); }
uint64_t GifArbiter::DrainedPackets() const { return drained_packets_.load(std::memory_order_relaxed); }

void GifArbiter::PushInboxSlot(GifArbiterPacket&& pkt) {
    {
        std::lock_guard<std::mutex> lock(mtvu_mtx_);
        inbox_.push_back(InboxSlot{std::move(pkt), nullptr});
        inbox_slots_.fetch_add(1, std::memory_order_relaxed);
    }
    if (DrainThreadRunning()) drain_thread_->Notify();
}

// Routing: a worker-thread submit fills the span's marker; any other submit
// while the worker or the drain thread runs goes to the inbox tail; otherwise
// straight to the drain owner's queue.
void GifArbiter::Enqueue(GifArbiterPacket&& pkt) {
    pending_bytes_.fetch_add(pkt.data.size(), std::memory_order_relaxed);
    if (WorkerRunning()) {
        if (owner_->OnWorkerThread()) {
            std::shared_ptr<GifMtvuMarker> m = owner_->ActiveMarker();
            if (m) {
                m->filled.push_back(std::move(pkt));
                return;
            }
        }
        PushInboxSlot(std::move(pkt));
        return;
    }
    if (DrainThreadRunning() && !OnDrainThread()) {
        PushInboxSlot(std::move(pkt));
        return;
    }
    // A frame/library marker already in the inbox is older than this item.
    // Keep the producer's order when inline work resumes behind that marker.
    if (inbox_slots_.load(std::memory_order_relaxed) != 0)
        PushInboxSlot(std::move(pkt));
    else
        queue_.push_back(std::move(pkt));
}

void GifArbiter::submit(GifPath path, const uint8_t* data, size_t size, bool is_directhl) {
    if (data == nullptr || size < kTagBytes) return;
    if (observer_) observer_(observer_user_, path, data, size);
    GifArbiterPacket pkt;
    pkt.path = path;
    pkt.is_directhl = path == GifPath::Path2 && is_directhl;
    pkt.is_path3_image = path == GifPath::Path3 && IsImagePacket(data, size);
    pkt.data.assign(data, data + size);
    Enqueue(std::move(pkt));
}

void GifArbiter::submitOwned(GifPath path, std::vector<uint8_t>&& data, bool is_directhl) {
    if (data.size() < kTagBytes) return;
    if (observer_) observer_(observer_user_, path, data.data(), data.size());
    GifArbiterPacket pkt;
    pkt.path = path;
    pkt.is_directhl = path == GifPath::Path2 && is_directhl;
    pkt.is_path3_image = path == GifPath::Path3 && IsImagePacket(data.data(), data.size());
    pkt.data = std::move(data);
    Enqueue(std::move(pkt));
}

void GifArbiter::submitDirectOwned(std::vector<uint8_t>&& fallback, std::function<void(bool)> deliver) {
    if (fallback.size() < kTagBytes) return;
    if (observer_) observer_(observer_user_, GifPath::Path1, fallback.data(), fallback.size());
    GifArbiterPacket pkt;
    pkt.path = GifPath::Path1;
    pkt.data = std::move(fallback);
    pkt.direct = std::move(deliver);
    Enqueue(std::move(pkt));
}

void GifArbiter::submitOrdered(std::function<void()> work) {
    if (!work) return;
    GifArbiterPacket pkt;
    pkt.path = GifPath::Path1;
    pkt.ordered = std::move(work);
    Enqueue(std::move(pkt));
}

std::vector<uint8_t> GifArbiter::acquireBuffer() {
    std::lock_guard<std::mutex> lock(pool_mtx_);
    if (pool_.empty()) return {};
    std::vector<uint8_t> b = std::move(pool_.back());
    pool_.pop_back();
    return b;
}

void GifArbiter::recycleBuffer(std::vector<uint8_t>&& buffer) {
    if (buffer.capacity() < kPoolMinCapacity || buffer.capacity() > kPoolMaxCapacity) return;
    buffer.clear();
    std::lock_guard<std::mutex> lock(pool_mtx_);
    if (pool_.size() < kPoolMax) pool_.push_back(std::move(buffer));
}

size_t GifArbiter::PoolSize() const {
    std::lock_guard<std::mutex> lock(pool_mtx_);
    return pool_.size();
}

// ---- markers ----------------------------------------------------------------

std::shared_ptr<GifMtvuMarker> GifArbiter::openMarker() {
    auto m = std::make_shared<GifMtvuMarker>();
    std::lock_guard<std::mutex> lock(mtvu_mtx_);
    inbox_.push_back(InboxSlot{GifArbiterPacket{}, m});
    inbox_slots_.fetch_add(1, std::memory_order_relaxed);
    return m;
}

void GifArbiter::sealMarker(const std::shared_ptr<GifMtvuMarker>& marker) {
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mtvu_mtx_);
        marker->sealed = true;
        if (!marker->detached) {
            sealed_ready_.fetch_add(1, std::memory_order_relaxed);
            notify = true;
        } else {
            // Detached: never collected, so its bytes settle here.
            uint64_t freed = 0;
            for (const GifArbiterPacket& p : marker->filled) freed += GifArbPayloadBytes(p);
            if (freed) pending_bytes_.fetch_sub(freed, std::memory_order_relaxed);
        }
    }
    if (notify && DrainThreadRunning()) drain_thread_->Notify();
}

bool GifArbiter::hasSealedReady() const {
    return sealed_ready_.load(std::memory_order_relaxed) != 0;
}

// Promote the ready prefix of the inbox: plain slots and sealed markers
// (expanded in place), stopping at the first unsealed marker.
void GifArbiter::collectReady() {
    std::lock_guard<std::mutex> lock(mtvu_mtx_);
    while (!inbox_.empty()) {
        InboxSlot& s = inbox_.front();
        if (!s.marker) {
            queue_.push_back(std::move(s.pkt));
            inbox_.pop_front();
            inbox_slots_.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }
        if (!s.marker->sealed) break;
        for (GifArbiterPacket& p : s.marker->filled) queue_.push_back(std::move(p));
        sealed_ready_.fetch_sub(1, std::memory_order_relaxed);
        inbox_.pop_front();
        inbox_slots_.fetch_sub(1, std::memory_order_relaxed);
    }
}

// ---- boundary slots ---------------------------------------------------------

void GifArbiter::openBoundary(uint32_t seq, uint64_t enqueue_ns) {
    if (!bnd_feed_noted_) bnd_order_violations_.fetch_add(1, std::memory_order_relaxed);
    bnd_feed_noted_ = false;
    GifArbiterPacket pkt;
    pkt.boundary_seq = seq == 0 ? 1u : seq;
    pkt.bnd_enq_ns = enqueue_ns;
    bnd_enqueued_.fetch_add(1, std::memory_order_relaxed);
    PushInboxSlot(std::move(pkt));
}

void GifArbiter::noteFeedForBoundary() { bnd_feed_noted_ = true; }

bool GifArbiter::dispatchBoundaryInline(GifArbiterPacket& pkt) {
    if (pkt.boundary_seq == 0) return false;
    bnd_delivered_.fetch_add(1, std::memory_order_relaxed);
    if (boundary_hook_) boundary_hook_->OnBoundary(pkt.boundary_seq, pkt.bnd_enq_ns);
    return true;
}

GifBoundaryStats GifArbiter::boundary_stats() const {
    GifBoundaryStats s;
    s.enqueued = bnd_enqueued_.load(std::memory_order_relaxed);
    s.delivered = bnd_delivered_.load(std::memory_order_relaxed);
    s.order_violations = bnd_order_violations_.load(std::memory_order_relaxed);
    return s;
}

// ---- drain ------------------------------------------------------------------

void GifArbiter::drain() {
    if (DrainThreadRunning() && !OnDrainThread()) {
        drain_thread_->Notify();
        return;
    }
    DrainPass();
}

void GifArbiter::DrainPass() {
    // On the game thread (the drain is inline while GL is live) the pass is the
    // renderer's front half -- booked as `gifdrain` in the tick census.
    ps2r::ee::TickProfile::Scope profile(ps2r::ee::TickProfile::GifDrain);
    if (inbox_slots_.load(std::memory_order_relaxed) != 0) collectReady();
    if (queue_.empty()) return;
    bool had_path3 = false;
    for (size_t i = 0; i < queue_.size(); ++i) {
        GifArbiterPacket& pkt = queue_[i];
        if (drain_observer_) drain_observer_(drain_observer_user_, pkt);
        if (dispatchBoundaryInline(pkt)) continue;
        if (pkt.ordered) {
            pkt.ordered();
            continue;
        }
        bool lowered = false;
        if (pkt.direct) {
            lowered = parser_.CanAcceptDirect();
            pkt.direct(lowered);
        }
        if (!lowered && !pkt.data.empty()) parser_.ReceiveData(pkt.path, pkt.data.data(), pkt.data.size());
        if (pkt.path == GifPath::Path3) had_path3 = true;
    }
    if (had_path3) parser_.FinishPath3();
    uint64_t freed = 0;
    for (GifArbiterPacket& pkt : queue_) {
        freed += GifArbPayloadBytes(pkt);
        recycleBuffer(std::move(pkt.data));
    }
    pending_bytes_.fetch_sub(freed, std::memory_order_relaxed);
    drained_packets_.fetch_add(queue_.size(), std::memory_order_relaxed);
    queue_.clear();
}

bool GifArbiter::hasPending() const {
    if (DrainThreadRunning() && !OnDrainThread()) {
        // queue_ is drain-thread-owned: answer from the ledger and the mid-pass flag.
        return PendingSlots() != 0 || PendingBytes() != 0 || drain_thread_->Working();
    }
    if (!queue_.empty()) return true;
    if (inbox_slots_.load(std::memory_order_relaxed) == 0) return false;
    std::lock_guard<std::mutex> lock(mtvu_mtx_);
    return !inbox_.empty();
}

void GifArbiter::clear() {
    uint64_t freed = 0;
    for (const GifArbiterPacket& pkt : queue_) freed += GifArbPayloadBytes(pkt);
    queue_.clear();
    bnd_feed_noted_ = false;
    std::lock_guard<std::mutex> lock(mtvu_mtx_);
    for (InboxSlot& s : inbox_) {
        if (s.marker) {
            if (s.marker->sealed) {
                for (const GifArbiterPacket& p : s.marker->filled) freed += GifArbPayloadBytes(p);
            }
            s.marker->detached = true;
        } else {
            freed += GifArbPayloadBytes(s.pkt);
        }
    }
    inbox_slots_.fetch_sub(inbox_.size(), std::memory_order_relaxed);
    if (freed) pending_bytes_.fetch_sub(freed, std::memory_order_relaxed);
    inbox_.clear();
    sealed_ready_.store(0, std::memory_order_relaxed);
}
