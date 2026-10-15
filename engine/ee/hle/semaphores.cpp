// Semaphores implementation; the thsemap rules S1..S5 are in the header.
#include "engine/ee/hle/semaphores.h"

#include <algorithm>

#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

Semaphores& Semaphores::Instance() {
    static Semaphores s;
    return s;
}

std::shared_ptr<SemaInfo> Semaphores::Lookup(int sid) {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = semas_.find(sid);
    return (it != semas_.end()) ? it->second : nullptr;
}

int Semaphores::Create(const SemaParam& p) {
    int max = p.max_count <= 0 ? 1 : p.max_count;  // S1
    int init = p.init_count < 0 ? 0 : p.init_count;
    if (init > max) init = max;
    auto info = std::make_shared<SemaInfo>();
    info->count = init;
    info->max_count = max;
    info->init_count = init;
    info->attr = p.attr;
    info->option = p.option;
    std::lock_guard<std::mutex> lk(map_mu_);
    for (int attempts = 0; attempts < 0x7FFF; ++attempts) {
        if (next_id_ <= 0) next_id_ = 1;
        const int candidate = next_id_++;
        if (semas_.find(candidate) == semas_.end()) {
            semas_.emplace(candidate, info);
            return candidate;
        }
    }
    return KE_ERROR;
}

int Semaphores::Delete(int sid) {
    std::shared_ptr<SemaInfo> sema;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        auto it = semas_.find(sid);
        if (it == semas_.end()) return KE_UNKNOWN_SEMID;
        sema = it->second;
        semas_.erase(it);
    }
    std::vector<SemaInfo::Waiter> rel;
    {
        std::lock_guard<std::mutex> lk(sema->m);
        sema->deleted = true;  // S3: every waiter returns KE_WAIT_DELETE
        rel = sema->waitq;
    }
    sema->cv.notify_all();
    for (const auto& w : rel) EeRunToken::Instance().MakeReady(w.tid, w.prio);
    return KE_OK;
}

int Semaphores::Signal(int sid) {
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kSignalSema, sid, 0})) return KE_OK;
    auto sema = Lookup(sid);
    if (!sema) return KE_UNKNOWN_SEMID;
    int ret = KE_OK;
    int released_tid = 0, released_prio = 0;
    {
        std::lock_guard<std::mutex> lk(sema->m);
        if (!sema->waitq.empty()) {
            // S2: head of the wait queue -- FIFO, or priority under SA_THPRI.
            const bool by_prio = (sema->attr & SA_THPRI) != 0;
            size_t best = 0;
            for (size_t i = 1; i < sema->waitq.size(); i++) {
                const auto& a = sema->waitq[i];
                const auto& b = sema->waitq[best];
                const bool better = by_prio ? (a.prio < b.prio || (a.prio == b.prio && a.seq < b.seq)) : (a.seq < b.seq);
                if (better) best = i;
            }
            released_tid = sema->waitq[best].tid;
            released_prio = sema->waitq[best].prio;
            sema->waitq.erase(sema->waitq.begin() + static_cast<long>(best));
            if (auto w = ThreadManager::Instance().Lookup(released_tid)) w->released.store(true, std::memory_order_release);
            sema->cv.notify_all();
        } else if (sema->count >= sema->max_count) {
            ret = KE_SEMA_OVF;
        } else {
            sema->count++;
            sema->cv.notify_one();
        }
    }
    if (released_tid != 0) {
        EeRunToken::Instance().MakeReady(released_tid, released_prio);
        EeRunToken::Instance().PreemptPoint();  // dispatch edge
    }
    return ret;
}

int Semaphores::Wait(int sid) {
    auto sema = Lookup(sid);
    if (!sema) return KE_UNKNOWN_SEMID;
    ThreadManager& tm = ThreadManager::Instance();
    auto info = tm.Current();
    tm.ThrowIfTerminated(info);
    const int tid = tm.CurrentTid();
    EeEventQueue::Instance().DrainIfPendingOnHolder();  // syscall-boundary delivery
    int ret = KE_OK;
    for (;;) {
        bool consumed = false;
        {
            std::lock_guard<std::mutex> lk(sema->m);
            if (sema->deleted) {
                ret = KE_WAIT_DELETE;
                break;
            }
            if (sema->count > 0) {
                sema->count--;
                consumed = true;
            } else {
                tm.EnterObjectWait(info, TSW_SEMA, sid);
                sema->waitq.push_back({tid, tm.PriorityOf(info), EeRunToken::NextSeq()});
            }
        }
        if (consumed) break;
        tm.NotePark(tid, true);
        EeRunToken::Instance().BlockBegin(tid);
        {
            EeEventQueue::ParkReg reg(&sema->m, &sema->cv);  // registered before the object lock
            std::unique_lock<std::mutex> lk(sema->m);
            sema->cv.wait(lk, [&] {
                return (info && (info->released.load(std::memory_order_acquire) || info->force_release.load() ||
                                 info->terminated.load())) ||
                       sema->deleted || sema->count > 0 || EeEventQueue::Instance().HasPending() || !tm.Running();
            });
            for (auto it = sema->waitq.begin(); it != sema->waitq.end(); ++it)
                if (it->tid == tid) {
                    sema->waitq.erase(it);
                    break;
                }
        }
        tm.NotePark(tid, false);
        EeRunToken::Instance().BlockEnd(tid);  // re-acquire with no locks held
        if (info && info->terminated.load()) throw ThreadExitUnwind{};
        if (info && info->released.load(std::memory_order_acquire)) {
            info->released.store(false, std::memory_order_relaxed);
            ret = KE_OK;  // the count was handed to us by the signaller
            break;
        }
        if (info && info->force_release.load()) {
            info->force_release = false;
            ret = KE_RELEASE_WAIT;
            break;
        }
        {
            std::lock_guard<std::mutex> lk(sema->m);
            if (sema->deleted) {
                ret = KE_WAIT_DELETE;
                break;
            }
        }
        if (!tm.Running()) break;
        // Spurious wake (queue poke): re-check.
    }
    tm.LeaveObjectWait(info);
    tm.WaitWhileSuspended(info);
    return ret;
}

int Semaphores::Poll(int sid) {
    EeEventQueue::Instance().DrainIfPendingOnHolder();  // poll = scheduling point
    auto sema = Lookup(sid);
    if (!sema) return KE_UNKNOWN_SEMID;
    std::lock_guard<std::mutex> lk(sema->m);
    if (sema->count > 0) {
        sema->count--;
        return KE_OK;
    }
    return KE_SEMA_ZERO;  // S4
}

int Semaphores::ReferStatus(int sid, SemaStatus& out) {
    auto sema = Lookup(sid);
    if (!sema) return KE_UNKNOWN_SEMID;
    std::lock_guard<std::mutex> lk(sema->m);
    out.count = sema->count;
    out.max_count = sema->max_count;
    out.init_count = sema->init_count;
    out.wait_threads = static_cast<int>(sema->waitq.size());
    out.attr = sema->attr;
    out.option = sema->option;
    return KE_OK;
}

void Semaphores::RemoveWaiter(int sid, int tid) {
    auto sema = Lookup(sid);
    if (!sema) return;
    std::lock_guard<std::mutex> lk(sema->m);
    for (auto it = sema->waitq.begin(); it != sema->waitq.end(); ++it)
        if (it->tid == tid) {
            sema->waitq.erase(it);
            break;
        }
}

std::vector<Semaphores::TableEntry> Semaphores::SnapshotTable() {
    std::vector<TableEntry> out;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        for (const auto& kv : semas_) out.emplace_back(kv.first, kv.second);
    }
    std::sort(out.begin(), out.end(), [](const TableEntry& a, const TableEntry& b) { return a.first < b.first; });
    return out;
}

void Semaphores::InstallTable(const std::vector<TableEntry>& entries) {
    std::lock_guard<std::mutex> lk(map_mu_);
    semas_.clear();
    int max_id = 0;
    for (const TableEntry& e : entries) {
        semas_[e.first] = e.second;
        if (e.first > max_id) max_id = e.first;
    }
    next_id_ = max_id + 1;
}

void Semaphores::ResetForTest() {
    std::lock_guard<std::mutex> lk(map_mu_);
    semas_.clear();
    next_id_ = 1;
}
