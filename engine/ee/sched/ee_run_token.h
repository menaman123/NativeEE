#pragma once
// EeRunToken: exactly ONE guest EE thread executes guest code at any instant.
//
// Guest threads are hosted on their own host threads (large stacks for the
// recompiled dispatch chains, exception unwinding for ExitThread), but the PS2
// has one EE and one instruction stream: the BIOS thread scheduler chooses who
// runs. The token models that choice. It changes hands ONLY at PS2 kernel
// scheduling points:
//   * blocking syscalls (WaitSema / SleepThread / WaitEventFlag / suspend),
//   * the dispatch edge of a readying syscall (SignalSema / WakeupThread /
//     SetEventFlag / StartThread / ReleaseWaitThread / ResumeThread /
//     ChangeThreadPriority / RotateThreadReadyQueue) -- the preemption point,
//   * hardware waits (frame-boundary park, tick wait) -- ParkScope,
//   * a savestate relaunch (BeginRelaunch / EndRelaunch).
//
// Election is the thbase 1.02 (SDK 3.0.1) BIOS scheduler rule:
//   "The scheduler always runs the highest-priority READY thread (priority 0
//    highest .. 127 lowest); threads of equal priority run in FIFO order of
//    becoming READY. A running thread keeps the CPU until it blocks, exits, or
//    a higher-priority thread becomes READY (preemption at kernel dispatch
//    points); there is NO time-slicing. RotateThreadReadyQueue moves the head
//    of that priority's ready queue to the tail; ChangeThreadPriority moves
//    the thread to the TAIL of the new priority's ready queue."
// READY-set membership and the FIFO position (seq) are assigned at the KERNEL
// EVENT that readies a thread (MakeReady), never at host-thread wakeup, so the
// election is a pure function of the guest event stream even when host wakeups
// lag behind.
//
// Relaunch handshake (savestate load, see docs/bugs/BUG-036): the loading
// thread relaunches every guest thread -- including its OWN tid -- on fresh
// host threads while it still holds the token. BeginRelaunch releases the
// caller's identity and HOLDS elections; the loader MakeReady()s the resumed
// threads; EndRelaunch runs ONE election over the READY set. The first
// post-load runner is therefore the thbase winner, never the first host
// thread to arrive, and the old incarnation's later GuestExit is a no-op
// because only the host thread that ENTERED a tid may exit it.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

class EeRunToken {
public:
    static EeRunToken& Instance();

    static constexpr int kNone = -1;
    static constexpr int kHostTid = -2;   // HostGate pseudo-thread
    static constexpr int kHostPrio = -1;  // ahead of every guest priority

    // Monotonic kernel-event sequence shared with the kernel-object wait queues.
    static uint64_t NextSeq();

    // Runs after every successful acquire (the event-queue drain point).
    void SetPostAcquireHook(void (*fn)());

    // Guest worker lifecycle: Enter joins the ready set (keeping a seq already
    // assigned by StartThread's MakeReady) and blocks until elected; Exit
    // leaves the ready set and releases the token if held. Exit acts only on
    // the host thread that entered `tid`.
    void GuestEnter(int tid, int prio);
    void GuestExit(int tid);

    // Kernel blocking wait: BlockBegin leaves the ready set and releases
    // (WAITING is not READY); BlockEnd rejoins (fresh seq unless a mutator
    // already re-readied us) and blocks until elected.
    void BlockBegin(int tid);
    void BlockEnd(int tid);

    // Hardware/pacing park of the calling guest thread; depth counted, only
    // the outermost pair acts. The thread rejoins at the TAIL of its priority.
    void ParkBegin();
    void ParkEnd();

    // Mark tid READY (idempotent). THIS assigns the FIFO seq.
    // A tid whose host thread has EXITED (GuestExit ran, no GuestEnter since)
    // is RETIRED: readying it is a counted no-op, because a READY slot that no
    // host thread will ever attach would be elected and nothing could re-elect
    // past it (BUG-040). The next GuestEnter of that tid (StartThread of a
    // reused thread id spawns a fresh host thread) attaches and un-retires it.
    void MakeReady(int tid, int prio);
    uint64_t RetiredReadyNoops();

    // Preemption point: if a strictly-higher-priority thread is READY, hand
    // over (keeping our seq: a preempted thread resumes at the head of its
    // priority) and block until re-elected. Self-identified.
    void PreemptPoint();

    // RotateThreadReadyQueue(prio): rotating our own priority yields to the
    // next same-priority READY thread (self to tail); another priority moves
    // that queue's head to its tail. Self-identified.
    void Rotate(int rot_prio);

    // ChangeThreadPriority: tail of the NEW priority's ready queue if READY.
    void SetPriority(int tid, int prio);

    // Remove tid from the ready set (terminate/delete of a READY thread).
    void Drop(int tid);

    // Shutdown: every acquire returns at once; wakes all waiters.
    void Shutdown();

    // Savestate relaunch handoff (header comment).
    void BeginRelaunch();
    void EndRelaunch();

    // True iff the CALLING host thread holds the token.
    bool CurrentThreadHoldsToken() const;
    // Capture needs a real active grant, not the synthetic acquire that
    // shutdown uses to let blocked workers unwind.
    bool CanCaptureCurrentThread();
    // The guest tid bound to the calling host thread (kNone / kHostTid).
    int CurrentTid() const;
    bool ElectionHeld();

    // Introspection (tests / diagnostics).
    int OwnerForTest();
    bool AnyReadyBesides(int tid);
    // Test-only: drop every ready slot and clear shutdown/hold. Callers must
    // guarantee no guest thread is live.
    void ResetForTest();

    // Scoped token possession for a HOST thread that must execute guest-visible
    // work (RPC end-callbacks, maintenance duties): it joins the election at
    // kHostPrio and runs when the scheduler hands it the token.
    class HostGate {
    public:
        HostGate();
        ~HostGate();
        HostGate(const HostGate&) = delete;
        HostGate& operator=(const HostGate&) = delete;
    };

    // RAII hardware/pacing park (ParkBegin/ParkEnd) for the calling guest
    // thread; no-op on host threads. Construct BEFORE any lock in the scope.
    class ParkScope {
    public:
        ParkScope();
        ~ParkScope();
        ParkScope(const ParkScope&) = delete;
        ParkScope& operator=(const ParkScope&) = delete;

    private:
        bool armed_ = false;
    };

private:
    friend class ThreadManager;
    EeRunToken() = default;
    // ThreadManager checks stopped execution, joined workers and no live
    // contexts first. Token reset additionally requires zero attached owners.
    bool RestartAfterJoin(std::string* error);

    struct Ready {
        int tid = kNone;
        int prio = 0;
        uint64_t seq = 0;
        bool valid = false;
    };
    static constexpr int kMaxReady = 64;
    static constexpr int kMaxTid = 256;  // retirement is tracked for guest tids 0..255

    std::mutex mu_;
    std::condition_variable cv_;
    Ready ready_[kMaxReady];
    bool retired_[kMaxTid] = {};  // GuestExit ran for the tid; cleared by GuestEnter
    uint64_t retired_ready_noops_ = 0;
    int owner_ = kNone;           // elected tid (may not have arrived yet)
    bool owner_running_ = false;  // elected thread is inside its grant
    bool shutdown_ = false;
    bool hold_election_ = false;  // BeginRelaunch .. EndRelaunch
    uint32_t attached_guests_ = 0, attached_hosts_ = 0;
    std::mutex host_gate_mu_;     // serialises concurrent HostGate users
    void (*post_acquire_)() = nullptr;

    Ready* SlotFor(int tid);
    Ready* FindReady(int tid);
    bool IsRetired(int tid) const;
    void SetRetired(int tid, bool retired);
    void ElectLocked();
    void ReleaseLocked(int tid);
    void AcquireSlow(int tid);
    void RunPostAcquire();
};
