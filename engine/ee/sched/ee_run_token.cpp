// EeRunToken implementation; the model and the thbase rule are in the header.
#include "engine/ee/sched/ee_run_token.h"

#include <atomic>

namespace {
std::atomic<uint64_t> g_seq{1};
thread_local int t_tid = EeRunToken::kNone;
thread_local int t_prio = 0;
thread_local bool t_holds = false;
thread_local int t_park_depth = 0;
thread_local bool t_guest_attached = false;
thread_local int t_isr_depth = 0;            // IsrBegin .. IsrEnd nesting on this host thread
thread_local bool t_preempt_deferred = false;  // a PreemptPoint recorded inside the handler
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
}

void EeRunToken::GuestEnter(int tid, int prio) {
    t_tid = tid;
    t_prio = prio;
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

void EeRunToken::BlockBegin(int tid) {
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
    if (!t_holds || t_tid == kNone) return;
    const int tid = t_tid;
    uint64_t field_seen = 0;
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
        if (Ready* r = FindReady(tid)) r->valid = false;  // WAITING, not READY (BlockBegin's body)
        ReleaseLocked(tid);
    }
    cv_.notify_all();
    t_holds = false;
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] {
            if (shutdown_ || field_gen_ != field_seen) return true;
            for (const auto& r : ready_)
                if (r.valid) return false;
            return true;  // every other thread blocked: idle, the vblank is now
        });
    }
    BlockEnd(tid);
}

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
    if (!t_holds) return;
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
    if (!t_holds) return;
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
    owner_running_ = false;
    hold_election_ = false;
    shutdown_ = false;
    t_tid = kNone;
    t_prio = 0;
    t_park_depth = 0;
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
    owner_running_ = false;
    shutdown_ = false;
    hold_election_ = false;
    attached_guests_ = attached_hosts_ = 0;
    t_tid = kNone;
    t_holds = false;
    t_park_depth = 0;
    t_guest_attached = false;
    t_isr_depth = 0;
    t_preempt_deferred = false;
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
