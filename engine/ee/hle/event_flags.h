#pragma once
// EventFlags: the EE kernel event-flag table (thevent, SDK 3.0.1).
//
// Rules implemented:
//   E1 "CreateEventFlag: bits = init pattern; attr EA_MULTI (0x2) permits more
//       than one waiter, otherwise a second waiter gets KE_EVF_MULTI."
//   E2 "WaitEventFlag(bits, mode): mode must be within WEF_OR|WEF_CLEAR|
//       WEF_CLEAR_ALL (else KE_ILLEGAL_MODE); bits == 0 is KE_EVF_ILPAT.
//       Satisfied when (pattern & bits) != 0 under WEF_OR, or
//       (pattern & bits) == bits under WEF_AND. The pattern at release is
//       returned through resbits; then WEF_CLEAR (0x10) clears the WAITED bits
//       and WEF_CLEAR_ALL (0x20) clears every bit -- the clear-on-wait rule."
//   E3 "SetEventFlag ORs the bits, then scans the wait queue in order (FIFO,
//       or priority under attr bit 0): every satisfied waiter is released
//       with the pattern captured at that moment and its clear applied
//       IMMEDIATELY, which may leave later waiters unsatisfied."
//   E4 "ClearEventFlag ANDs the pattern with the mask (bits to KEEP)."
//   E5 "PollEventFlag: the same test without blocking; KE_EVF_COND when unmet."
//   E6 "Deleting the flag returns KE_WAIT_DELETE to every waiter; a forced
//       release returns KE_RELEASE_WAIT."
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ee/hle/kernel_codes.h"

struct EventFlagParam {
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t init_bits = 0;
};

// ReferEventFlagStatus's view (the SDK EventFlagParam as the kernel fills it).
struct EventFlagStatus {
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t init_bits = 0;
    uint32_t bits = 0;
    int wait_threads = 0;
};

struct EventFlagInfo {
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t init_bits = 0;
    uint32_t bits = 0;
    int waiters = 0;
    bool deleted = false;
    struct Waiter {
        int tid;
        int prio;
        uint64_t seq;
        uint32_t wait_bits;
        uint32_t mode;
    };
    std::vector<Waiter> waitq;  // guarded by m
    std::mutex m;
    std::condition_variable cv;
};

class EventFlags {
public:
    static EventFlags& Instance();

    int Create(const EventFlagParam& p);
    int Delete(int eid);
    int Set(int eid, uint32_t bits);
    int Clear(int eid, uint32_t keep_mask);
    // `result` (optional) receives the pattern at release, before the clear.
    int Wait(int eid, uint32_t wait_bits, uint32_t mode, uint32_t* result);
    int Poll(int eid, uint32_t wait_bits, uint32_t mode, uint32_t* result);
    int ReferStatus(int eid, EventFlagStatus& out);

    std::shared_ptr<EventFlagInfo> Lookup(int eid);
    void RemoveWaiter(int eid, int tid);
    void ResetForTest();
    // Savestate seams (same contract as Semaphores::SnapshotTable/InstallTable).
    using TableEntry = std::pair<int, std::shared_ptr<EventFlagInfo>>;
    std::vector<TableEntry> SnapshotTable();
    void InstallTable(const std::vector<TableEntry>& entries);

    // The E2 test, pure.
    static bool Satisfied(uint32_t pattern, uint32_t wait_bits, uint32_t mode);
    // The E2 clear-on-wait step, pure: the pattern after a release.
    static uint32_t AfterRelease(uint32_t pattern, uint32_t wait_bits, uint32_t mode);

private:
    EventFlags() = default;
    std::mutex map_mu_;
    std::unordered_map<int, std::shared_ptr<EventFlagInfo>> flags_;
    int next_id_ = 1;
};
