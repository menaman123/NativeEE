// engine/iop/iop_kernel.h -- the cooperative IOP scheduler. The IOP is native
// code (recompiled title IRX + HLE Sony modules) under one small cooperative
// kernel with thbase / thsemap / thevent / thmsgbx semantics: guest threads are
// coroutines run by one dispatcher at a time and park exactly where the real
// single-core kernel switches (SleepThread / DelayThread / WaitSema /
// WaitEventFlag / ReceiveMbx / the RPC marshal). Cross-host communication is an
// injection queue (memcpy + wakeup), never CPU emulation.
//
// Dispatch() applies injections, fires due events/timers, then runs Ready
// threads by priority (lower number = higher priority; FIFO among equals)
// until none is Ready, and reports the earliest future deadline. Deferred
// events obey the counted-queue contract: an event scheduled during pass k
// fires at pass k+1 at the earliest -- never inside the issuing pass.
//
// Time comes exclusively from IopClock (Frame = EE-frame-derived virtual time,
// the proof path; Realtime = steady_clock). Engine-pure: guest glue (register
// files, IOP RAM, diagnostics) is injected through hooks.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include "engine/iop/iop_clock.h"
#include "engine/iop/iop_coroutine.h"

namespace ps2r::iop {

class IopKernel {
public:
    enum class State : uint8_t { Dormant, Ready, Run, Wait };
    enum class WaitKind : uint8_t { None, Sleep, Delay, Sema, EventFlag, Mbx, Rpc };

    static constexpr int kMaxThreads = 32;   // guest tid space 1..31 (the IOPM section's kThreads)
    static constexpr int kMaxSemas = 64;     // sid space (the IOPM section's kSemas)
    static constexpr size_t kThreadStackBytes = 1024u * 1024u;   // host stack per guest thread

    using ThreadBody = std::function<void()>;
    // iopsched trace: (event, a, b, t_us) -- installed by the diag category only.
    using TraceHook = std::function<void(const char* evt, int64_t a, int64_t b, uint64_t t_us)>;

    IopKernel();
    ~IopKernel();
    IopKernel(const IopKernel&) = delete;
    IopKernel& operator=(const IopKernel&) = delete;

    IopClock& Clock() { return clock_; }
    const IopClock& Clock() const { return clock_; }

    // -- hooks (installed once by the composition root / diag category) --------
    void SetThreadRunHooks(std::function<void(int)> pre, std::function<void(int)> post);
    void SetWakeHook(std::function<void()> hook);   // an injection arrived (Realtime poke)
    void SetTraceHook(TraceHook hook);

    // -- threads (thbase) --------------------------------------------------------
    bool RegisterThread(int tid, uint32_t priority);
    void UnregisterThread(int tid);
    bool StartThread(int tid, ThreadBody body);   // arm the body, make Ready
    void ExitThread();                            // from the running thread
    void TerminateThread(int tid);
    // thbase semantics: tid 0 = self, priority 0 = caller's priority.
    // Requeue Ready / priority-ordered waiters and reschedule at this call.
    int ChangePriority(int tid, uint32_t priority);
    void SleepThread();                           // from the running thread
    int WakeupThread(int tid);                    // from kernel context
    void iWakeupThread(int tid);                  // from any host thread (queued)
    void iWakeupIfRpcWait(int tid);               // wake only a Wait::Rpc parkee
    void DelayUs(uint64_t usec);                  // from the running thread
    void Yield();                                 // from the running thread
    bool WaitRpcUs(uint64_t timeout_usec);        // park in Wait::Rpc; true if woken
    int CurrentTid() const;
    bool OnKernelThread() const;   // caller is this kernel's running coroutine
    uint32_t ThreadPriority(int tid) const;

    // -- semaphores (thsemap) ----------------------------------------------------
    bool RegisterSema(int sid, int32_t init, int32_t max_count, bool prio_order = false);
    int DeleteSema(int sid);
    int SignalSema(int sid);                      // from kernel context
    void InjectSignalSema(int sid);               // from any host thread (queued)
    uint64_t InjectedSignalErrors() const { return injected_signal_errors_.load(std::memory_order_relaxed); }
    uint64_t InjectedWakeErrors() const { return injected_wake_errors_.load(std::memory_order_relaxed); }
    int WaitSema(int sid);                        // from the running thread
    int32_t PollSema(int sid);                    // 0 = taken, -1 = would block
    int32_t SemaCount(int sid) const;
    void SetSemaCount(int sid, int32_t count);    // savestate restore
    bool SemaActive(int sid) const;
    void ReconcileSemaWaiters();                  // after restore: counts -> FIFO waiters

    // -- event flags (thevent) ---------------------------------------------------
    int  CreateEventFlag(uint32_t init_pattern, bool multi_waiters);
    int  WaitEventFlag(int ef, uint32_t pattern, uint32_t mode);   // mode: bit0 OR, bit4 clear
    int  SetEventFlag(int ef, uint32_t pattern);
    int  ClearEventFlag(int ef, uint32_t keep_mask);
    uint32_t EventFlagValue(int ef) const;

    // -- message boxes (thmsgbx) -------------------------------------------------
    int  CreateMbx();
    int  SendMbx(int mbx, void* msg);
    int  PollMbx(void** out, int mbx);            // 0 = got, -1 = empty
    int  ReceiveMbx(void** out, int mbx);         // blocks (Wait::Mbx)

    // -- deferred events + periodic timers (kernel-clock dues) -------------------
    void ScheduleEventUs(uint64_t due_us, std::function<void()> fire);
    int  StartPeriodicTimerUs(uint64_t period_us, std::function<bool()> beat);   // beat: rearm?  first due = now + period
    // The same beat with an explicit first due (kernel-clock microseconds; a due
    // at or below now fires in the next Dispatch pass), then every period.
    int  StartPeriodicTimerAtUs(uint64_t first_due_us, uint64_t period_us, std::function<bool()> beat);
    void StopTimer(int id);
    // The next due (kernel-clock microseconds) of a running timer; nullopt when
    // unknown / stopped / still queued as an injection (a savestate section's phase).
    std::optional<uint64_t> TimerDueUs(int id) const;

    // -- dispatch ----------------------------------------------------------------
    struct DispatchResult {
        bool ran_something = false;
        std::optional<uint64_t> next_due_us;      // earliest future deadline
    };
    DispatchResult Dispatch();
    void WithDispatchLock(const std::function<void()>& fn);   // savestate vs. a dispatch pass
    uint64_t DispatchGeneration() const { return dispatch_gen_.load(std::memory_order_acquire); }

    // -- census / savestate reconciliation -----------------------------------------
    int UnparkedCount() const { return unparked_.load(std::memory_order_acquire); }
    // Periodic-timer beats fired so far (every running timer, every pass): the
    // run.meta census that proves a restored heartbeat beats.
    uint64_t TimerBeats() const { return timer_beats_; }
    State ThreadState(int tid) const;
    WaitKind ThreadWaitKind(int tid) const;
    bool ThreadRegistered(int tid) const;
    uint32_t ThreadInitialPriority(int tid) const;   // the priority RegisterThread gave (StartThread's reset value)
    int ThreadWakeupPending(int tid) const;          // banked WakeupThread tokens
    void SetThreadWakeupPending(int tid, int tokens);
    void ReconcileThreadFromFlags(int tid, bool sleeping, bool terminated);
    // -- the object tables as a savestate section reads / rebuilds them --------------
    int32_t SemaMaxCount(int sid) const;
    bool SemaPrioOrder(int sid) const;
    int EventFlagCount() const { return static_cast<int>(event_flags_.size()); }
    bool EventFlagActive(int ef) const;
    bool EventFlagMulti(int ef) const;
    // Installs event flag `ef` (the table grows to hold it) with no waiters.
    void RestoreEventFlag(int ef, bool active, bool multi, uint32_t value);
    int MbxCount() const { return static_cast<int>(mbxs_.size()); }
    bool MbxActive(int mbx) const;
    std::vector<void*> MbxMessages(int mbx) const;   // queued tokens, FIFO order
    // Installs message box `mbx` (the table grows to hold it) with the queued tokens and no waiters.
    void RestoreMbx(int mbx, bool active, const std::vector<void*>& messages);
    // Savestate restore of a whole kernel: every thread terminated and unregistered
    // (their coroutines destroyed -- never the running one), every semaphore deleted,
    // the event-flag / message-box tables emptied, every timer and deferred event
    // dropped, the injection queue cleared. The clock and the id counters stay.
    void ResetForRestore();
    // Savestate restore: queued deferred events are in-flight hardware ops of the
    // pre-load world and are dropped (periodic timers stay armed).
    void DropPendingEvents();
    // Deferred events still queued (the drive completions in flight on the IOP side).
    size_t PendingEventCount() const { return events_.size(); }
    // The earliest future deadline over time-parked threads, queued events and
    // running timers (none = the IOP is idle without a due): the pump points use
    // it to decide whether work is due inside the current frame.
    std::optional<uint64_t> NextDueUs() const;

    void Shutdown();
    bool IsShutdown() const { return shutdown_.load(std::memory_order_acquire); }

private:
    struct Thread {
        int tid = 0;
        uint32_t priority = 0x7F;
        uint32_t initial_priority = 0x7F;
        State state = State::Dormant;
        WaitKind wait = WaitKind::None;
        uint64_t wait_due_us = 0;                 // Delay / Rpc deadline
        bool rpc_woken = false;
        int wait_result = 0;
        int wakeup_pending = 0;                   // banked WakeupThread tokens
        uint64_t ready_seq = 0;                   // FIFO order among equal priorities
        ThreadBody body;
        IopCoroutine coro;
        bool registered = false;
        IopKernel* owner = nullptr;
    };
    struct Sema {
        bool active = false;
        int32_t count = 0;
        int32_t max_count = 0;
        bool prio_order = false;                  // FIFO (default) vs priority wake order
        std::list<Thread*> waiters;
    };
    struct EventFlagRec {
        bool active = false;
        bool multi = false;
        uint32_t value = 0;
        struct Waiter { Thread* t; uint32_t pattern; uint32_t mode; };
        std::list<Waiter> waiters;
    };
    struct MbxRec {
        bool active = false;
        std::queue<void*> messages;
        std::list<Thread*> waiters;
    };
    struct Event {
        uint64_t due_us = 0;
        uint64_t seq = 0;                         // FIFO among equal dues
        uint64_t gen = 0;                         // dispatch generation at schedule time
        std::function<void()> fire;
    };
    struct Timer {
        int id = -1;
        bool running = false;
        uint64_t period_us = 0;
        uint64_t due_us = 0;
        std::function<bool()> beat;
    };

    static void FiberTrampoline(void* param);
    void RunThread(Thread* t);
    void LeaveThread();                           // park: switch to the dispatcher
    void MakeReady(Thread* t);
    void SetWaiting(Thread* t, WaitKind k);
    void ClearFromWaitLists(Thread* t);
    Thread* SchedNext();
    void UpdateDeadlines();
    void ApplyInjections();
    bool FireDueEventsAndTimers();
    void PokeWakeHook();
    void Trace(const char* evt, int64_t a, int64_t b);
    static bool EventFlagMatch(uint32_t value, uint32_t pattern, uint32_t mode);
    void CompleteEventFlagWaiters(EventFlagRec& ef);

    IopClock clock_;
    Thread threads_[kMaxThreads];
    Sema semas_[kMaxSemas];
    std::vector<EventFlagRec> event_flags_;
    std::vector<MbxRec> mbxs_;
    std::vector<Event> events_;
    std::vector<Timer> timers_;
    uint64_t event_seq_ = 1;
    uint64_t timer_beats_ = 0;
    uint64_t ready_seq_ = 1;
    std::atomic<uint64_t> dispatch_gen_{0};
    std::atomic<int> next_timer_id_{0};

    Thread* current_ = nullptr;
    void* dispatch_ctx_ = nullptr;
    std::recursive_mutex dispatch_mutex_;
    std::mutex inject_mutex_;
    std::deque<std::function<void()>> injections_;
    std::atomic<uint64_t> injected_signal_errors_{0}, injected_wake_errors_{0};

    std::function<void(int)> run_pre_, run_post_;
    std::function<void()> wake_hook_;
    TraceHook trace_;

    std::atomic<int> unparked_{0};
    std::atomic<bool> shutdown_{false};
};

}  // namespace ps2r::iop
