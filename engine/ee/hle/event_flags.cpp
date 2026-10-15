// EventFlags implementation; the thevent rules E1..E6 are in the header.
#include "engine/ee/hle/event_flags.h"

#include <algorithm>

#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"

EventFlags& EventFlags::Instance() {
    static EventFlags e;
    return e;
}

bool EventFlags::Satisfied(uint32_t pattern, uint32_t wait_bits, uint32_t mode) {
    return (mode & WEF_OR) ? ((pattern & wait_bits) != 0) : ((pattern & wait_bits) == wait_bits);
}

uint32_t EventFlags::AfterRelease(uint32_t pattern, uint32_t wait_bits, uint32_t mode) {
    if (mode & WEF_CLEAR_ALL) return 0;
    if (mode & WEF_CLEAR) return pattern & ~wait_bits;
    return pattern;
}

std::shared_ptr<EventFlagInfo> EventFlags::Lookup(int eid) {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = flags_.find(eid);
    return (it != flags_.end()) ? it->second : nullptr;
}

int EventFlags::Create(const EventFlagParam& p) {
    auto info = std::make_shared<EventFlagInfo>();
    info->attr = p.attr;
    info->option = p.option;
    info->init_bits = p.init_bits;
    info->bits = p.init_bits;
    std::lock_guard<std::mutex> lk(map_mu_);
    const int id = next_id_++;
    flags_[id] = info;
    return id;
}

int EventFlags::Delete(int eid) {
    std::shared_ptr<EventFlagInfo> info;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        auto it = flags_.find(eid);
        if (it == flags_.end()) return KE_UNKNOWN_EVFID;
        info = it->second;
        flags_.erase(it);
    }
    std::vector<EventFlagInfo::Waiter> rel;
    {
        std::lock_guard<std::mutex> lk(info->m);
        info->deleted = true;  // E6
        rel = info->waitq;
    }
    info->cv.notify_all();
    for (const auto& w : rel) EeRunToken::Instance().MakeReady(w.tid, w.prio);
    return KE_OK;
}

int EventFlags::Set(int eid, uint32_t bits) {
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kSetEventFlag, eid, bits})) return KE_OK;
    auto info = Lookup(eid);
    if (!info) return KE_UNKNOWN_EVFID;
    if (bits == 0) return KE_OK;
    struct Rel {
        int tid;
        int prio;
    };
    std::vector<Rel> released;
    {
        std::lock_guard<std::mutex> lk(info->m);
        info->bits |= bits;
        // E3: scan in queue order (FIFO / priority), release + clear at once.
        std::vector<EventFlagInfo::Waiter> q = info->waitq;
        const bool by_prio = (info->attr & SA_THPRI) != 0;
        std::sort(q.begin(), q.end(), [&](const EventFlagInfo::Waiter& a, const EventFlagInfo::Waiter& b) {
            if (by_prio) return a.prio < b.prio || (a.prio == b.prio && a.seq < b.seq);
            return a.seq < b.seq;
        });
        for (const auto& w : q) {
            if (!Satisfied(info->bits, w.wait_bits, w.mode)) continue;
            if (auto t = ThreadManager::Instance().Lookup(w.tid)) {
                std::lock_guard<std::mutex> tl(t->m);
                t->evf_result = info->bits;
                t->released.store(true, std::memory_order_release);
            }
            info->bits = AfterRelease(info->bits, w.wait_bits, w.mode);
            released.push_back({w.tid, w.prio});
            for (auto it = info->waitq.begin(); it != info->waitq.end(); ++it)
                if (it->tid == w.tid) {
                    info->waitq.erase(it);
                    break;
                }
        }
        info->waiters -= static_cast<int>(released.size());
    }
    info->cv.notify_all();
    for (const auto& r : released) EeRunToken::Instance().MakeReady(r.tid, r.prio);
    if (!released.empty()) EeRunToken::Instance().PreemptPoint();
    return KE_OK;
}

int EventFlags::Clear(int eid, uint32_t keep_mask) {
    if (EeEventQueue::Instance().RouteIfForeign({EeEventQueue::Event::kClearEventFlag, eid, keep_mask})) return KE_OK;
    auto info = Lookup(eid);
    if (!info) return KE_UNKNOWN_EVFID;
    {
        std::lock_guard<std::mutex> lk(info->m);
        info->bits &= keep_mask;  // E4
    }
    info->cv.notify_all();
    return KE_OK;
}

int EventFlags::Wait(int eid, uint32_t wait_bits, uint32_t mode, uint32_t* result) {
    if ((mode & ~WEF_MODE_MASK) != 0) return KE_ILLEGAL_MODE;  // E2
    if (wait_bits == 0) return KE_EVF_ILPAT;
    auto info = Lookup(eid);
    if (!info) return KE_UNKNOWN_EVFID;
    ThreadManager& tm = ThreadManager::Instance();
    auto t = tm.Current();
    tm.ThrowIfTerminated(t);
    const int tid = tm.CurrentTid();
    EeEventQueue::Instance().DrainIfPendingOnHolder();
    int ret = KE_OK;
    for (;;) {
        bool satisfied = false;
        {
            std::lock_guard<std::mutex> lk(info->m);
            if (info->deleted) {
                ret = KE_WAIT_DELETE;
                break;
            }
            if ((info->attr & EA_MULTI) == 0 && info->waiters > 0) return KE_EVF_MULTI;  // E1
            if (Satisfied(info->bits, wait_bits, mode)) {
                if (result) *result = info->bits;
                info->bits = AfterRelease(info->bits, wait_bits, mode);
                satisfied = true;
            } else {
                tm.EnterObjectWait(t, TSW_EVENT, eid);
                info->waiters++;
                info->waitq.push_back({tid, tm.PriorityOf(t), EeRunToken::NextSeq(), wait_bits, mode});
            }
        }
        if (satisfied) break;
        tm.NotePark(tid, true);
        EeRunToken::Instance().BlockBegin(tid);
        {
            EeEventQueue::ParkReg reg(&info->m, &info->cv);
            std::unique_lock<std::mutex> lk(info->m);
            info->cv.wait(lk, [&] {
                return (t && (t->released.load(std::memory_order_acquire) || t->force_release.load() ||
                              t->terminated.load())) ||
                       info->deleted || EeEventQueue::Instance().HasPending() || !tm.Running();
            });
            for (auto it = info->waitq.begin(); it != info->waitq.end(); ++it)
                if (it->tid == tid) {
                    info->waitq.erase(it);
                    info->waiters--;
                    break;
                }
        }
        tm.NotePark(tid, false);
        EeRunToken::Instance().BlockEnd(tid);
        if (t && t->terminated.load()) throw ThreadExitUnwind{};
        if (t && t->released.load(std::memory_order_acquire)) {
            t->released.store(false, std::memory_order_relaxed);
            if (result) {
                std::lock_guard<std::mutex> tl(t->m);
                *result = t->evf_result;  // pattern captured at release (E3)
            }
            ret = KE_OK;
            break;
        }
        if (t && t->force_release.load()) {
            t->force_release = false;
            ret = KE_RELEASE_WAIT;
            break;
        }
        {
            std::lock_guard<std::mutex> lk(info->m);
            if (info->deleted) {
                ret = KE_WAIT_DELETE;
                break;
            }
        }
        if (!tm.Running()) break;
        // Spurious wake (queue poke): re-check / re-enrol.
    }
    tm.LeaveObjectWait(t);
    tm.WaitWhileSuspended(t);
    return ret;
}

int EventFlags::Poll(int eid, uint32_t wait_bits, uint32_t mode, uint32_t* result) {
    if ((mode & ~WEF_MODE_MASK) != 0) return KE_ILLEGAL_MODE;
    if (wait_bits == 0) return KE_EVF_ILPAT;
    EeEventQueue::Instance().DrainIfPendingOnHolder();
    auto info = Lookup(eid);
    if (!info) return KE_UNKNOWN_EVFID;
    std::lock_guard<std::mutex> lk(info->m);
    if ((info->attr & EA_MULTI) == 0 && info->waiters > 0) return KE_EVF_MULTI;
    if (!Satisfied(info->bits, wait_bits, mode)) return KE_EVF_COND;  // E5
    if (result) *result = info->bits;
    info->bits = AfterRelease(info->bits, wait_bits, mode);
    return KE_OK;
}

void EventFlags::RemoveWaiter(int eid, int tid) {
    auto info = Lookup(eid);
    if (!info) return;
    std::lock_guard<std::mutex> lk(info->m);
    for (auto it = info->waitq.begin(); it != info->waitq.end(); ++it)
        if (it->tid == tid) {
            info->waitq.erase(it);
            info->waiters--;
            break;
        }
}

std::vector<EventFlags::TableEntry> EventFlags::SnapshotTable() {
    std::vector<TableEntry> out;
    {
        std::lock_guard<std::mutex> lk(map_mu_);
        for (const auto& kv : flags_) out.emplace_back(kv.first, kv.second);
    }
    std::sort(out.begin(), out.end(), [](const TableEntry& a, const TableEntry& b) { return a.first < b.first; });
    return out;
}

void EventFlags::InstallTable(const std::vector<TableEntry>& entries) {
    std::lock_guard<std::mutex> lk(map_mu_);
    flags_.clear();
    int max_id = 0;
    for (const TableEntry& e : entries) {
        flags_[e.first] = e.second;
        if (e.first > max_id) max_id = e.first;
    }
    next_id_ = max_id + 1;
}

void EventFlags::ResetForTest() {
    std::lock_guard<std::mutex> lk(map_mu_);
    flags_.clear();
    next_id_ = 1;
}
