#pragma once
#include <cstddef>
// The EE side of the MTVU pipeline: where the EE must observe VU1 / VIF1 as
// hardware would (a VIF1 register read, a VU1 memory window access, an sce
// sync HLE, an EE-side VU1 kick), it waits for the worker to fall idle first
// -- draining the GIF arbiter eagerly while it waits so the parked EE stays
// the boundary deliverer -- and then fences the drain thread.
//
// PS2 rule: VPU_STAT.VBS1 / VIF1_STAT report VU1 busy while a program runs;
// the EE polls them. TryVif1StatBusyView is that observation while segments
// are in flight: VPS=3 (decoding) | VEW (waiting on the VU) | FQC=16 (FIFO
// full). Frame boundaries are deferred (queued as arbiter slots) whenever the
// worker runs -- DeferOn -- so the sites that only needed "the GS caught up"
// skip the sync and note it (NoteSiteSkipped); the vblank boundary itself
// stays unconditional. The PATH-sync relax arm is not written: SceGsSyncPath
// under DeferOn skips like the other GS syncs, unconditionally.
//
// Cross-thread contract: Sync / Try* / NoteSiteSkipped run on the producer
// (EE) thread; a call from the worker returns at once. The site counters are
// relaxed atomics. Redirect runs on whichever thread kicks the inline unit.
#include <atomic>
#include <cstdint>

class GifArbiter;
class MtvuWorker;
class VuTierDispatch;
class VuUnit;

// The EE-side observation sites, in the OLD id order.
enum class SyncSite : uint8_t {
    Vif1RegRead = 0,
    Vif1RegWrite,
    Vu1Mem,
    SceGsSyncV,
    SceGsSyncPath,
    SceGsResetPath,
    SceDmaSync,
    SceGifSyncDma,
    WaitVblank,
    Vu1EeKick,
    Other,
    PrivPush,
    Count
};

class MtvuSync {
public:
    static constexpr uint32_t kVif1StatBusyView = 0x03u | (1u << 2) | (16u << 24);
    static constexpr uint64_t kPollNs = 500'000;

    MtvuSync(MtvuWorker& worker, GifArbiter& arbiter);

    // Wait until the worker is idle (eager-draining meanwhile), then fence the
    // drain thread. Immediate when the worker is down or on the worker thread.
    void Sync(SyncSite site = SyncSite::Other);
    // A site that would have synced skipped it under DeferOn (census only).
    void NoteSiteSkipped(SyncSite site);
    // True and `*stat` = the busy view while segments are in flight.
    bool TryVif1StatBusyView(uint32_t* stat) const;
    // Opportunistic EE-side drains of the sealed prefix (no wait).
    void TryEagerDrain();
    void TryParkDrain();
    // Frame boundaries are deferred: the worker runs.
    bool DeferOn() const;

    uint64_t SyncCount(SyncSite site) const;
    uint64_t SkippedCount(SyncSite site) const;

    // The dispatcher's MTVU step for the INLINE VU1 unit: an EE kick while
    // the worker owns VU1 syncs, then runs the program on the worker's unit.
    static bool Redirect(void* user, VuUnit& unit, uint32_t start_pc);
    void InstallInto(VuTierDispatch& inline_vu1_dispatch);
    static void UninstallFrom(VuTierDispatch& inline_vu1_dispatch);

private:
    MtvuWorker& worker_;
    GifArbiter& arbiter_;
    std::atomic<uint64_t> sync_count_[static_cast<size_t>(SyncSite::Count)];
    std::atomic<uint64_t> skipped_count_[static_cast<size_t>(SyncSite::Count)];
};
