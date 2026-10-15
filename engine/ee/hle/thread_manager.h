#pragma once
// ThreadManager: the EE BIOS thread scheduler (thbase 1.02, SDK 3.0.1) over the
// run token. One host thread per guest thread; the host threads park on their
// ThreadInfo's own mutex+cv, and ONLY the run-token holder executes guest code.
//
// thbase 1.02 rules implemented here (quoted as the kernel documents them):
//   R1 "128 priority levels, 0 highest .. 127 lowest; the scheduler runs the
//       highest-priority READY thread; equal priorities run FIFO in the order
//       they became READY; no time-slicing."           -> EeRunToken election
//   R2 "A running thread keeps the CPU until it blocks, exits, or a higher-
//       priority thread becomes READY at a kernel dispatch point."
//                                                     -> PreemptPoint after every readying syscall
//   R3 "StartThread makes a DORMANT thread READY (KE_NOT_DORMANT otherwise);
//       it may preempt the caller."                    -> StartThread
//   R4 "WakeupThread on a thread sleeping in SleepThread readies it; on any
//       other non-dormant thread it BANKS the wakeup (wakeupCount++) so a
//       later SleepThread returns at once. A self-wakeup therefore banks; it
//       is never rejected."  (the lost-wakeup rule)   -> WakeupThread / SleepThread
//   R5 "CancelWakeupThread returns and clears the banked count."
//   R6 "SuspendThread adds to a suspend count; a WAITING thread becomes
//       WAITSUSPEND, otherwise SUSPEND. ResumeThread decrements; at zero a
//       thread with an outstanding wait returns to WAIT, else READY."
//   R7 "ReleaseWaitThread forces a WAIT/WAITSUSPEND thread out of its wait
//       with KE_RELEASE_WAIT and removes it from the object's wait queue;
//       a non-waiting target returns KE_NOT_WAIT."
//   R8 "ChangeThreadPriority moves the thread to the TAIL of the new priority's
//       ready queue and reschedules; RotateThreadReadyQueue moves the head of
//       that priority's ready queue to its tail."
//   R9 "TerminateThread forces a thread DORMANT (KE_DORMANT if already);
//       DeleteThread requires DORMANT (KE_NOT_DORMANT otherwise) and never
//       deletes the caller (KE_ILLEGAL_THID). ExitThread ends the caller."
//   R10 "Thread ids 1..255; id 1 is the main thread; a created thread's
//        priority 0 is clamped to 1 and >127 to 127."
// The i-variants (iWakeupThread, iResumeThread, ...) share their bodies: the
// interrupt-context distinction is the routing predicate's (EeEventQueue) job.
//
// Foreign callers (a host thread that neither holds the run token nor is the
// holder's drain) never mutate here: every wake-type mutator's first line is
// EeEventQueue::RouteIfForeign, the ONE routing predicate.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/kernel_codes.h"
#include "engine/platform/host/platform.h"

// Decoded ThreadParam (the syscall layer reads the guest struct).
struct ThreadParam {
    uint32_t attr = 0;
    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t stack_size = 0;
    uint32_t gp = 0;
    int priority = 1;
    uint32_t option = 0;
};

// Guest-visible ReferThreadStatus record (12 words, written by the syscall layer).
struct ThreadStatus {
    int status = 0;
    uint32_t func = 0;
    uint32_t stack = 0;
    int stack_size = 0;
    uint32_t gp_reg = 0;
    int initial_priority = 0;
    int current_priority = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t wait_type = 0;
    uint32_t wait_id = 0;
    uint32_t wakeup_count = 0;
};

// Thrown on a guest host thread to unwind its recompiled call stack
// (ExitThread / ExitDeleteThread / TerminateThread(self) / termination).
struct ThreadExitUnwind {};

struct ThreadInfo {
    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t stack_size = 0;
    uint32_t gp = 0;
    uint32_t priority = 0;  // initial
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t arg = 0;
    // Per-thread heap window the kernel's InitHeap bookkeeping recorded
    // (THRD record fields; the boot path fills them for the main thread).
    uint32_t heap_base = 0;
    uint32_t heap_end = 0;
    bool started = false;

    int status = THS_DORMANT;
    int wait_type = TSW_NONE;
    int wait_id = 0;
    int wakeup_count = 0;
    int current_priority = 0;
    int suspend_count = 0;

    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> force_release{false};
    std::atomic<bool> terminated{false};
    // Object-wait handoff: set by the releasing kernel call (SignalSema /
    // SetEventFlag) under the object mutex before it notifies; consumed by
    // the waiter. evf_result holds the event-flag pattern captured at release.
    std::atomic<bool> released{false};
    uint32_t evf_result = 0;

    CpuContext* ctx = nullptr;  // the worker's stack-local context while live
    // Host continuation identity, not guest state. Protected by m; never
    // serialized. A restarted worker gets a new incarnation even at one tid.
    uint64_t capture_incarnation = 0;
    uint64_t capture_revision = 0;
    std::thread::id capture_owner;
    bool capture_parked = false;
    bool capture_suspended = false;
};

class ThreadManager {
public:
    static ThreadManager& Instance();

    // The per-thread guest run loop (installed by the composition root: the
    // dispatch loop over the recompiled functions). A thread whose body
    // returns ends like ExitThread.
    using GuestBody = std::function<void(int tid, CpuContext& ctx)>;
    void SetGuestBody(GuestBody body);
    // Host stack per guest thread (default 1 GiB virtual; tests shrink it).
    void SetStackBytes(size_t bytes);
    // Observers: park/unpark of a guest thread (savestate harvest), INTC raise
    // (the queue's kIntcRaise applier; default no-op until the INTC lands).
    void SetParkObserver(void (*fn)(int tid, bool parked));
    void SetIntcRaise(void (*fn)(int cause));
    using CaptureObserver = std::function<void(int, const std::shared_ptr<ThreadInfo>&, bool parked)>;
    // One product capture owner. Configure before workers start. Detach waits
    // for an in-flight observer; callbacks must not call Attach/Detach.
    bool AttachCaptureObserver(const void* owner, CaptureObserver observer);
    void DetachCaptureObserver(const void* owner);

    // Identity of the calling host thread.
    int CurrentTid() const;
    uint64_t CurrentIncarnation() const;
    // Register the calling host thread as guest `tid` (started, RUN, prio):
    // the main thread at boot; test threads that model running guest threads.
    std::shared_ptr<ThreadInfo> AdoptCurrentThread(int tid, int prio);
    std::shared_ptr<ThreadInfo> Lookup(int tid);
    std::shared_ptr<ThreadInfo> Current();
    int PriorityOf(const std::shared_ptr<ThreadInfo>& info);

    // --- lifecycle (R3, R9, R10) ---
    int CreateThread(const ThreadParam& p);
    int DeleteThread(int tid);
    int StartThread(int tid, uint32_t arg, uint32_t caller_sp, uint32_t caller_gp);
    [[noreturn]] void ExitThread();
    [[noreturn]] void ExitDeleteThread();
    int TerminateThread(int tid);
    int ReferThreadStatus(int tid, ThreadStatus& out);

    // --- sleep / wake (R4, R5) ---
    int SleepThread();
    int WakeupThread(int tid);
    int CancelWakeupThread(int tid);

    // --- suspend / resume / release (R6, R7) ---
    int SuspendThread(int tid);
    int ResumeThread(int tid);
    int ReleaseWaitThread(int tid);

    // --- priority / dispatch (R8) ---
    int ChangeThreadPriority(int tid, int priority);
    int RotateThreadReadyQueue(int priority);
    int DisableDispatchThread();
    int EnableDispatchThread();

    // --- wait-site helpers for the kernel objects ---
    // Mark the current thread WAITING on (type, id) / back to RUN.
    void EnterObjectWait(const std::shared_ptr<ThreadInfo>& info, int wait_type, int id);
    void LeaveObjectWait(const std::shared_ptr<ThreadInfo>& info);
    void WaitWhileSuspended(const std::shared_ptr<ThreadInfo>& info);
    void ThrowIfTerminated(const std::shared_ptr<ThreadInfo>& info);
    bool Running() const { return running_.load(std::memory_order_relaxed); }
    void NotePark(int tid, bool parked);

    // Re-install the queue applier (tests that swap in a model restore it).
    void InstallEventApplier();

    // --- shutdown ---
    void TerminateAll();
    void JoinAll();
    // The bounded shutdown join (the session's close): each worker is waited on
    // its own ThreadInfo cv until FinishCurrentThread cleared `started` (then
    // joined, cheap) or the shared deadline passes -- an unfinished worker is
    // ABANDONED (detached, never joined; the process exits around it) and named
    // in *report as "tid:entry=0x..:status=N:wait=T" (comma-separated). Returns
    // the number abandoned (0 = a complete join). Only the close uses it: a
    // restart after an abandoned worker is refused by RestartAfterJoin.
    size_t JoinAll(uint64_t deadline_ms, std::string* report);
    // Adopted boot owners must release their stack-local context before it
    // dies. Normal ThreadManager workers do this in their exit epilogue.
    bool ReleaseCurrentThread(std::string* error = nullptr);
    // Supervisor only: after stopping external deliveries, terminating and
    // joining old workers, BEFORE applying the prepared restored image.
    // Refusal changes nothing. No test-only reset or implicit join is used.
    bool RestartAfterJoin(std::string* error);
    // Test-only: forget every thread; no guest thread may be live.
    void ResetForTest();

    // Savestate seams. Snapshot: every record, ascending tid. InstallTable:
    // replaces the table wholesale (info only -- no host worker is started;
    // the relaunch step re-creates workers from the restored contexts) and
    // continues tid allocation above the highest installed id. Requires
    // that no guest worker is live.
    using TableEntry = std::pair<int, std::shared_ptr<ThreadInfo>>;
    std::vector<TableEntry> SnapshotTable();
    void InstallTable(const std::vector<TableEntry>& entries);
    // Savestate relaunch: starts a host worker for the installed thread `tid`
    // whose guest context is `seed` (the parked CpuContext from the THRD
    // record) and whose run loop is `body` (the shadow-chain walk) instead of
    // the default guest body. The worker keeps the restored status/priority
    // and joins the ready set through EeRunToken::GuestEnter; the caller
    // readies it (MakeReady) inside the BeginRelaunch..EndRelaunch handshake.
    bool StartResumed(int tid, const CpuContext& seed, GuestBody body);

private:
    struct WorkerArgs;
    ThreadManager();
    void JoinHostThread(int tid);
    void RunGuestThread(WorkerArgs& wa);
    bool SpawnWorker(WorkerArgs* wa, int tid, const std::shared_ptr<ThreadInfo>& info);
    static void WorkerMain(void* arg);
    void NotifyCapture(int tid, const std::shared_ptr<ThreadInfo>& info, bool parked);
    void NoteCapturePark(int tid, bool parked, bool suspended);
    bool FinishCurrentThread(int tid, const std::shared_ptr<ThreadInfo>& info, uint64_t incarnation);

    std::mutex map_mu_;
    std::unordered_map<int, std::shared_ptr<ThreadInfo>> threads_;
    int next_tid_ = 2;

    std::mutex host_mu_;
    std::unordered_map<int, ps2r::host::NativeThread> host_threads_;

    GuestBody body_;
    std::mutex body_mu_;
    size_t stack_bytes_ = static_cast<size_t>(1024) * 1024 * 1024;
    std::atomic<bool> running_{true};
    std::atomic<void (*)(int, bool)> park_observer_{nullptr};
    std::atomic<void (*)(int)> intc_raise_{nullptr};
    std::atomic<uint64_t> next_incarnation_{1};
    std::mutex capture_observer_mu_;
    const void* capture_observer_owner_ = nullptr;
    CaptureObserver capture_observer_;
};
