// EeEventQueue implementation; the two BUG-035 halves are in the header.
#include "engine/ee/sched/ee_event_queue.h"

#include "engine/ee/sched/ee_run_token.h"

namespace {
thread_local bool t_applying = false;
}  // namespace

EeEventQueue& EeEventQueue::Instance() {
    static EeEventQueue q;
    return q;
}

// The post-acquire hook is installed once: after every token acquire the new
// holder consumes queued wakes -- interrupt delivery at the scheduling point.
EeEventQueue::EeEventQueue() {
    EeRunToken::Instance().SetPostAcquireHook(+[] { EeEventQueue::Instance().DrainIfPendingOnHolder(); });
}

bool EeEventQueue::ApplyingOnThisThread() { return t_applying; }

bool EeEventQueue::CallerMayMutate() {
    return EeRunToken::Instance().CurrentThreadHoldsToken() || t_applying;
}

bool EeEventQueue::RouteIfForeign(const Event& ev) {
    if (CallerMayMutate()) return false;
    Enqueue(ev);
    return true;
}

void EeEventQueue::Enqueue(const Event& ev) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (count_ < kMaxEvents) {
            ring_[(head_ + count_) % kMaxEvents] = ev;
            count_++;
        } else {
            ring_full_drops_.fetch_add(1, std::memory_order_relaxed);
        }
        pending_.store(true, std::memory_order_release);
    }
    PokeParks();
}

void EeEventQueue::PokeParks() {
    // The holder (if any) applies the event at its next scheduling point: only
    // its own parks need the wake (the boundary protocol's field park, a park
    // inside an idle grant). Blocked threads stay blocked. With no holder ONE
    // park wakes: the last runner's, else the lowest guest tid's (header).
    EeRunToken& token = EeRunToken::Instance();
    const int owner = token.OwnerTid();
    std::lock_guard<std::mutex> plk(park_mu_);
    int target = owner;
    if (owner == EeRunToken::kNone) {
        pokes_all_.fetch_add(1, std::memory_order_relaxed);
        const int last = token.LastOwnerTid();
        bool last_parked = false;
        int lowest = EeRunToken::kNone;   // lowest GUEST tid with a park
        for (const auto& p : parks_) {
            if (!p.active) continue;
            if (p.tid == last) last_parked = true;
            if (p.tid >= 0 && (lowest == EeRunToken::kNone || p.tid < lowest)) lowest = p.tid;
        }
        // Neither: only non-guest registrants (tid kNone) are parked -- the loop
        // below pokes them all (target kNone matches each).
        target = last_parked ? last : lowest;
        last_idle_poke_tid_.store(target, std::memory_order_relaxed);
    } else {
        pokes_holder_.fetch_add(1, std::memory_order_relaxed);
    }
    for (auto& p : parks_) {
        if (!p.active || p.tid != target) continue;
        // Empty-lock + notify: a parked waiter either sees pending_ in its
        // predicate or receives this notification.
        { std::lock_guard<std::mutex> olk(*p.m); }
        p.cv->notify_all();
    }
}

bool EeEventQueue::ParkWakeDue() const {
    if (!pending_.load(std::memory_order_acquire)) return false;
    EeRunToken& token = EeRunToken::Instance();
    const int owner = token.OwnerTid();
    return owner == EeRunToken::kNone || owner == token.CurrentTid();
}

void EeEventQueue::PokeIfOrphaned() {
    if (!pending_.load(std::memory_order_acquire)) return;
    if (EeRunToken::Instance().OwnerTid() != EeRunToken::kNone) return;
    PokeParks();
}

void EeEventQueue::DrainOnToken() {
    // (b): applications take the direct bodies for the whole drain; nesting-safe
    // (a nested drain from a scheduling point inside an applied mutator restores
    // the outer value on exit).
    const bool prev_applying = t_applying;
    t_applying = true;
    for (;;) {
        Event ev;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (count_ == 0) {
                pending_.store(false, std::memory_order_release);
                break;
            }
            ev = ring_[head_];
            head_ = (head_ + 1) % kMaxEvents;
            count_--;
        }
        Applier fn;
        {
            std::lock_guard<std::mutex> alk(applier_mu_);
            fn = applier_;
        }
        if (fn) fn(ev);
    }
    t_applying = prev_applying;
}

void EeEventQueue::DrainIfPendingOnHolder() {
    if (!EeRunToken::Instance().CurrentThreadHoldsToken()) {
        nonholder_drains_.fetch_add(1, std::memory_order_relaxed);  // (a): refused, nothing lost
        return;
    }
    if (!HasPending()) return;
    DrainOnToken();
}

uint64_t EeEventQueue::NonHolderDrainRequests() const {
    return nonholder_drains_.load(std::memory_order_relaxed);
}

void EeEventQueue::SetApplier(Applier fn) {
    std::lock_guard<std::mutex> alk(applier_mu_);
    applier_ = std::move(fn);
}

int EeEventQueue::RegisterPark(std::mutex* m, std::condition_variable* cv) {
    std::lock_guard<std::mutex> plk(park_mu_);
    for (int i = 0; i < kMaxParks; i++) {
        if (!parks_[i].active) {
            parks_[i] = ParkSlot{m, cv, EeRunToken::Instance().CurrentTid(), true};
            return i;
        }
    }
    return -1;
}

void EeEventQueue::UnregisterPark(int slot) {
    if (slot < 0 || slot >= kMaxParks) return;
    std::lock_guard<std::mutex> plk(park_mu_);
    parks_[slot] = ParkSlot{};
}

size_t EeEventQueue::Size() {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<size_t>(count_);
}

EeEventQueue::ParkReg::ParkReg(std::mutex* m, std::condition_variable* cv)
    : slot_(EeEventQueue::Instance().RegisterPark(m, cv)) {}

EeEventQueue::ParkReg::~ParkReg() { EeEventQueue::Instance().UnregisterPark(slot_); }
