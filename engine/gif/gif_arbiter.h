#pragma once
// The PATH1/2/3 arbiter. The three GIF paths feed ONE GS through ONE register
// file and arbitration happens only at packet boundaries, so the GS-visible
// order is the SUBMISSION order: XGKICK (PATH1) and VIF DIRECT (PATH2) submit
// at the exact point the VU1 ran inside the VIF stream, the GIF DMA chain
// (PATH3) in walk order; drain() feeds the queue to the parser in that order.
//
// Under MTVU the VIF1 spans run on a worker thread, so their PATH1/PATH2
// submits happen later in wall-clock than the EE's PATH3 submits that follow
// the chain kick. The construction that keeps the delivered order identical
// for every interleaving: (1) the EE opens an order MARKER in the inbox at the
// position the span's packets would have had; (2) the worker's submits fill
// that marker in production order; (3) the worker seals it when the span
// completes; (4) EE-side submits (PATH3, boundary slots) append plain slots at
// the inbox tail; (5) the drain owner promotes the READY PREFIX of the inbox
// (plain slots and sealed markers, stopping at the first unsealed marker) and
// never blocks. A frame boundary is a marker-less slot with empty data that
// the drain hands to the boundary hook in stream position.
//
// Cross-thread contract (single drain owner: the EE thread, or the drain
// thread while it runs -- drain() turns every foreign call into a kick):
//   queue_          drain-owner only, never under a lock
//   inbox_, marker sealed/detached, sealed_ready_ writes   under mtvu_mtx_
//   marker->filled  lock-free by the ONE worker until sealed; read after sealed under mtvu_mtx_
//   pending_bytes_, inbox_slots_   relaxed atomics, any thread; 0 when the pipeline is empty
//   sealed_ready_ read   relaxed hint (a stale value costs one no-op drain / one poll)
//   bnd_feed_noted_ EE thread only (the frame-boundary HLE)
//   boundary_hook_  set at construction; called by the drain owner only
//   pool_/pool_mtx_ leaf lock, any thread
//   path1_pool_/path1_pool_mtx_   leaf lock, any thread (the drain owner recycles a pass's PATH1
//                                 buffers in one batch; the VU1 unit's XgKick takes them in batches)
//   owner_, drain_thread_   set before producer traffic, cleared after it stops
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "engine/gif/gif_mtvu_marker.h"
#include "engine/gif/gif_tag.h"

class GifBoundaryHook;
class GifDrainThread;
class GifMarkerOwner;
class GifParser;

// Boundary census (relaxed counters): slots enqueued, slots delivered to the
// hook, and boundaries opened without their frame's feed note (tripwire).
struct GifBoundaryStats {
    uint64_t enqueued = 0;
    uint64_t delivered = 0;
    uint64_t order_violations = 0;
};

class GifArbiter {
public:
    static constexpr size_t kPoolMax = 4;
    // Staging buffers kept by the pool have a capacity in this range.
    static constexpr size_t kPoolMinCapacity = 0x1000;
    static constexpr size_t kPoolMaxCapacity = 0x10000;
    // The PATH1 packet pool: right-sized XGKICK packet buffers (capacity <= one
    // VU1 data memory) that travel worker -> inbox -> drain and come back here
    // instead of being freed; bounded so a lagging drain cannot grow it.
    static constexpr size_t kPath1PoolMax = 2048;
    static constexpr size_t kPath1PoolMaxCapacity = 0x4000;
    // A marker's packet vector is reserved at open for the packets one Batch
    // segment of the first title produces (~60), so the worker's fills grow it
    // once at most instead of through the doubling ladder per segment.
    static constexpr size_t kMarkerReservePackets = 64;

    explicit GifArbiter(GifParser& parser, GifBoundaryHook* boundary_hook = nullptr);

    // Queue a copy of `size` bytes on `path`; payloads shorter than a GIFtag are dropped.
    void submit(GifPath path, const uint8_t* data, size_t size, bool is_directhl = false);
    // Queue an owned buffer (no copy); payloads shorter than a GIFtag are dropped.
    void submitOwned(GifPath path, std::vector<uint8_t>&& data, bool is_directhl = false);
    // Queue an owned HLE geometry delivery at its PATH1 position. The original
    // bytes remain available for observers and for a partial/masked raw stream
    // that prevents direct delivery. The callback runs on the drain owner, with
    // true for direct delivery or false for observation-only fallback.
    void submitDirectOwned(std::vector<uint8_t>&& fallback, std::function<void(bool)> deliver);
    // Library work shares ordering with retained packet producers. Callers
    // flush preceding staged input before enqueueing from its producer owner.
    // The job runs on the drain owner and must not reenter this queue.
    void submitOrdered(std::function<void()> work);

    // Take a recycled staging buffer (empty, capacity kept) or an empty vector.
    std::vector<uint8_t> acquireBuffer();
    // Return a staging-class buffer to the pool; others are freed.
    void recycleBuffer(std::vector<uint8_t>&& buffer);
    // Move up to `count` recycled PATH1 packet buffers (empty, capacity kept)
    // into `out` under one lock; fewer when the pool runs short. The drain
    // owner refills the pool with a delivered pass's PATH1 buffers
    // (recyclePath1Buffers), dropping what exceeds kPath1PoolMax.
    void acquirePath1Buffers(std::vector<std::vector<uint8_t>>& out, size_t count);
    void recyclePath1Buffers(std::vector<std::vector<uint8_t>>& buffers);
    size_t Path1PoolSize() const;

    // Deliver every ready packet to the parser in submission order (boundary
    // slots to the hook), then tell the parser a PATH3 pass finished if any
    // PATH3 packet was delivered. While the drain thread runs, a call from any
    // other thread only requests a pass.
    void drain();
    bool hasPending() const;
    // Discard every queued and inbox packet without delivering it; markers the
    // worker still owns are detached (their bytes settle at their seal).
    void clear();

    // ---- MTVU order markers (EE opens, the worker fills and seals) ----
    // EE thread: append an order marker at the current stream position.
    std::shared_ptr<GifMtvuMarker> openMarker();
    // Worker thread: every packet of the span is in `filled`.
    void sealMarker(const std::shared_ptr<GifMtvuMarker>& marker);
    // Lock-free hint for EE poll sites: a sealed marker awaits collection.
    bool hasSealedReady() const;
    // The worker (null = inline routing); set before traffic, cleared after Stop.
    void SetMarkerOwner(GifMarkerOwner* owner);

    // ---- frame-boundary command slots ----
    // EE thread: enqueue the boundary of frame `seq` (0 is mapped to 1) behind
    // every marker and packet submitted so far. Every boundary must follow its
    // frame's feed (noteFeedForBoundary); a missing note counts a violation.
    void openBoundary(uint32_t seq, uint64_t enqueue_ns);
    // EE thread: the frame's VIF1 tail was fed; consumed by the next openBoundary.
    void noteFeedForBoundary();
    // The one place a boundary slot is recognised: true and the hook fired
    // when `pkt` is a boundary; false for a data packet.
    bool dispatchBoundaryInline(GifArbiterPacket& pkt);
    GifBoundaryStats boundary_stats() const;

    // ---- drain thread coupling ----
    // Attach / detach the drain thread (null = inline drains). Set by
    // GifDrainThread::Start before producer traffic.
    void AttachDrainThread(GifDrainThread* thread);
    bool DrainThreadRunning() const;
    bool OnDrainThread() const;
    // Returns once a drain pass that started after this call completed;
    // immediate when no drain thread runs or when called from it. Rethrows a
    // fault the drain thread caught (as drain() does): the exception the inline
    // drain would have thrown on the caller reaches the caller here.
    void DrainFence();
    // The drain thread holds or is delivering packets: its parser has not yet
    // reached everything submitted (the FINISH / SIGNAL an EE waiter needs may
    // still be in flight). False with the drain inline (the caller drains).
    bool DrainInFlight() const;
    // The drain thread's completed-pass count (0 inline) and a bounded park
    // until it moves past `seen`: the EE's idle wait on the deliveries in flight.
    uint64_t DrainPassGeneration() const;
    void WaitDrainPassAfter(uint64_t seen, uint64_t timeout_ns);

    // Ledger: payload bytes accepted and not yet delivered / dropped, and
    // inbox slots (packets, markers, boundaries) not yet promoted.
    uint64_t PendingBytes() const;
    uint64_t PendingSlots() const;
    uint64_t DrainedPackets() const;
    size_t PoolSize() const;

    // Byte tap on every accepted submit (path, payload, size), called before
    // the packet is queued; null = absent. Tools record PATH1 kicks with it.
    using SubmitObserver = void (*)(void* user, GifPath path, const uint8_t* data, size_t size);
    void SetSubmitObserver(SubmitObserver fn, void* user);
    // Per-packet tap in the drain pass, boundaries included, before delivery;
    // runs on the drain owner thread. Tools record delivery order with it.
    using DrainObserver = void (*)(void* user, const GifArbiterPacket& pkt);
    void SetDrainObserver(DrainObserver fn, void* user);

private:
    friend class GifDrainThread;

    struct InboxSlot {
        GifArbiterPacket pkt;
        std::shared_ptr<GifMtvuMarker> marker;  // null = plain EE packet or boundary slot
    };

    void Enqueue(GifArbiterPacket&& pkt);
    void PushInboxSlot(GifArbiterPacket&& pkt);
    void collectReady();
    // The pass body, run by the drain owner (drain() inline, or the loop);
    // DrainPassDelivered reports whether any slot was delivered.
    void DrainPass();
    bool DrainPassDelivered();
    void SettleDeliveredQueue();
    // Producer side: a fault the drain thread caught is thrown here, once.
    void RethrowDrainFault();
    bool WorkerRunning() const;

    GifParser& parser_;
    GifBoundaryHook* boundary_hook_;
    SubmitObserver observer_ = nullptr;
    void* observer_user_ = nullptr;
    DrainObserver drain_observer_ = nullptr;
    void* drain_observer_user_ = nullptr;
    GifMarkerOwner* owner_ = nullptr;
    GifDrainThread* drain_thread_ = nullptr;
    std::vector<GifArbiterPacket> queue_;
    mutable std::mutex mtvu_mtx_;
    std::deque<InboxSlot> inbox_;
    std::atomic<uint32_t> sealed_ready_{0};
    std::atomic<uint64_t> pending_bytes_{0};
    std::atomic<uint64_t> inbox_slots_{0};
    std::atomic<uint64_t> drained_packets_{0};
    std::atomic<uint64_t> bnd_enqueued_{0};
    std::atomic<uint64_t> bnd_delivered_{0};
    std::atomic<uint64_t> bnd_order_violations_{0};
    bool bnd_feed_noted_ = false;
    mutable std::mutex pool_mtx_;
    std::vector<std::vector<uint8_t>> pool_;
    mutable std::mutex path1_pool_mtx_;
    std::vector<std::vector<uint8_t>> path1_pool_;
    std::vector<std::vector<uint8_t>> path1_freed_;  // drain owner only: the pass's PATH1 buffers
};
