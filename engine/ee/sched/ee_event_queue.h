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
// Enqueue also POKES registered kernel-object parks (empty-lock + notify_all
// on the object's own cv). WHICH parks: while a thread holds the token (a
// running thread, the pre-dispatch's pipeline spinner, the idle runner) only
// the HOLDER's own parks are poked -- it applies the event at its next
// scheduling point, and waking the other blocked threads would make them
// READY with their condition unsatisfied: the holder then defers the
// interrupt to them and the handler runs on whichever woken thread wins the
// election -- a host race in the guest's ISR context (a drain thread's FINISH
// landed on four different thread stacks). With no holder (a fully parked
// guest set) exactly ONE park is poked -- the LAST RUNNER's (the thread that
// released the token last: inline, the one whose kernel wait would have run
// the device pipeline and taken the interrupt), else the lowest guest tid
// with a park -- so the thread that wakes to drain is a function of the
// guest's schedule, not of which host thread the kernel scheduled first; it
// re-elects through the run token, drains, and re-parks if its own condition
// is still unsatisfied.
//
// kDeferredLatched: a device stage that latches a cause OUTSIDE the queue
// (Intc::RaiseDeferred, collected only at the holder's observation points)
// enqueues this no-op so the poke above reaches a fully parked guest set the
// usual way -- the woken thread's post-acquire drain applies the no-op (the
// queue runs empty, nothing spins on HasPending) and its re-park's idle grant
// is the observation point that collects the cause.
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
            kDeferredLatched,   // no-op wake (header comment)
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
    // object lock; unregister AFTER releasing it. The registering thread's
    // guest tid is recorded (the holder-only poke above).
    int RegisterPark(std::mutex* m, std::condition_variable* cv);
    // Census: pokes with no holder (one park: the last runner's) / to the holder's parks only.
    uint64_t PokesAll() const { return pokes_all_.load(std::memory_order_relaxed); }
    uint64_t PokesHolder() const { return pokes_holder_.load(std::memory_order_relaxed); }
    // Which guest tid the last no-holder poke went to (kNone when no park was
    // registered); tests / diagnostics.
    int LastIdlePokeTid() const { return last_idle_poke_tid_.load(std::memory_order_relaxed); }
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
        int tid = -1;
        bool active = false;
    };
    std::mutex park_mu_;
    ParkSlot parks_[kMaxParks];
    std::atomic<uint64_t> pokes_all_{0}, pokes_holder_{0};
    std::atomic<int> last_idle_poke_tid_{-1};

    void PokeParks();
};
