#pragma once
// EeEventQueue: queued guest-visible wakes from foreign host threads.
//
// Rule: a foreign host thread (tick source, graphics ack, IOP dispatcher,
// audio) must NEVER mutate guest EE kernel state directly. Its wake becomes a
// QUEUED event, applied in FIFO order by whichever guest thread holds the run
// token at its next scheduling point -- the interrupt-at-syscall analog.
//
// The drain invariant has TWO halves (docs/bugs BUG-035), both here:
//  (a) HOLDER-ONLY DRAIN: only the run-token holder drains (at every token
//      acquisition through the post-acquire hook, and at the boundary step).
//      A non-holder request is a counted no-op; the events stay queued and the
//      park pokes guarantee a holder exists to deliver them.
//  (b) ROUTING PREDICATE: every guest-state MUTATOR (thread wake / priority /
//      state changes, SignalSema, SetEventFlag, INTC raise) decides "mutate in
//      place or enqueue" through ONE predicate -- CallerMayMutate(): the
//      caller holds the token, or is the holder's drain applying an entry
//      (ApplyingOnThisThread()). Any other caller enqueues. RouteIfForeign is
//      the single helper every mutator calls; there are no per-site copies, so
//      an application can never re-route its own event back into the queue
//      (the pre-fix unbounded spin).
//
// Enqueue also POKES every registered kernel-object park (empty-lock +
// notify_all on the object's own cv) so a fully parked guest set wakes to
// drain: the woken thread re-elects through the run token, drains, and
// re-parks if its own condition is still unsatisfied.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

class EeEventQueue {
public:
    struct Event {
        enum Kind : uint8_t {
            kSignalSema,
            kSetEventFlag,
            kClearEventFlag,
            kWakeupThread,
            kReleaseWaitThread,
            kResumeThread,
            kIntcRaise,
        };
        Kind kind = kSignalSema;
        int32_t a = 0;   // sid / eid / tid / intc cause
        uint32_t b = 0;  // event-flag bits
    };
    using Applier = std::function<void(const Event&)>;

    static EeEventQueue& Instance();

    // --- the ONE routing predicate (half b) ---------------------------------
    // True when the calling thread may mutate guest kernel state in place.
    static bool CallerMayMutate();
    // True while THIS thread is inside DrainOnToken applying events.
    static bool ApplyingOnThisThread();
    // Every mutator's first line: enqueue when the caller may not mutate.
    // Returns true when the event was queued (the mutator returns KE_OK).
    bool RouteIfForeign(const Event& ev);

    // Any thread: push + poke registered parks. A full ring drops the NEW
    // event (bounded queues never reorder); every drop is counted -- a nonzero
    // count is a lost guest wake and a construction finding.
    void Enqueue(const Event& ev);
    uint64_t RingFullDrops() const { return ring_full_drops_.load(std::memory_order_relaxed); }

    // One relaxed load; safe inside cv predicates.
    bool HasPending() const { return pending_.load(std::memory_order_acquire); }

    // Caller must hold the run token (DrainIfPendingOnHolder enforces it).
    // FIFO-applies every queued event through the installed applier with
    // ApplyingOnThisThread() set for the whole drain.
    void DrainOnToken();

    // Half (a): drain only when the calling thread holds the token; otherwise
    // count the refusal and leave the queue untouched.
    void DrainIfPendingOnHolder();
    uint64_t NonHolderDrainRequests() const;

    // The guest kernel installs how events are applied (tests install a model).
    void SetApplier(Applier fn);

    // Kernel-object park registry (poke targets). Register BEFORE taking the
    // object lock; unregister AFTER releasing it.
    int RegisterPark(std::mutex* m, std::condition_variable* cv);
    void UnregisterPark(int slot);

    size_t Size();

    // RAII park registration for a kernel-object wait.
    class ParkReg {
    public:
        ParkReg(std::mutex* m, std::condition_variable* cv);
        ~ParkReg();
        ParkReg(const ParkReg&) = delete;
        ParkReg& operator=(const ParkReg&) = delete;

    private:
        int slot_ = -1;
    };

private:
    EeEventQueue();

    static constexpr int kMaxEvents = 256;
    static constexpr int kMaxParks = 64;

    std::mutex mu_;
    Event ring_[kMaxEvents];
    int head_ = 0, count_ = 0;
    std::atomic<bool> pending_{false};
    std::atomic<uint64_t> nonholder_drains_{0};
    std::atomic<uint64_t> ring_full_drops_{0};
    Applier applier_;
    std::mutex applier_mu_;

    struct ParkSlot {
        std::mutex* m = nullptr;
        std::condition_variable* cv = nullptr;
        bool active = false;
    };
    std::mutex park_mu_;
    ParkSlot parks_[kMaxParks];

    void PokeParks();
};
