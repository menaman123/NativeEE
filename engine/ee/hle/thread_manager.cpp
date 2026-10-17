// ThreadManager implementation; the thbase rules R1..R10 are in the header.
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/timing/tick_profile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <cstring>
#include <memory>

#include "engine/ee/cpu/ps2_float.h"
#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"

namespace {
thread_local int t_current_tid = 1;  // id 1 = the main thread (R10)
thread_local uint64_t t_incarnation = 0;

void ApplySuspendStatusLocked(ThreadInfo& info) {
    info.status = (info.wait_type != TSW_NONE) ? THS_WAITSUSPEND : THS_SUSPEND;
}
}  // namespace

// One host worker's arguments. A StartThread worker builds its context from
// the ThreadInfo (entry/stack/gp/arg); a StartResumed worker copies `seed`
// (the parked context) and runs `body` (the chain walk) instead of the
// default guest body.
struct ThreadManager::WorkerArgs {
    ThreadManager* mgr = nullptr;
    int tid = 0;
    std::shared_ptr<ThreadInfo> info;
    uint32_t caller_sp = 0;
    uint32_t caller_gp = 0;
    std::unique_ptr<CpuContext> seed;
    GuestBody body;
    uint64_t incarnation = 0;
};

ThreadManager& ThreadManager::Instance() {
    static ThreadManager m;
    return m;
}

ThreadManager::ThreadManager() { InstallEventApplier(); }

// The holder's drain applies queued foreign wakes through the direct bodies:
// RouteIfForeign sees ApplyingOnThisThread() and never re-enqueues.
void ThreadManager::InstallEventApplier() {
    EeEventQueue::Instance().SetApplier([this](const EeEventQueue::Event& ev) {
        using E = EeEventQueue::Event;
        switch (ev.kind) {
            case E::kSignalSema: Semaphores::Instance().Signal(ev.a); break;
            case E::kSetEventFlag: EventFlags::Instance().Set(ev.a, ev.b); break;
            case E::kClearEventFlag: EventFlags::Instance().Clear(ev.a, ev.b); break;
            case E::kWakeupThread: WakeupThread(ev.a); break;
            case E::kReleaseWaitThread: ReleaseWaitThread(ev.a); break;
            case E::kResumeThread: ResumeThread(ev.a); break;
            case E::kIntcRaise:
                if (auto* fn = intc_raise_.load(std::memory_order_acquire)) fn(ev.a);
                break;
        }
    });
}

void ThreadManager::SetGuestBody(GuestBody body) {
    std::lock_guard<std::mutex> lk(body_mu_);
    body_ = std::move(body);
}
void ThreadManager::SetStackBytes(size_t bytes) { stack_bytes_ = bytes; }
void ThreadManager::SetParkObserver(void (*fn)(int, bool)) { park_observer_.store(fn); }
void ThreadManager::SetIntcRaise(void (*fn)(int)) { intc_raise_.store(fn); }

int ThreadManager::CurrentTid() const { return t_current_tid; }
uint64_t ThreadManager::CurrentIncarnation() const { return t_incarnation; }

bool ThreadManager::AttachCaptureObserver(const void* owner, CaptureObserver observer) {
    std::lock_guard<std::mutex> lock(capture_observer_mu_);
    if (!owner || !observer || capture_observer_owner_) return false;
    capture_observer_owner_ = owner;
    capture_observer_ = std::move(observer);
    return true;
}

void ThreadManager::DetachCaptureObserver(const void* owner) {
    std::lock_guard<std::mutex> lock(capture_observer_mu_);
    if (capture_observer_owner_ != owner) return;
    capture_observer_ = {};
    capture_observer_owner_ = nullptr;
}

void ThreadManager::NotifyCapture(int tid, const std::shared_ptr<ThreadInfo>& info, bool parked) {
    std::lock_guard<std::mutex> lock(capture_observer_mu_);
    if (capture_observer_) capture_observer_(tid, info, parked);
}

std::shared_ptr<ThreadInfo> ThreadManager::AdoptCurrentThread(int tid, int prio) {
    t_current_tid = tid;
    t_incarnation = next_incarnation_.fetch_add(1);
    std::shared_ptr<ThreadInfo> info;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        auto it = threads_.find(tid);
        info = (it != threads_.end()) ? it->second : std::make_shared<ThreadInfo>();
        std::lock_guard<std::mutex> tl(info->m);
        info->started = true;
        info->status = THS_RUN;
        info->wait_type = TSW_NONE;
        info->wait_id = 0;
        info->priority = static_cast<uint32_t>(prio);
        info->current_priority = prio;
        info->capture_incarnation = t_incarnation;
        ++info->capture_revision;
        info->capture_owner = std::this_thread::get_id();
        info->capture_parked = info->capture_suspended = false;
        info->ctx = nullptr;
        threads_[tid] = info;
    }
    NotifyCapture(tid, info, false);
    return info;
}

std::shared_ptr<ThreadInfo> ThreadManager::Lookup(int tid) {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = threads_.find(tid);
    return (it != threads_.end()) ? it->second : nullptr;
}

std::shared_ptr<ThreadInfo> ThreadManager::Current() { return Lookup(t_current_tid); }

int ThreadManager::PriorityOf(const std::shared_ptr<ThreadInfo>& info) {
    if (!info) return 64;
    std::lock_guard<std::mutex> lk(info->m);
    return info->current_priority;
}

// ---- lifecycle ---------------------------------------------------------------

int ThreadManager::CreateThread(const ThreadParam& p) {
    auto info = std::make_shared<ThreadInfo>();
    info->attr = p.attr;
    info->entry = p.entry;
    info->stack = p.stack;
    info->stack_size = p.stack_size;
    info->gp = p.gp;
    info->option = p.option;
    int prio = p.priority;
    if (prio <= 0) prio = 1;  // R10 clamp
    if (prio > kEeMaxPriority) prio = kEeMaxPriority;
    info->priority = static_cast<uint32_t>(prio);
    info->current_priority = prio;

    std::lock_guard<std::mutex> lk(map_mu_);
    for (int attempts = 0; attempts < 0xFE; ++attempts) {
        if (next_tid_ < 2 || next_tid_ > 0xFF) next_tid_ = 2;
        const int candidate = next_tid_;
        next_tid_ = (next_tid_ >= 0xFF) ? 2 : next_tid_ + 1;
        if (threads_.find(candidate) == threads_.end()) {
            threads_[candidate] = info;
            return candidate;
        }
    }
    return KE_ERROR;
}

int ThreadManager::DeleteThread(int tid) {
    if (tid == 0 || tid == t_current_tid) return KE_ILLEGAL_THID;  // R9
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status != THS_DORMANT) return KE_NOT_DORMANT;
    }
    std::lock_guard<std::mutex> lk(map_mu_);
    threads_.erase(tid);
    return KE_OK;
}

void ThreadManager::WorkerMain(void* arg) {
    auto* wa = static_cast<WorkerArgs*>(arg);
    wa->mgr->RunGuestThread(*wa);
    delete wa;
}

void ThreadManager::RunGuestThread(WorkerArgs& wa) {
    const int tid = wa.tid;
    std::shared_ptr<ThreadInfo> info = wa.info;
    t_current_tid = tid;
    t_incarnation = wa.incarnation;
    // The EE FPU and VU0 macro pipeline round toward zero (the PS2's FPU is
    // not IEEE round-to-nearest); the rounding mode is per host thread, so
    // every thread that runs recompiled EE code sets it before the first op.
    Ps2Float::SetGuestThreadRounding();
    ps2r::ee::TickProfile::BindGameThread();   // this thread's hook work is the tick's (TickProfile)
    CpuContext ctx{};
    if (wa.seed) {
        ctx = *wa.seed;  // the parked context of a relaunched thread
    } else {
        std::memset(&ctx, 0, sizeof(CpuContext));
        ctx.vuRegs.VF[0].w = 1.0f;  // VF00 = {0,0,0,1}
        uint32_t sp = wa.caller_sp;
        if (info->stack) {
            const uint32_t size = info->stack_size ? info->stack_size : 0x800u;
            sp = (info->stack + size) & ~0xFu;
        }
        const uint32_t gp = info->gp ? info->gp : wa.caller_gp;
        ctx.cpuRegs.GPR.r[29].UL[0] = sp;
        ctx.cpuRegs.GPR.r[28].UL[0] = gp;
        ctx.cpuRegs.GPR.r[4].UL[0] = info->arg;
        ctx.cpuRegs.GPR.r[31].UL[0] = 0;
        ctx.cpuRegs.pc = info->entry;
    }
    int prio;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->capture_incarnation != wa.incarnation) return;
        info->ctx = &ctx;
        info->capture_owner = std::this_thread::get_id();
        info->capture_parked = info->capture_suspended = false;
        ++info->capture_revision;
        prio = info->current_priority;
    }
    NotifyCapture(tid, info, false);
    // Join the ready set (seq pre-assigned by StartThread) and block until
    // elected: guest code on this host thread runs only while holding the token.
    // The thread is READY until the election makes it the RUNning one.
    EeRunToken::Instance().GuestEnter(tid, prio);
    {
        std::lock_guard<std::mutex> lk(info->m);
        // Do not update metadata belonging to a replacement worker.
        if (info->capture_incarnation != wa.incarnation) return;
        if (info->status == THS_READY) info->status = THS_RUN;
    }
    GuestBody body = wa.body;
    if (!body) {
        std::lock_guard<std::mutex> lk(body_mu_);
        body = body_;
    }
    try {
        if (body && Running()) {
            ThrowIfTerminated(info);
            WaitWhileSuspended(info);
            body(tid, ctx);
        }
    } catch (const ThreadExitUnwind&) {
    }
    FinishCurrentThread(tid, info, wa.incarnation);
}

bool ThreadManager::FinishCurrentThread(int tid, const std::shared_ptr<ThreadInfo>& info,
                                       uint64_t incarnation) {
    if (!info) return false;
    {
        std::lock_guard<std::mutex> lk(info->m);
        // An old epilogue must never clear a later incarnation's continuation.
        if (info->capture_incarnation != incarnation || info->capture_owner != std::this_thread::get_id())
            return false;
        info->started = false;
        info->status = THS_DORMANT;
        info->wait_type = TSW_NONE;
        info->wait_id = 0;
        info->wakeup_count = 0;
        info->suspend_count = 0;
        info->force_release = false;
        info->terminated = false;
        info->released = false;
        info->ctx = nullptr;
        info->capture_owner = {};
        info->capture_parked = info->capture_suspended = false;
        ++info->capture_revision;
    }
    NotifyCapture(tid, info, false);
    if (auto* fn = park_observer_.load(std::memory_order_acquire)) fn(tid, false);
    info->cv.notify_all();
    EeRunToken::Instance().GuestExit(tid);
    t_incarnation = 0;
    return true;
}

int ThreadManager::StartThread(int tid, uint32_t arg, uint32_t caller_sp, uint32_t caller_gp) {
    if (tid == 0) return KE_ILLEGAL_THID;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    if (!Running()) return KE_ERROR;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->started || info->status != THS_DORMANT) return KE_NOT_DORMANT;  // R3
    }
    JoinHostThread(tid);  // a DORMANT thread's previous host thread has finished
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->started || info->status != THS_DORMANT) return KE_NOT_DORMANT;
        info->started = true;
        info->status = THS_READY;
        info->arg = arg;
        info->terminated = false;
        info->force_release = false;
        info->wait_type = TSW_NONE;
        info->wait_id = 0;
        info->wakeup_count = 0;
        info->suspend_count = 0;
        info->current_priority = static_cast<int>(info->priority);
        info->capture_incarnation = next_incarnation_.fetch_add(1);
        ++info->capture_revision;
        info->capture_owner = {};
        info->capture_parked = info->capture_suspended = false;
        info->ctx = nullptr;
    }
    NotifyCapture(tid, info, false);
    auto* wa = new WorkerArgs;
    wa->mgr = this; wa->tid = tid; wa->info = info; wa->caller_sp = caller_sp; wa->caller_gp = caller_gp;
    wa->incarnation = info->capture_incarnation;
    if (!SpawnWorker(wa, tid, info)) {
        std::lock_guard<std::mutex> lk(info->m);
        info->started = false;
        info->status = THS_DORMANT;
        return KE_ERROR;
    }
    // R3: READY at the syscall (deterministic FIFO seq), preemption at the edge.
    EeRunToken::Instance().MakeReady(tid, static_cast<int>(info->priority));
    EeRunToken::Instance().PreemptPoint();
    return KE_OK;
}

// Creates the host thread for `wa` (owned by the worker from here) on the
// configured stack and records it for JoinHostThread. False (wa deleted) when
// the host refuses the thread (host::ThreadCreate: pthread_create / _beginthreadex).
bool ThreadManager::SpawnWorker(WorkerArgs* wa, int tid, const std::shared_ptr<ThreadInfo>& info) {
    (void)info;
    ps2r::host::NativeThread pt;
    if (!ps2r::host::ThreadCreate(&pt, &ThreadManager::WorkerMain, wa, stack_bytes_)) {
        delete wa;
        return false;
    }
    std::lock_guard<std::mutex> hl(host_mu_);
    host_threads_[tid] = pt;
    return true;
}

bool ThreadManager::StartResumed(int tid, const CpuContext& seed, GuestBody body) {
    auto info = Lookup(tid);
    if (!info || !body || !Running()) return false;
    JoinHostThread(tid);
    auto* wa = new WorkerArgs;
    wa->mgr = this; wa->tid = tid; wa->info = info;
    wa->seed = std::make_unique<CpuContext>(seed);
    wa->body = std::move(body);
    {
        std::lock_guard<std::mutex> lock(info->m);
        info->capture_incarnation = next_incarnation_.fetch_add(1);
        ++info->capture_revision;
        info->capture_owner = {};
        info->capture_parked = info->capture_suspended = false;
        info->ctx = nullptr;
        wa->incarnation = info->capture_incarnation;
    }
    NotifyCapture(tid, info, false);
    return SpawnWorker(wa, tid, info);
}

void ThreadManager::ExitThread() {
    if (auto info = Current()) {
        {
            std::lock_guard<std::mutex> lk(info->m);
            info->terminated = true;
            info->force_release = true;
        }
        info->cv.notify_all();
    }
    throw ThreadExitUnwind{};
}

void ThreadManager::ExitDeleteThread() {
    const int tid = t_current_tid;
    if (auto info = Current()) {
        {
            std::lock_guard<std::mutex> lk(info->m);
            info->terminated = true;
            info->force_release = true;
        }
        info->cv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        threads_.erase(tid);
    }
    throw ThreadExitUnwind{};
}

int ThreadManager::TerminateThread(int tid) {
    if (tid == 0) tid = t_current_tid;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status == THS_DORMANT) return KE_DORMANT;  // R9
        info->terminated = true;
        info->force_release = true;
    }
    info->cv.notify_all();
    if (tid == t_current_tid) throw ThreadExitUnwind{};
    // The target must run its unwind: ready it, release the token across the join.
    EeRunToken::Instance().MakeReady(tid, PriorityOf(info));
    EeRunToken::Instance().BlockBegin(t_current_tid);
    {
        EeEventQueue::ParkReg reg(&info->m, &info->cv);
        std::unique_lock<std::mutex> lk(info->m);
        info->cv.wait(lk, [&] { return (!info->started && info->status == THS_DORMANT) || !Running(); });
    }
    EeRunToken::Instance().BlockEnd(t_current_tid);
    return KE_OK;
}

int ThreadManager::ReferThreadStatus(int tid, ThreadStatus& out) {
    if (tid == 0) tid = t_current_tid;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    std::lock_guard<std::mutex> lk(info->m);
    out.status = info->status;
    out.func = info->entry;
    out.stack = info->stack;
    out.stack_size = static_cast<int>(info->stack_size);
    out.gp_reg = info->gp;
    out.initial_priority = static_cast<int>(info->priority);
    out.current_priority = info->current_priority;
    out.attr = info->attr;
    out.option = info->option;
    out.wait_type = static_cast<uint32_t>(info->wait_type);
    out.wait_id = static_cast<uint32_t>(info->wait_id);
    out.wakeup_count = static_cast<uint32_t>(info->wakeup_count);
    return tid;
}

// ---- sleep / wake (R4, R5) -----------------------------------------------------

int ThreadManager::SleepThread() {
    auto info = Current();
    if (!info) return KE_UNKNOWN_THID;
    ThrowIfTerminated(info);
    const int tid = t_current_tid;
    EeEventQueue::Instance().DrainIfPendingOnHolder();  // syscall-boundary delivery
    int ret = KE_OK;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(info->m);
            if (info->wakeup_count > 0) {  // R4: a banked wakeup is consumed at once
                info->wakeup_count--;
                info->status = THS_RUN;
                break;
            }
            info->status = THS_WAIT;
            info->wait_type = TSW_SLEEP;
            info->force_release = false;
        }
        NotePark(tid, true);
        EeRunToken::Instance().BlockBegin(tid);
        IdleWhileBlocked(info);
        {
            EeEventQueue::ParkReg reg(&info->m, &info->cv);
            std::unique_lock<std::mutex> lk(info->m);
            info->cv.wait(lk, [&] {
                return info->wakeup_count > 0 || info->force_release.load() || info->terminated.load() ||
                       EeEventQueue::Instance().HasPending() || !Running();
            });
        }
        NotePark(tid, false);
        EeRunToken::Instance().BlockEnd(tid);
        if (info->terminated.load()) throw ThreadExitUnwind{};
        std::lock_guard<std::mutex> lk(info->m);
        info->status = THS_RUN;
        info->wait_type = TSW_NONE;
        if (info->force_release.load()) {
            info->force_release = false;
            ret = KE_RELEASE_WAIT;
            break;
        }
        if (info->wakeup_count > 0) {
            info->wakeup_count--;
            break;
        }
        if (!Running()) break;
        // Spurious (queue poke): re-enter the wait.
    }
    WaitWhileSuspended(info);
    return ret;
}

int ThreadManager::WakeupThread(int tid) {
    if (tid == 0) return KE_ILLEGAL_THID;
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kWakeupThread, tid, 0})) return KE_OK;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    bool woke = false;
    int prio = 64;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status == THS_DORMANT) return KE_DORMANT;
        if (info->status == THS_WAIT && info->wait_type == TSW_SLEEP) {
            info->status = (info->suspend_count > 0) ? THS_SUSPEND : THS_READY;
            info->wait_type = TSW_NONE;
            info->wakeup_count++;
            info->cv.notify_one();
            woke = true;
            prio = info->current_priority;
        } else {
            info->wakeup_count++;  // R4: bank (including a self-wakeup)
        }
    }
    if (woke) {
        EeRunToken::Instance().MakeReady(tid, prio);
        EeRunToken::Instance().PreemptPoint();
    }
    return KE_OK;
}

int ThreadManager::CancelWakeupThread(int tid) {
    if (tid == 0) tid = t_current_tid;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    std::lock_guard<std::mutex> lk(info->m);
    const int old = info->wakeup_count;
    info->wakeup_count = 0;
    return old;
}

// ---- suspend / resume / release (R6, R7) ---------------------------------------

int ThreadManager::SuspendThread(int tid) {
    if (tid == 0) tid = t_current_tid;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status == THS_DORMANT) return KE_DORMANT;
        info->suspend_count++;
        ApplySuspendStatusLocked(*info);
    }
    info->cv.notify_all();
    if (tid == t_current_tid) WaitWhileSuspended(info);
    return KE_OK;
}

int ThreadManager::ResumeThread(int tid) {
    if (tid == 0) tid = t_current_tid;
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kResumeThread, tid, 0})) return KE_OK;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    bool ready = false;
    int prio = 64;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status == THS_DORMANT) return KE_DORMANT;
        if (info->suspend_count <= 0) return KE_NOT_SUSPEND;
        info->suspend_count--;
        if (info->suspend_count == 0) {
            if (info->wait_type != TSW_NONE) {
                info->status = THS_WAIT;
            } else if (tid == t_current_tid) {
                info->status = THS_RUN;
            } else {
                info->status = THS_READY;
                ready = true;
                prio = info->current_priority;
            }
        }
    }
    info->cv.notify_all();
    if (ready) {
        EeRunToken::Instance().MakeReady(tid, prio);
        EeRunToken::Instance().PreemptPoint();
    }
    return KE_OK;
}

int ThreadManager::ReleaseWaitThread(int tid) {
    if (tid == 0 || tid == t_current_tid) return KE_ILLEGAL_THID;
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kReleaseWaitThread, tid, 0})) return KE_OK;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    int wait_type = TSW_NONE, wait_id = 0;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status != THS_WAIT && info->status != THS_WAITSUSPEND) return KE_NOT_WAIT;  // R7
        wait_type = info->wait_type;
        wait_id = info->wait_id;
        info->force_release = true;
        info->wait_type = TSW_NONE;
        info->wait_id = 0;
        info->status = (info->suspend_count > 0) ? THS_SUSPEND : THS_READY;
    }
    info->cv.notify_all();
    if (wait_type == TSW_SEMA) {
        Semaphores::Instance().RemoveWaiter(wait_id, tid);
        if (auto s = Semaphores::Instance().Lookup(wait_id)) s->cv.notify_all();
    } else if (wait_type == TSW_EVENT) {
        EventFlags::Instance().RemoveWaiter(wait_id, tid);
        if (auto e = EventFlags::Instance().Lookup(wait_id)) e->cv.notify_all();
    }
    EeRunToken::Instance().MakeReady(tid, PriorityOf(info));
    EeRunToken::Instance().PreemptPoint();
    return KE_OK;
}

// ---- priority / dispatch (R8) --------------------------------------------------

int ThreadManager::ChangeThreadPriority(int tid, int priority) {
    if (tid == 0) tid = t_current_tid;
    if (priority < 0 || priority > kEeMaxPriority) return KE_ILLEGAL_PRIORITY;
    auto info = Lookup(tid);
    if (!info) return KE_UNKNOWN_THID;
    int old;
    {
        std::lock_guard<std::mutex> lk(info->m);
        if (info->status == THS_DORMANT) return KE_DORMANT;
        old = info->current_priority;
        info->current_priority = priority;
    }
    EeRunToken::Instance().SetPriority(tid, priority);  // tail of the new priority
    EeRunToken::Instance().PreemptPoint();
    return old;
}

int ThreadManager::RotateThreadReadyQueue(int priority) {
    if (priority < 0 || priority > kEeMaxPriority) return KE_ILLEGAL_PRIORITY;
    EeEventQueue::Instance().DrainIfPendingOnHolder();
    EeRunToken::Instance().Rotate(priority);
    return KE_OK;
}

int ThreadManager::DisableDispatchThread() { return 1; }
int ThreadManager::EnableDispatchThread() { return KE_OK; }

// ---- wait-site helpers -----------------------------------------------------------

void ThreadManager::EnterObjectWait(const std::shared_ptr<ThreadInfo>& info, int wait_type, int id) {
    if (!info) return;
    std::lock_guard<std::mutex> lk(info->m);
    info->status = (info->suspend_count > 0) ? THS_WAITSUSPEND : THS_WAIT;
    info->wait_type = wait_type;
    info->wait_id = id;
    info->force_release = false;
    info->released.store(false, std::memory_order_relaxed);
}

void ThreadManager::LeaveObjectWait(const std::shared_ptr<ThreadInfo>& info) {
    if (!info) return;
    std::lock_guard<std::mutex> lk(info->m);
    info->status = (info->suspend_count > 0) ? THS_SUSPEND : THS_RUN;
    info->wait_type = TSW_NONE;
    info->wait_id = 0;
}

void ThreadManager::ThrowIfTerminated(const std::shared_ptr<ThreadInfo>& info) {
    if (info && info->terminated.load()) throw ThreadExitUnwind{};
}

void ThreadManager::WaitWhileSuspended(const std::shared_ptr<ThreadInfo>& info) {
    if (!info) return;
    const int tid = t_current_tid;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(info->m);
            if (info->suspend_count == 0) return;
            info->status = THS_SUSPEND;
            info->wait_type = TSW_NONE;
            info->wait_id = 0;
        }
        // A suspension park releases the token; a queue poke wakes us to drain
        // (the queued ResumeThread may be our own release).
        NoteCapturePark(tid, true, true);
        EeRunToken::Instance().BlockBegin(tid);
        {
            EeEventQueue::ParkReg reg(&info->m, &info->cv);
            std::unique_lock<std::mutex> lk(info->m);
            info->cv.wait(lk, [&] {
                return info->suspend_count == 0 || info->terminated.load() || EeEventQueue::Instance().HasPending() ||
                       !Running();
            });
        }
        NoteCapturePark(tid, false, true);
        EeRunToken::Instance().BlockEnd(tid);
        if (info->terminated.load()) throw ThreadExitUnwind{};
        if (!Running()) return;
        std::lock_guard<std::mutex> lk(info->m);
        if (info->suspend_count == 0) {
            info->status = THS_RUN;
            return;
        }
    }
}

void ThreadManager::NotePark(int tid, bool parked) {
    NoteCapturePark(tid, parked, false);
    if (auto* fn = park_observer_.load(std::memory_order_acquire)) fn(tid, parked);
}

void ThreadManager::IdleWhileBlocked(const std::shared_ptr<ThreadInfo>& info) {
    if (!info || !info->ctx || !Running()) return;
    EeYieldPoint::Instance().IdleWhileBlocked(*info->ctx);
}

void ThreadManager::NoteCapturePark(int tid, bool parked, bool suspended) {
    if (tid != t_current_tid) return;
    const auto info = Current();
    if (!info) return;
    {
        std::lock_guard<std::mutex> lock(info->m);
        if (info->capture_incarnation != t_incarnation || info->capture_owner != std::this_thread::get_id())
            return;
        info->capture_parked = parked;
        info->capture_suspended = suspended;
        ++info->capture_revision;
    }
    NotifyCapture(tid, info, parked);
}

// ---- shutdown --------------------------------------------------------------------

void ThreadManager::JoinHostThread(int tid) {
    std::lock_guard<std::mutex> hl(host_mu_);
    auto it = host_threads_.find(tid);
    if (it != host_threads_.end()) {
        ps2r::host::ThreadJoin(it->second);
        host_threads_.erase(it);
    }
}

void ThreadManager::TerminateAll() {
    running_.store(false);
    EeRunToken::Instance().Shutdown();  // every acquire returns: unwinds never deadlock
    std::lock_guard<std::mutex> lk(map_mu_);
    for (auto& [tid, info] : threads_) {
        if (!info) continue;
        {
            std::lock_guard<std::mutex> tl(info->m);
            info->terminated = true;
            info->force_release = true;
        }
        info->cv.notify_all();
    }
}

void ThreadManager::JoinAll() {
    std::lock_guard<std::mutex> hl(host_mu_);
    for (auto& [tid, pt] : host_threads_) ps2r::host::ThreadJoin(pt);
    host_threads_.clear();
}

size_t ThreadManager::JoinAll(uint64_t deadline_ms, std::string* report) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadline_ms);
    std::lock_guard<std::mutex> hl(host_mu_);
    size_t abandoned = 0;
    for (auto& [tid, pt] : host_threads_) {
        auto info = Lookup(tid);
        bool finished = !info;
        if (info) {
            std::unique_lock<std::mutex> lk(info->m);
            // started is cleared by FinishCurrentThread on the worker, under m, then cv notified.
            finished = info->cv.wait_until(lk, deadline, [&] { return !info->started && !info->ctx; });
            if (!finished && report) {
                if (!report->empty()) *report += ",";
                *report += std::to_string(tid) + ":entry=0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%x", info->entry); return std::string(b); }() +
                           ":status=" + std::to_string(info->status) + ":wait=" + std::to_string(info->wait_type);
            }
        }
        if (finished) ps2r::host::ThreadJoin(pt);
        else { ps2r::host::ThreadDetach(pt); ++abandoned; }
    }
    host_threads_.clear();
    return abandoned;
}

bool ThreadManager::ReleaseCurrentThread(std::string* error) {
    if (!t_incarnation || !FinishCurrentThread(t_current_tid, Current(), t_incarnation)) {
        if (error) *error = "This host thread does not own the current worker incarnation.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ThreadManager::RestartAfterJoin(std::string* error) {
    auto fail = [&](const char* reason) { if (error) *error = reason; return false; };
    std::lock_guard<std::mutex> host_lock(host_mu_);
    if (Running()) return fail("Thread execution has not been stopped.");
    if (!host_threads_.empty()) return fail("Old worker threads must be joined before restart.");
    std::lock_guard<std::mutex> table_lock(map_mu_);
    for (const auto& [tid, info] : threads_) {
        (void)tid;
        if (!info) return fail("Invalid old thread record.");
        std::lock_guard<std::mutex> info_lock(info->m);
        if (info->ctx || info->started || info->capture_owner != std::thread::id{})
            return fail("An old context owner has not completed its exit.");
    }
    if (EeEventQueue::Instance().HasPending())
        return fail("Old event deliveries must be drained on their owner before restart.");
    if (!EeRunToken::Instance().RestartAfterJoin(error)) return false;
    threads_.clear();
    next_tid_ = 2;
    running_.store(true);
    t_current_tid = 1;
    t_incarnation = 0;
    // next_incarnation_ deliberately stays monotonic across every restore.
    if (error) error->clear();
    return true;
}

std::vector<ThreadManager::TableEntry> ThreadManager::SnapshotTable() {
    std::vector<TableEntry> out;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        out.reserve(threads_.size());
        for (const auto& kv : threads_) out.emplace_back(kv.first, kv.second);
    }
    std::sort(out.begin(), out.end(), [](const TableEntry& a, const TableEntry& b) { return a.first < b.first; });
    return out;
}

void ThreadManager::InstallTable(const std::vector<TableEntry>& entries) {
    std::lock_guard<std::mutex> lk(map_mu_);
    threads_.clear();
    int max_tid = 1;
    for (const TableEntry& e : entries) {
        threads_[e.first] = e.second;
        if (e.first > max_tid) max_tid = e.first;
    }
    next_tid_ = max_tid + 1;
}

void ThreadManager::ResetForTest() {
    JoinAll();
    std::lock_guard<std::mutex> lk(map_mu_);
    threads_.clear();
    next_tid_ = 2;
    running_.store(true);
    t_current_tid = 1;
    t_incarnation = 0;
}
