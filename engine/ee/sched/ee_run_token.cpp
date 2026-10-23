// EeRunToken implementation; the model and the thbase rule are in the header.
#include "engine/ee/sched/ee_run_token.h"

#include "engine/ee/sched/ee_yield_point.h"
#include "engine/platform/host/platform.h"

#include <atomic>

namespace {
std::atomic<uint64_t> g_seq{1};
thread_local int t_tid = EeRunToken::kNone;
thread_local int t_prio = 0;
thread_local bool t_holds = false;
thread_local int t_park_depth = 0;
thread_local bool t_guest_attached = false;
thread_local uint64_t t_cpu_at_enter = 0;   // CpuCensus: the host thread's CPU clock at GuestEnter
thread_local int t_isr_depth = 0;            // IsrBegin .. IsrEnd nesting on this host thread
thread_local bool t_preempt_deferred = false;  // a PreemptPoint recorded inside the handler
thread_local bool t_idle = false;              // this host thread holds the idle grant (TryIdleAcquire .. IdleRelease)
}  // namespace

EeRunToken& EeRunToken::Instance() {
    static EeRunToken t;
    return t;
}

uint64_t EeRunToken::NextSeq() { return g_seq.fetch_add(1, std::memory_order_relaxed); }

void EeRunToken::SetPostAcquireHook(void (*fn)()) {
    std::lock_guard<std::mutex> lk(mu_);
    post_acquire_ = fn;
}

void EeRunToken::RunPostAcquire() {
    void (*fn)() = nullptr;
    {
        std::lock_guard<std::mutex> lk(mu_);
        fn = post_acquire_;
    }
    if (fn) fn();
}

EeRunToken::Ready* EeRunToken::FindReady(int tid) {
    for (auto& r : ready_)
        if (r.valid && r.tid == tid) return &r;
    return nullptr;
}

EeRunToken::Ready* EeRunToken::SlotFor(int tid) {
    if (Ready* r = FindReady(tid)) return r;
    for (auto& r : ready_)
        if (!r.valid) {
            r.valid = true;
            r.tid = tid;
            r.seq = 0;
            return &r;
        }
    return nullptr;  // table full: caller treats as no-op
}

bool EeRunToken::IsRetired(int tid) const { return tid >= 0 && tid < kMaxTid && retired_[tid]; }

void EeRunToken::SetRetired(int tid, bool retired) {
    if (tid >= 0 && tid < kMaxTid) retired_[tid] = retired;
}

// Highest priority (lowest value), then oldest seq (FIFO within a priority).
void EeRunToken::ElectLocked() {
    if (hold_election_) {
        owner_ = kNone;
        owner_running_ = false;
        return;
    }
    const Ready* best = nullptr;
    for (const auto& r : ready_) {
        if (!r.valid) continue;
        if (!best || r.prio < best->prio || (r.prio == best->prio && r.seq < best->seq)) best = &r;
    }
    owner_ = best ? best->tid : kNone;
    owner_running_ = false;
}

void EeRunToken::ReleaseLocked(int tid) {
    if (owner_ == tid) {
        if (tid >= 0 && !t_idle) last_owner_ = tid;
        owner_ = kNone;
        owner_running_ = false;
        ElectLocked();
    }
}

void EeRunToken::AcquireSlow(int tid) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return owner_ == tid || shutdown_; });
    owner_running_ = true;
    t_holds = true;
    lk.unlock();
    EeYieldPoint::Instance().NoteSchedulingPoint();   // E432: the thread was scheduled, it is not spinning
}

void EeRunToken::GuestEnter(int tid, int prio) {
    t_tid = tid;
    t_prio = prio;
    t_cpu_at_enter = ps2r::host::ThreadCpuTimeNs();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!t_guest_attached) ++attached_guests_;
        t_guest_attached = true;
        SetRetired(tid, false);  // a host thread is attached from here on
        if (Ready* r = SlotFor(tid)) {
            if (r->seq == 0) r->seq = NextSeq();  // not pre-enqueued by StartThread
            r->prio = prio;
        }
        if (owner_ == kNone) ElectLocked();
    }
    cv_.notify_all();
    AcquireSlow(tid);
    RunPostAcquire();
}

void EeRunToken::GuestExit(int tid) {
    if (t_tid != tid) return;  // only the entering host thread may exit a tid
    if (tid >= 0 && tid < kMaxTid) {
        const uint64_t now = ps2r::host::ThreadCpuTimeNs();
        if (now > t_cpu_at_enter) cpu_ns_[tid].fetch_add(now - t_cpu_at_enter, std::memory_order_relaxed);
        cpu_live_ns_[tid].store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (t_guest_attached) --attached_guests_;
        t_guest_attached = false;
        if (Ready* r = FindReady(tid)) r->valid = false;
        SetRetired(tid, true);  // no host thread: a later MakeReady must not re-enqueue it
        ReleaseLocked(tid);
    }
    cv_.notify_all();
    t_holds = false;
    t_tid = kNone;
}

void EeRunToken::SnapshotCpu() {
    const int tid = t_tid;
    if (tid < 0 || tid >= kMaxTid) return;
    const uint64_t now = ps2r::host::ThreadCpuTimeNs();
    cpu_live_ns_[tid].store(now > t_cpu_at_enter ? now - t_cpu_at_enter : 0, std::memory_order_relaxed);
}

void EeRunToken::BlockBegin(int tid) {
    if (t_tid == tid) SnapshotCpu();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (Ready* r = FindReady(tid)) r->valid = false;
        ReleaseLocked(tid);
    }
    cv_.notify_all();
    t_holds = false;
}

void EeRunToken::BlockEnd(int tid) {
    MakeReady(tid, t_prio);  // no-op when a mutator already re-readied us
    AcquireSlow(tid);
    RunPostAcquire();
}

void EeRunToken::ParkBegin() {
    if (t_tid == kNone) return;
    if (t_park_depth++ > 0) return;
    BlockBegin(t_tid);
}

void EeRunToken::ParkEnd() {
    if (t_tid == kNone) return;
    if (--t_park_depth > 0) return;
    BlockEnd(t_tid);
}

void EeRunToken::WaitVblank() {
    SnapshotCpu();
    if (!t_holds || t_tid == kNone) return;
    const int tid = t_tid;
    uint64_t field_seen = 0, wall_seen = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        bool others = false;
        for (const auto& r : ready_)
            if (r.valid && r.tid != tid) others = true;
        vblank_waits_.fetch_add(1, std::memory_order_relaxed);
        if (!others) {  // idle EE: the vblank is now
            vblank_waits_idle_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        field_seen = field_gen_;
        wall_seen = wall_tick_gen_;
        if (Ready* r = FindReady(tid)) r->valid = false;  // WAITING, not READY (BlockBegin's body)
        ReleaseLocked(tid);
        ++vblank_waiters_;  // an all-blocked EE wakes THIS thread, never the idle grant
        for (auto& w : vblank_waiter_)
            if (w.tid == kNone) {
                w.tid = tid;
                w.prio = t_prio;
                break;
            }
    }
    cv_.notify_all();
    t_holds = false;
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] {
            if (shutdown_ || field_gen_ != field_seen || wall_tick_gen_ != wall_seen) return true;
            for (const auto& r : ready_)
                if (r.valid) return false;
            return true;  // every other thread blocked: idle, the vblank is now
        });
        // Rejoin the ready set under the lock that ends the wait. Between the waiter's
        // leave below and BlockEnd's MakeReady there was an instant with no owner, no
        // READY thread and no vblank waiter: a thread that had just blocked (its
        // IdleWhileBlocked) took the idle grant there and passed a field of its own over
        // this thread's boundary -- one extra idle field (VBON / TIM2 +1, ee_yield isrs
        // +2) in 3 of 12 Paris 600 lockstep runs with the early FINISH, none without it
        // (the window is wider when the logic thread blocks while the worker still runs).
        // The slot assigned here makes BlockEnd's MakeReady the idempotent no-op; a wall
        // tick wake (WakeVblankWaitersForWallTick) assigned it already.
        if (!shutdown_) {
            if (Ready* r = SlotFor(tid)) {
                r->prio = t_prio;
                if (r->seq == 0) r->seq = NextSeq();
            }
        }
        --vblank_waiters_;
        for (auto& w : vblank_waiter_)
            if (w.tid == tid) w.tid = kNone;
    }
    BlockEnd(tid);
}

bool EeRunToken::WakeVblankWaitersForWallTick() {
    if (!t_holds || t_idle) return false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (vblank_waiters_ == 0) return false;
        ++wall_tick_gen_;
        // READY now, from the holder's side: the holder's PreemptPoint sees the
        // waiter at once (its own BlockEnd's MakeReady is then the idempotent no-op).
        for (const auto& w : vblank_waiter_) {
            if (w.tid == kNone) continue;
            Ready* r = SlotFor(w.tid);
            if (!r) continue;
            r->prio = w.prio;
            if (r->seq == 0) r->seq = NextSeq();
        }
    }
    wall_tick_wakes_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_all();
    return true;
}

uint32_t EeRunToken::VblankWaiters() {
    std::lock_guard<std::mutex> lk(mu_);
    return vblank_waiters_;
}

bool EeRunToken::TryIdleAcquire(int tid) {
    if (tid == kNone || tid == kHostTid || t_holds || t_idle) return false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (shutdown_ || hold_election_ || owner_ != kNone || vblank_waiters_ != 0) return false;
        for (const auto& r : ready_)
            if (r.valid) return false;   // something to schedule: not idle
        owner_ = tid;
        owner_running_ = true;
    }
    t_holds = true;
    t_idle = true;
    idle_grants_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void EeRunToken::IdleRelease(int tid) {
    if (!t_idle) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (owner_ == tid) {
            owner_ = kNone;
            owner_running_ = false;
            ElectLocked();   // whatever the field readied runs now
        }
    }
    cv_.notify_all();
    t_holds = false;
    t_idle = false;
}

bool EeRunToken::CurrentThreadIdling() const { return t_idle; }

void EeRunToken::NoteFieldElapsed() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++field_gen_;
    }
    cv_.notify_all();
}

void EeRunToken::MakeReady(int tid, int prio) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        Ready* r = FindReady(tid);
        if (!r) {
            if (IsRetired(tid)) {  // exited, no runner attached: electing it would wedge the token
                retired_ready_noops_++;
                return;
            }
            r = SlotFor(tid);
            if (!r) return;
            r->prio = prio;
            r->seq = NextSeq();  // FIFO position = the readying kernel event
        }
        if (owner_ == kNone) ElectLocked();
    }
    cv_.notify_all();
}

uint64_t EeRunToken::RetiredReadyNoops() {
    std::lock_guard<std::mutex> lk(mu_);
    return retired_ready_noops_;
}

void EeRunToken::IsrBegin() { ++t_isr_depth; }

void EeRunToken::IsrEnd() {
    if (t_isr_depth > 0) --t_isr_depth;
    if (t_isr_depth != 0 || !t_preempt_deferred) return;
    t_preempt_deferred = false;
    PreemptPoint();  // the handler's exit: the one dispatch edge of the interrupt
}

bool EeRunToken::InIsr() const { return t_isr_depth > 0; }

void EeRunToken::PreemptPoint() {
    if (!t_holds || t_idle) return;   // the idle runner hands over at IdleRelease, never mid-field
    const int tid = t_tid;
    if (tid == kNone) return;
    if (t_isr_depth > 0) {
        t_preempt_deferred = true;
        return;
    }
    bool yield = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const Ready* self = FindReady(tid);
        const int my_prio = self ? self->prio : t_prio;
        for (const auto& r : ready_)
            if (r.valid && r.tid != tid && r.prio < my_prio) {
                yield = true;
                break;
            }
        if (yield) ReleaseLocked(tid);  // slot/seq kept: resume at the head of our priority
    }
    if (!yield) return;
    cv_.notify_all();
    t_holds = false;
    AcquireSlow(tid);
    RunPostAcquire();
}

void EeRunToken::Rotate(int rot_prio) {
    if (!t_holds || t_idle) return;
    const int tid = t_tid;
    if (tid == kNone) return;
    int my_prio = t_prio;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (const Ready* self = FindReady(tid)) my_prio = self->prio;
    }
    if (rot_prio == my_prio) {
        bool other = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (Ready* self = FindReady(tid)) self->seq = NextSeq();  // self to tail
            for (const auto& r : ready_)
                if (r.valid && r.tid != tid && r.prio == my_prio) other = true;
            if (other) ReleaseLocked(tid);
        }
        if (!other) return;
        cv_.notify_all();
        t_holds = false;
        AcquireSlow(tid);
        RunPostAcquire();
        return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    Ready* head = nullptr;
    for (auto& r : ready_) {
        if (!r.valid || r.prio != rot_prio) continue;
        if (!head || r.seq < head->seq) head = &r;
    }
    if (head) head->seq = NextSeq();  // head -> tail
}

void EeRunToken::SetPriority(int tid, int prio) {
    if (tid == t_tid) t_prio = prio;
    std::lock_guard<std::mutex> lk(mu_);
    if (Ready* r = FindReady(tid)) {
        r->prio = prio;
        r->seq = NextSeq();  // tail of the NEW priority's ready queue
    }
}

void EeRunToken::Drop(int tid) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (Ready* r = FindReady(tid)) r->valid = false;
        ReleaseLocked(tid);
    }
    cv_.notify_all();
}

void EeRunToken::BeginRelaunch() {
    const int self = t_tid;
    {
        std::lock_guard<std::mutex> lk(mu_);
        hold_election_ = true;  // hold FIRST: the self-exit below elects nobody
    }
    if (self != kNone && self != kHostTid) GuestExit(self);
    std::lock_guard<std::mutex> lk(mu_);
    owner_ = kNone;
    owner_running_ = false;
}

void EeRunToken::EndRelaunch() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        hold_election_ = false;
        ElectLocked();
    }
    cv_.notify_all();
}

void EeRunToken::Shutdown() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

bool EeRunToken::RestartAfterJoin(std::string* error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!shutdown_ || attached_guests_ || attached_hosts_ || t_holds || t_guest_attached) {
        if (error) *error = "Execution token still has an active owner or has not been stopped.";
        return false;
    }
    for (auto& ready : ready_) ready = Ready{};
    for (auto& retired : retired_) retired = false;
    owner_ = kNone;
    last_owner_ = kNone;
    owner_running_ = false;
    hold_election_ = false;
    shutdown_ = false;
    t_tid = kNone;
    t_prio = 0;
    t_park_depth = 0;
    t_idle = false;
    vblank_waiters_ = 0;
    // Scheduling sequence and diagnostic counters remain monotonic.
    if (error) error->clear();
    return true;
}

bool EeRunToken::CurrentThreadHoldsToken() const { return t_holds; }
bool EeRunToken::CanCaptureCurrentThread() {
    std::lock_guard<std::mutex> lock(mu_);
    return t_holds && t_tid > 0 && t_park_depth == 0 && !shutdown_ &&
        !hold_election_ && owner_ == t_tid && owner_running_;
}
int EeRunToken::CurrentTid() const { return t_tid; }

bool EeRunToken::ElectionHeld() {
    std::lock_guard<std::mutex> lk(mu_);
    return hold_election_;
}

int EeRunToken::OwnerTid() {
    std::lock_guard<std::mutex> lk(mu_);
    return owner_;
}

int EeRunToken::LastOwnerTid() {
    std::lock_guard<std::mutex> lk(mu_);
    return last_owner_;
}

int EeRunToken::OwnerForTest() {
    std::lock_guard<std::mutex> lk(mu_);
    return owner_;
}

bool EeRunToken::AnyReadyBesides(int tid) {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& r : ready_)
        if (r.valid && r.tid != tid) return true;
    return false;
}

void EeRunToken::ResetForTest() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& r : ready_) r = Ready{};
    for (auto& r : retired_) r = false;
    retired_ready_noops_ = 0;
    owner_ = kNone;
    last_owner_ = kNone;
    owner_running_ = false;
    shutdown_ = false;
    hold_election_ = false;
    attached_guests_ = attached_hosts_ = 0;
    for (auto& w : vblank_waiter_) w = VblankWaiter{};
    vblank_waiters_ = 0;
    t_tid = kNone;
    t_holds = false;
    t_park_depth = 0;
    t_guest_attached = false;
    t_isr_depth = 0;
    t_preempt_deferred = false;
    t_idle = false;
    vblank_waiters_ = 0;
}

EeRunToken::HostGate::HostGate() {
    EeRunToken& t = Instance();
    t.host_gate_mu_.lock();
    {
        std::lock_guard<std::mutex> lock(t.mu_);
        ++t.attached_hosts_;
    }
    t_tid = kHostTid;
    t_prio = kHostPrio;
    t.MakeReady(kHostTid, kHostPrio);
    t.AcquireSlow(kHostTid);
}

EeRunToken::HostGate::~HostGate() {
    EeRunToken& t = Instance();
    {
        std::lock_guard<std::mutex> lk(t.mu_);
        --t.attached_hosts_;
        if (Ready* r = t.FindReady(kHostTid)) r->valid = false;
        t.ReleaseLocked(kHostTid);
    }
    t.cv_.notify_all();
    t_holds = false;
    t_tid = kNone;
    t_prio = 0;
    t.host_gate_mu_.unlock();
}

EeRunToken::ParkScope::ParkScope() {
    if (t_tid == kNone || t_tid == kHostTid) return;
    armed_ = true;
    Instance().ParkBegin();
}

EeRunToken::ParkScope::~ParkScope() {
    if (armed_) Instance().ParkEnd();
}

uint64_t EeRunToken::CpuNs(int tid) const {
    if (tid < 0 || tid >= kMaxTid) return 0;
    return cpu_ns_[tid].load(std::memory_order_relaxed) + cpu_live_ns_[tid].load(std::memory_order_relaxed);
}

std::string EeRunToken::CpuCensus() const {
    std::string out;
    uint64_t total = 0;
    for (int tid = 0; tid < kMaxTid; ++tid) {
        const uint64_t ns = CpuNs(tid);
        if (ns < 1000000u) continue;   // below a millisecond: the one-shot threads
        total += ns;
        if (!out.empty()) out += ',';
        out += std::to_string(tid) + ':' + std::to_string(ns / 1000000u);
    }
    out += ";total=" + std::to_string(total / 1000000u);
    return out;
}
