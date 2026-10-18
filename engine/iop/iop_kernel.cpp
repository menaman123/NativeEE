// engine/iop/iop_kernel.cpp -- IopKernel: the cooperative scheduler.
#include "engine/iop/iop_kernel.h"

#include <algorithm>
#include <cstdio>

#include "engine/iop/iop_error.h"

namespace ps2r::iop {

namespace {
// A foreign host must not mistake a coroutine running on the dispatcher for
// its own context (or switch stacks into that dispatcher's saved context).
thread_local IopKernel* running_kernel = nullptr;
}  // namespace

IopKernel::IopKernel() {
    for (int i = 0; i < kMaxThreads; i++) {
        threads_[i].tid = i;
        threads_[i].owner = this;
    }
}

IopKernel::~IopKernel() { Shutdown(); }

void IopKernel::SetThreadRunHooks(std::function<void(int)> pre, std::function<void(int)> post) {
    run_pre_ = std::move(pre);
    run_post_ = std::move(post);
}

void IopKernel::SetWakeHook(std::function<void()> hook) { wake_hook_ = std::move(hook); }

void IopKernel::SetTraceHook(TraceHook hook) { trace_ = std::move(hook); }

void IopKernel::PokeWakeHook() {
    if (wake_hook_) wake_hook_();
}

void IopKernel::Trace(const char* evt, int64_t a, int64_t b) {
    if (trace_) trace_(evt, a, b, clock_.NowUs());
}

// -- threads -------------------------------------------------------------------

bool IopKernel::RegisterThread(int tid, uint32_t priority) {
    if (tid <= 0 || tid >= kMaxThreads) return false;
    Thread& t = threads_[tid];
    if (t.registered) return false;
    t.priority = priority;
    t.initial_priority = priority;
    t.state = State::Dormant;
    t.wait = WaitKind::None;
    t.wakeup_pending = 0;
    t.registered = true;
    return true;
}

void IopKernel::UnregisterThread(int tid) {
    if (tid <= 0 || tid >= kMaxThreads) return;
    TerminateThread(tid);
    threads_[tid].registered = false;
}

void IopKernel::FiberTrampoline(void* param) {
    Thread* t = static_cast<Thread*>(param);
    for (;;) {
        // Keep the callable owned by the thread record. Terminating a parked
        // coroutine destroys its stack without unwinding C++ stack locals.
        if (t->body) t->body();
        t->body = nullptr;
        t->owner->ExitThread();   // parks Dormant; resumes here on the next StartThread
    }
}

bool IopKernel::StartThread(int tid, ThreadBody body) {
    if (tid <= 0 || tid >= kMaxThreads) return false;
    Thread& t = threads_[tid];
    if (!t.registered || t.state != State::Dormant) return false;
    if (!t.coro.Valid()) {
        if (!t.coro.Create(kThreadStackBytes, &IopKernel::FiberTrampoline, &t)) {
            std::fprintf(stderr, "iop: coroutine create failed tid=%d\n", tid);
            return false;
        }
    }
    t.body = std::move(body);
    t.priority = t.initial_priority;
    t.wait = WaitKind::None;
    t.wakeup_pending = 0;
    MakeReady(&t);
    Trace("start", tid, (int64_t)t.priority);
    PokeWakeHook();
    return true;
}

void IopKernel::ExitThread() {
    Thread* self = current_;
    if (!self) return;
    self->state = State::Dormant;
    self->wait = WaitKind::None;
    unparked_.fetch_sub(1, std::memory_order_acq_rel);
    Trace("exit", self->tid, 0);
    LeaveThread();
}

void IopKernel::TerminateThread(int tid) {
    if (tid <= 0 || tid >= kMaxThreads) return;
    Thread& t = threads_[tid];
    if (t.state == State::Ready || t.state == State::Run)
        unparked_.fetch_sub(1, std::memory_order_acq_rel);
    ClearFromWaitLists(&t);
    t.state = State::Dormant;
    t.wait = WaitKind::None;
    t.body = nullptr;
    t.wakeup_pending = 0;
    // A terminated continuation must not resume at its old Sleep/Wait/Delay
    // call on a later StartThread. Never free the running stack itself.
    if (&t != current_) t.coro.Destroy();
}

int IopKernel::ChangePriority(int tid, uint32_t priority) {
    Thread* self = OnKernelThread() ? current_ : nullptr;
    if (tid == 0) {
        if (!self) return IopError::IllegalContext;
        tid = self->tid;
    }
    if (!ThreadRegistered(tid)) return IopError::UnknownThid;
    Thread& target = threads_[tid];
    if (target.state == State::Dormant) return IopError::Dormant;
    if (priority == 0) {
        if (!self) return IopError::IllegalContext;
        priority = self->priority;
    }
    if (priority > 126) return IopError::IllegalPriority;
    target.priority = priority;
    if (target.state == State::Ready) target.ready_seq = ready_seq_++;
    if (target.state == State::Wait && target.wait == WaitKind::Sema) {
        // Selection uses current priorities. Moving to the tail preserves
        // FIFO among equal priorities after a priority-queue reinsertion.
        for (Sema& sema : semas_) {
            if (!sema.active || !sema.prio_order) continue;
            auto it = std::find(sema.waiters.begin(), sema.waiters.end(), &target);
            if (it != sema.waiters.end()) sema.waiters.splice(sema.waiters.end(), sema.waiters, it);
        }
    }
    Thread* next = SchedNext();
    if (self && next && (next->priority < self->priority ||
                       (&target == self && next->priority == self->priority))) {
        self->state = State::Ready;
        // Changing self goes to the tail; a preempted caller retains its
        // place ahead of its equal-priority peers.
        if (&target == self) self->ready_seq = ready_seq_++;
        LeaveThread();
    }
    return IopError::Ok;
}

int IopKernel::CurrentTid() const {
    return OnKernelThread() ? current_->tid : 0;
}

bool IopKernel::OnKernelThread() const {
    return running_kernel == this && current_ != nullptr;
}

uint32_t IopKernel::ThreadPriority(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return 0;
    return threads_[tid].priority;
}

void IopKernel::SleepThread() {
    Thread* self = current_;
    if (!self) return;
    if (self->wakeup_pending > 0) {
        self->wakeup_pending--;   // a banked WakeupThread token (thbase): no park
        return;
    }
    Trace("sleep", self->tid, 0);
    SetWaiting(self, WaitKind::Sleep);
    LeaveThread();
}

int IopKernel::WakeupThread(int tid) {
    if (tid == 0 || tid == CurrentTid()) return IopError::IllegalThid;
    if (!ThreadRegistered(tid)) return IopError::UnknownThid;
    Thread& t = threads_[tid];
    if (t.state == State::Dormant) return IopError::Dormant;
    if (t.state == State::Wait && t.wait == WaitKind::Sleep) {
        Trace("wakeup", tid, 0);
        MakeReady(&t);
    } else {
        t.wakeup_pending++;       // bank: consumed by the next SleepThread
        Trace("wakeup_bank", tid, t.wakeup_pending);
    }
    return IopError::Ok;
}

void IopKernel::iWakeupThread(int tid) {
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        injections_.push_back([this, tid] {
            if (WakeupThread(tid) != IopError::Ok)
                injected_wake_errors_.fetch_add(1, std::memory_order_relaxed);
        });
    }
    PokeWakeHook();
}

void IopKernel::iWakeupIfRpcWait(int tid) {
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        injections_.push_back([this, tid] {
            if (tid <= 0 || tid >= kMaxThreads) return;
            Thread& t = threads_[tid];
            if (t.state == State::Wait && t.wait == WaitKind::Rpc) {
                t.rpc_woken = true;
                Trace("rpc_wake", tid, 0);
                MakeReady(&t);
            }
            // A reply after the waiter timed out is dropped: never bank a token
            // for a wait that no longer exists.
        });
    }
    PokeWakeHook();
}

void IopKernel::DelayUs(uint64_t usec) {
    Thread* self = current_;
    if (!self) return;
    self->wait_due_us = clock_.NowUs() + usec;
    Trace("delay", self->tid, (int64_t)usec);
    SetWaiting(self, WaitKind::Delay);
    LeaveThread();
}

void IopKernel::Yield() {
    Thread* self = current_;
    if (!self) return;
    self->state = State::Ready;   // Run -> Ready keeps the unparked census
    self->wait = WaitKind::None;
    self->ready_seq = ready_seq_++;
    LeaveThread();
}

bool IopKernel::WaitRpcUs(uint64_t timeout_usec) {
    Thread* self = current_;
    if (!self) return false;
    self->rpc_woken = false;
    self->wait_due_us = clock_.NowUs() + timeout_usec;
    Trace("rpc_wait", self->tid, (int64_t)timeout_usec);
    SetWaiting(self, WaitKind::Rpc);
    LeaveThread();
    return self->rpc_woken;
}

// -- semaphores ----------------------------------------------------------------

bool IopKernel::RegisterSema(int sid, int32_t init, int32_t max_count, bool prio_order) {
    if (sid < 0 || sid >= kMaxSemas) return false;
    Sema& s = semas_[sid];
    if (s.active || init < 0 || max_count <= 0 || init > max_count) return false;
    s.active = true;
    s.count = init;
    s.max_count = max_count;
    s.prio_order = prio_order;
    s.waiters.clear();
    return true;
}

int IopKernel::DeleteSema(int sid) {
    if (!SemaActive(sid)) return IopError::UnknownSemid;
    Sema& s = semas_[sid];
    s.active = false;
    s.count = 0;
    while (!s.waiters.empty()) {   // waiters return observing the inactive sema
        Thread* t = s.waiters.front();
        s.waiters.pop_front();
        t->wait_result = IopError::WaitDelete;
        MakeReady(t);
    }
    return IopError::Ok;
}

int IopKernel::SignalSema(int sid) {
    if (!SemaActive(sid)) return IopError::UnknownSemid;
    Sema& s = semas_[sid];
    if (!s.waiters.empty()) {
        Thread* to_run = nullptr;
        if (s.prio_order) {
            auto best = s.waiters.begin();
            for (auto it = s.waiters.begin(); it != s.waiters.end(); ++it)
                if ((*it)->priority < (*best)->priority) best = it;
            to_run = *best;
            s.waiters.erase(best);
        } else {
            to_run = s.waiters.front();
            s.waiters.pop_front();
        }
        Trace("sema_handoff", sid, to_run->tid);
        MakeReady(to_run);          // direct handoff: the count stays put
        return IopError::Ok;
    }
    if (s.count >= s.max_count) return IopError::SemaOverflow;
    s.count++;
    Trace("sema_signal", sid, s.count);
    return IopError::Ok;
}

void IopKernel::InjectSignalSema(int sid) {
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        injections_.push_back([this, sid] {
            if (SignalSema(sid) != IopError::Ok)
                injected_signal_errors_.fetch_add(1, std::memory_order_relaxed);
        });
    }
    PokeWakeHook();
}

int IopKernel::WaitSema(int sid) {
    if (!SemaActive(sid)) return IopError::UnknownSemid;
    Thread* self = OnKernelThread() ? current_ : nullptr;
    Sema& s = semas_[sid];
    if (s.count > 0) {
        s.count--;
        Trace("sema_take", sid, self ? self->tid : 0);
        return IopError::Ok;
    }
    if (!self) return IopError::CanNotWait;
    Trace("sema_wait", sid, self->tid);
    self->wait_result = IopError::Ok;
    s.waiters.push_back(self);
    SetWaiting(self, WaitKind::Sema);
    LeaveThread();
    return self->wait_result;
}

int32_t IopKernel::PollSema(int sid) {
    if (sid < 0 || sid >= kMaxSemas) return -1;
    Sema& s = semas_[sid];
    if (!s.active || s.count <= 0) return -1;
    s.count--;
    return 0;
}

int32_t IopKernel::SemaCount(int sid) const {
    if (sid < 0 || sid >= kMaxSemas) return 0;
    return semas_[sid].count;
}

void IopKernel::SetSemaCount(int sid, int32_t count) {
    if (sid < 0 || sid >= kMaxSemas) return;
    semas_[sid].count = count;
}

bool IopKernel::SemaActive(int sid) const {
    if (sid < 0 || sid >= kMaxSemas) return false;
    return semas_[sid].active;
}

void IopKernel::ReconcileSemaWaiters() {
    for (int sid = 0; sid < kMaxSemas; sid++) {
        Sema& s = semas_[sid];
        if (!s.active) continue;
        while (s.count > 0 && !s.waiters.empty()) {
            auto best = s.waiters.begin();
            if (s.prio_order) {
                for (auto it = s.waiters.begin(); it != s.waiters.end(); ++it)
                    if ((*it)->priority < (*best)->priority) best = it;
            }
            Thread* t = *best;
            s.waiters.erase(best);
            s.count--;
            MakeReady(t);
        }
    }
}

// -- event flags ---------------------------------------------------------------

bool IopKernel::EventFlagMatch(uint32_t value, uint32_t pattern, uint32_t mode) {
    if (mode & 1) return (value & pattern) != 0;   // WEF_OR
    return (value & pattern) == pattern;           // WEF_AND
}

int IopKernel::CreateEventFlag(uint32_t init_pattern, bool multi_waiters) {
    EventFlagRec ef;
    ef.active = true;
    ef.multi = multi_waiters;
    ef.value = init_pattern;
    event_flags_.push_back(std::move(ef));
    return (int)event_flags_.size() - 1;
}

int IopKernel::WaitEventFlag(int id, uint32_t pattern, uint32_t mode) {
    Thread* self = current_;
    if (id < 0 || id >= (int)event_flags_.size()) return -1;
    EventFlagRec& ef = event_flags_[id];
    if (EventFlagMatch(ef.value, pattern, mode)) {
        if (mode & 0x10) ef.value = 0;             // WEF_CLEAR on match
        return 0;
    }
    if (!self) return -1;                          // non-thread poll
    if (!ef.multi && !ef.waiters.empty()) return -1;   // single-waiter attribute
    ef.waiters.push_back({self, pattern, mode});
    SetWaiting(self, WaitKind::EventFlag);
    LeaveThread();
    return 0;
}

void IopKernel::CompleteEventFlagWaiters(EventFlagRec& ef) {
    for (auto it = ef.waiters.begin(); it != ef.waiters.end();) {
        if (EventFlagMatch(ef.value, it->pattern, it->mode)) {
            if (it->mode & 0x10) ef.value = 0;
            MakeReady(it->t);
            it = ef.waiters.erase(it);
        } else {
            ++it;
        }
    }
}

int IopKernel::SetEventFlag(int id, uint32_t pattern) {
    if (id < 0 || id >= (int)event_flags_.size()) return -1;
    EventFlagRec& ef = event_flags_[id];
    ef.value |= pattern;
    CompleteEventFlagWaiters(ef);
    return 0;
}

int IopKernel::ClearEventFlag(int id, uint32_t keep_mask) {
    if (id < 0 || id >= (int)event_flags_.size()) return -1;
    event_flags_[id].value &= keep_mask;   // thevent: the argument is the keep mask
    return 0;
}

uint32_t IopKernel::EventFlagValue(int id) const {
    if (id < 0 || id >= (int)event_flags_.size()) return 0;
    return event_flags_[id].value;
}

// -- message boxes -------------------------------------------------------------

int IopKernel::CreateMbx() {
    MbxRec m;
    m.active = true;
    mbxs_.push_back(std::move(m));
    return (int)mbxs_.size() - 1;
}

int IopKernel::SendMbx(int id, void* msg) {
    if (id < 0 || id >= (int)mbxs_.size()) return -1;
    MbxRec& m = mbxs_[id];
    m.messages.push(msg);
    if (!m.waiters.empty()) {
        Thread* t = m.waiters.front();
        m.waiters.pop_front();
        MakeReady(t);
    }
    return 0;
}

int IopKernel::PollMbx(void** out, int id) {
    if (id < 0 || id >= (int)mbxs_.size()) return -1;
    MbxRec& m = mbxs_[id];
    if (m.messages.empty()) return -1;
    if (out) *out = m.messages.front();
    m.messages.pop();
    return 0;
}

int IopKernel::ReceiveMbx(void** out, int id) {
    Thread* self = current_;
    if (id < 0 || id >= (int)mbxs_.size()) return -1;
    if (!self) return PollMbx(out, id);
    while (mbxs_[id].messages.empty()) {   // re-wait if a poller took the message
        mbxs_[id].waiters.push_back(self);
        SetWaiting(self, WaitKind::Mbx);
        LeaveThread();
    }
    return PollMbx(out, id);
}

// -- deferred events + periodic timers -----------------------------------------

void IopKernel::ScheduleEventUs(uint64_t due_us, std::function<void()> fire) {
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        Event e;
        e.due_us = due_us;
        e.seq = event_seq_++;
        e.gen = dispatch_gen_.load(std::memory_order_acquire);
        e.fire = std::move(fire);
        injections_.push_back([this, e = std::move(e)]() mutable { events_.push_back(std::move(e)); });
    }
    Trace("evt_sched", (int64_t)due_us, 0);
    PokeWakeHook();
}

int IopKernel::StartPeriodicTimerUs(uint64_t period_us, std::function<bool()> beat) {
    const uint64_t period = period_us ? period_us : 1;
    return StartPeriodicTimerAtUs(clock_.NowUs() + period, period, std::move(beat));
}

int IopKernel::StartPeriodicTimerAtUs(uint64_t first_due_us, uint64_t period_us, std::function<bool()> beat) {
    const int id = next_timer_id_.fetch_add(1, std::memory_order_acq_rel);
    Timer t;
    t.id = id;
    t.running = true;
    t.period_us = period_us ? period_us : 1;
    t.due_us = first_due_us;
    t.beat = std::move(beat);
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        injections_.push_back([this, t = std::move(t)]() mutable { timers_.push_back(std::move(t)); });
    }
    Trace("timer_start", id, (int64_t)period_us);
    PokeWakeHook();
    return id;
}

std::optional<uint64_t> IopKernel::TimerDueUs(int id) const {
    for (const Timer& t : timers_)
        if (t.id == id && t.running) return t.due_us;
    return std::nullopt;
}

void IopKernel::StopTimer(int id) {
    std::lock_guard<std::mutex> lk(inject_mutex_);
    injections_.push_back([this, id] {
        for (auto& t : timers_)
            if (t.id == id) t.running = false;
    });
}

// -- scheduler internals -------------------------------------------------------

void IopKernel::MakeReady(Thread* t) {
    if (t->state == State::Ready || t->state == State::Run) return;
    t->state = State::Ready;
    t->wait = WaitKind::None;
    t->ready_seq = ready_seq_++;
    unparked_.fetch_add(1, std::memory_order_acq_rel);
}

void IopKernel::SetWaiting(Thread* t, WaitKind k) {
    if (t->state == State::Ready || t->state == State::Run)
        unparked_.fetch_sub(1, std::memory_order_acq_rel);
    t->state = State::Wait;
    t->wait = k;
}

void IopKernel::ClearFromWaitLists(Thread* t) {
    for (int i = 0; i < kMaxSemas; i++) semas_[i].waiters.remove(t);
    for (auto& ef : event_flags_)
        ef.waiters.remove_if([t](const EventFlagRec::Waiter& w) { return w.t == t; });
    for (auto& m : mbxs_) m.waiters.remove(t);
}

IopKernel::Thread* IopKernel::SchedNext() {
    Thread* best = nullptr;
    for (int i = 1; i < kMaxThreads; i++) {
        Thread& t = threads_[i];
        if (t.state != State::Ready) continue;
        if (!best || t.priority < best->priority ||
            (t.priority == best->priority && t.ready_seq < best->ready_seq)) {
            best = &t;
        }
    }
    return best;
}

void IopKernel::UpdateDeadlines() {
    const uint64_t now = clock_.NowUs();
    for (int i = 1; i < kMaxThreads; i++) {
        Thread& t = threads_[i];
        if (t.state != State::Wait) continue;
        if ((t.wait == WaitKind::Delay || t.wait == WaitKind::Rpc) && t.wait_due_us <= now) {
            Trace(t.wait == WaitKind::Delay ? "delay_due" : "rpc_timeout", t.tid, 0);
            MakeReady(&t);   // an Rpc expiry leaves rpc_woken false (timeout)
        }
    }
}

void IopKernel::ApplyInjections() {
    std::deque<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lk(inject_mutex_);
        work.swap(injections_);
    }
    for (auto& f : work) f();
}

bool IopKernel::FireDueEventsAndTimers() {
    const uint64_t now = clock_.NowUs();
    const uint64_t cur_gen = dispatch_gen_.load(std::memory_order_acquire);
    bool fired = false;
    if (!events_.empty())
        Trace("evq", (int64_t)events_.size(), (int64_t)events_[0].due_us);
    // Events: every due entry scheduled BEFORE this pass, in (due, seq) order --
    // N schedules fire N callbacks, never inside the issuing pass.
    std::vector<Event> due;
    for (auto it = events_.begin(); it != events_.end();) {
        if (it->due_us <= now && it->gen < cur_gen) {
            due.push_back(std::move(*it));
            it = events_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(due.begin(), due.end(), [](const Event& a, const Event& b) {
        return a.due_us != b.due_us ? a.due_us < b.due_us : a.seq < b.seq;
    });
    for (auto& e : due) {
        Trace("evt_fire", (int64_t)e.due_us, (int64_t)e.seq);
        if (e.fire) e.fire();
        fired = true;
    }
    // Timers: at most one beat per timer per pass; a stalled timer resyncs
    // forward instead of bursting (no beat debt).
    for (auto& t : timers_) {
        if (!t.running || t.due_us > now) continue;
        Trace("hb_beat", t.id, (int64_t)t.due_us);
        timer_beats_++;
        const bool rearm = t.beat ? t.beat() : false;
        if (rearm) {
            t.due_us += t.period_us;
            if (t.due_us <= now) t.due_us = now + t.period_us;
        } else {
            t.running = false;
        }
        fired = true;
    }
    return fired;
}

std::optional<uint64_t> IopKernel::NextDueUs() const {
    std::optional<uint64_t> best;
    auto acc = [&best](uint64_t v) {
        if (!best || v < *best) best = v;
    };
    for (int i = 1; i < kMaxThreads; i++) {
        const Thread& t = threads_[i];
        if (t.state == State::Wait && (t.wait == WaitKind::Delay || t.wait == WaitKind::Rpc))
            acc(t.wait_due_us);
    }
    for (const auto& e : events_) acc(e.due_us);
    for (const auto& t : timers_)
        if (t.running) acc(t.due_us);
    return best;
}

void IopKernel::RunThread(Thread* t) {
    IopKernel* previous = running_kernel;
    current_ = t;
    t->state = State::Run;
    Trace("run", t->tid, (int64_t)t->priority);
    if (run_pre_) run_pre_(t->tid);
    running_kernel = this;
    t->coro.SwitchTo();
    running_kernel = previous;
    if (run_post_) run_post_(t->tid);
    current_ = nullptr;
}

void IopKernel::LeaveThread() { IopCoroutine::SwitchToContext(dispatch_ctx_); }

IopKernel::DispatchResult IopKernel::Dispatch() {
    std::lock_guard<std::recursive_mutex> lk(dispatch_mutex_);
    DispatchResult r;
    if (shutdown_.load(std::memory_order_acquire)) return r;
    // The generation advances per Dispatch CALL: an event scheduled during pass
    // k fires at pass k+1 at the earliest (the counted-queue contract). Under
    // Frame the EE advances virtual time between calls, so a delay-poll loop
    // cannot generation-hold its own completion.
    dispatch_gen_.fetch_add(1, std::memory_order_acq_rel);
    dispatch_ctx_ = IopCoroutine::CurrentHostContext();
    for (;;) {
        ApplyInjections();
        UpdateDeadlines();
        const bool fired = FireDueEventsAndTimers();
        Thread* t = SchedNext();
        if (t) {
            RunThread(t);
            r.ran_something = true;
            continue;
        }
        if (!fired) break;
        r.ran_something = true;
    }
    r.next_due_us = NextDueUs();
    return r;
}

void IopKernel::WithDispatchLock(const std::function<void()>& fn) {
    std::lock_guard<std::recursive_mutex> lk(dispatch_mutex_);
    fn();
}

IopKernel::State IopKernel::ThreadState(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return State::Dormant;
    return threads_[tid].state;
}

IopKernel::WaitKind IopKernel::ThreadWaitKind(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return WaitKind::None;
    return threads_[tid].wait;
}

bool IopKernel::ThreadRegistered(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return false;
    return threads_[tid].registered;
}

void IopKernel::ReconcileThreadFromFlags(int tid, bool sleeping, bool terminated) {
    if (tid <= 0 || tid >= kMaxThreads) return;
    Thread& t = threads_[tid];
    if (terminated) {
        TerminateThread(tid);
        return;
    }
    // Parked in Sleep but the restored flag says awake: release it.
    if (!sleeping && t.state == State::Wait && t.wait == WaitKind::Sleep) MakeReady(&t);
}

void IopKernel::DropPendingEvents() {
    std::lock_guard<std::recursive_mutex> lk(dispatch_mutex_);
    events_.clear();
}

uint32_t IopKernel::ThreadInitialPriority(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return 0;
    return threads_[tid].initial_priority;
}

int IopKernel::ThreadWakeupPending(int tid) const {
    if (tid <= 0 || tid >= kMaxThreads) return 0;
    return threads_[tid].wakeup_pending;
}

void IopKernel::SetThreadWakeupPending(int tid, int tokens) {
    if (tid <= 0 || tid >= kMaxThreads || tokens < 0) return;
    threads_[tid].wakeup_pending = tokens;
}

int32_t IopKernel::SemaMaxCount(int sid) const {
    if (sid < 0 || sid >= kMaxSemas) return 0;
    return semas_[sid].max_count;
}

bool IopKernel::SemaPrioOrder(int sid) const {
    if (sid < 0 || sid >= kMaxSemas) return false;
    return semas_[sid].prio_order;
}

bool IopKernel::EventFlagActive(int ef) const {
    if (ef < 0 || ef >= static_cast<int>(event_flags_.size())) return false;
    return event_flags_[ef].active;
}

bool IopKernel::EventFlagMulti(int ef) const {
    if (ef < 0 || ef >= static_cast<int>(event_flags_.size())) return false;
    return event_flags_[ef].multi;
}

void IopKernel::RestoreEventFlag(int ef, bool active, bool multi, uint32_t value) {
    if (ef < 0 || ef >= 4096) return;
    if (ef >= static_cast<int>(event_flags_.size())) event_flags_.resize(static_cast<size_t>(ef) + 1);
    EventFlagRec& rec = event_flags_[ef];
    rec.active = active;
    rec.multi = multi;
    rec.value = value;
    rec.waiters.clear();
}

bool IopKernel::MbxActive(int mbx) const {
    if (mbx < 0 || mbx >= static_cast<int>(mbxs_.size())) return false;
    return mbxs_[mbx].active;
}

std::vector<void*> IopKernel::MbxMessages(int mbx) const {
    std::vector<void*> out;
    if (mbx < 0 || mbx >= static_cast<int>(mbxs_.size())) return out;
    std::queue<void*> copy = mbxs_[mbx].messages;
    while (!copy.empty()) {
        out.push_back(copy.front());
        copy.pop();
    }
    return out;
}

void IopKernel::RestoreMbx(int mbx, bool active, const std::vector<void*>& messages) {
    if (mbx < 0 || mbx >= 4096) return;
    if (mbx >= static_cast<int>(mbxs_.size())) mbxs_.resize(static_cast<size_t>(mbx) + 1);
    MbxRec& rec = mbxs_[mbx];
    rec.active = active;
    rec.messages = std::queue<void*>();
    for (void* m : messages) rec.messages.push(m);
    rec.waiters.clear();
}

void IopKernel::ResetForRestore() {
    std::lock_guard<std::recursive_mutex> lk(dispatch_mutex_);
    for (int tid = 1; tid < kMaxThreads; tid++) UnregisterThread(tid);
    for (int sid = 0; sid < kMaxSemas; sid++) {
        Sema& s = semas_[sid];
        s.active = false;
        s.count = 0;
        s.max_count = 0;
        s.prio_order = false;
        s.waiters.clear();
    }
    event_flags_.clear();
    mbxs_.clear();
    events_.clear();
    timers_.clear();
    {
        std::lock_guard<std::mutex> ilk(inject_mutex_);
        injections_.clear();
    }
    unparked_.store(0, std::memory_order_release);
}

void IopKernel::Shutdown() {
    shutdown_.store(true, std::memory_order_release);
    PokeWakeHook();
}

}  // namespace ps2r::iop
