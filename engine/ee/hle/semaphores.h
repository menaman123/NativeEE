#pragma once
// Semaphores: the EE kernel semaphore table (thsemap, SDK 3.0.1).
//
// thbase/thsemap rules implemented:
//   S1 "CreateSema: count = init (clamped 0..max), max >= 1. Ids are positive."
//   S2 "SignalSema: if threads wait on the semaphore, the HEAD of the wait
//       queue is released and given the resource (count stays 0); the queue is
//       FIFO by default and priority-ordered under SA_THPRI (attr bit 0).
//       Otherwise count++, and KE_SEMA_OVF when count == max."
//   S3 "WaitSema: count > 0 -> count--, return; else the caller WAITs (TSW_SEMA)
//       until released. A forced release returns KE_RELEASE_WAIT; deletion of
//       the semaphore returns KE_WAIT_DELETE to every waiter."
//   S4 "PollSema: count > 0 -> count--, KE_OK; else KE_SEMA_ZERO (never blocks)."
//   S5 "iSignalSema / iPollSema share the bodies (interrupt-context routing
//       is the EeEventQueue predicate)."
// Blocking waits release the run token across the park (BlockBegin/BlockEnd
// with no object lock held at the re-acquire) and register the park so a
// queued foreign wake pokes the waiter to drain.
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ee/hle/kernel_codes.h"

struct SemaParam {
    uint32_t attr = 0;
    int max_count = 1;
    int init_count = 0;
    uint32_t option = 0;
};

struct SemaStatus {
    int count = 0;
    int max_count = 0;
    int init_count = 0;
    int wait_threads = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
};

struct SemaInfo {
    int count = 0;
    int max_count = 0;
    int init_count = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
    bool deleted = false;
    struct Waiter {
        int tid;
        int prio;
        uint64_t seq;
    };
    std::vector<Waiter> waitq;  // guarded by m
    std::mutex m;
    std::condition_variable cv;
};

class Semaphores {
public:
    static Semaphores& Instance();

    int Create(const SemaParam& p);
    int Delete(int sid);
    int Signal(int sid);
    int Wait(int sid);
    int Poll(int sid);
    int ReferStatus(int sid, SemaStatus& out);

    std::shared_ptr<SemaInfo> Lookup(int sid);
    // ReleaseWaitThread / termination: forget a waiter so a later Signal cannot
    // hand it the count.
    void RemoveWaiter(int sid, int tid);
    void ResetForTest();
    // Savestate seams: every record ascending id; InstallTable replaces the
    // table (records only; no waiter may be live) and continues id
    // allocation above the highest installed id.
    using TableEntry = std::pair<int, std::shared_ptr<SemaInfo>>;
    std::vector<TableEntry> SnapshotTable();
    void InstallTable(const std::vector<TableEntry>& entries);

private:
    Semaphores() = default;
    std::mutex map_mu_;
    std::unordered_map<int, std::shared_ptr<SemaInfo>> semas_;
    int next_id_ = 1;
};
