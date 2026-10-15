#pragma once
// MtvuStack: the MTVU half of the composition root, constructed ONLY when
// Config engine.mtvu is on (the deterministic profile sets it off, so no
// harness leg owns one). It bundles, in the checklist's construction order,
// the worker's own VU1 unit, the runahead guard + backpressure, the MtvuWorker,
// the EE-side MtvuSync (installed on the INLINE unit's dispatch as the EE-kick
// redirect), the DMAC staged feed and the GIF drain thread -- and the three
// things the root reaches them through:
//
//  * SiteSync(site): the DeferOn wrapper around a GS/DMA sync HLE row. While
//    the worker runs, frame boundaries are deferred and a site that only
//    needed "the GS caught up" notes the skip; otherwise it waits for the
//    worker to fall idle (MtvuSync::Sync). The caller's own observation
//    (FlushDeferredVif1) runs after, unconditionally.
//  * FrameSync(seq, now_ns): the boundary ladder at the frame sync (after the
//    DMAC's tail chase): the frame's feed is declared complete; while the
//    worker runs an ORDERED boundary slot is enqueued and one drain pass runs
//    (the EE starts frame N+1 while the worker finishes frame N); otherwise
//    the frame sync is the terminal sync point (Sync(SceGsSyncV)).
//  * Vif1Window(): an MmioHandler over the VIF1 register / FIFO windows: a
//    VIF1_STAT read returns the busy view while segments are in flight, every
//    other register read and every write syncs first (PS2 rule: the EE
//    observes VIF1 as hardware would -- nothing may touch the VIF1 unit while
//    the worker owns it).
//
// Thread rule: Start() spawns the worker, and the drain thread only when the
// root asks for it: the drain owner runs the GIF parser and so the GS state
// machine and its DrawIR consumer -- with a live GL video stack that consumer
// draws on the draining thread, and a GL context has ONE current thread, so
// the drain stays inline on the EE thread while GL is live and moves to the
// thread only when no GL consumer exists. Start() runs before any producer
// traffic (the ELF has not been loaded when the root constructs it). Teardown order: Quiesce (savestate), then Stop() = worker.Stop()
// (drains, joins, unbinds VIF1) then drain.Stop(); the destructor does the
// same. Nothing here reads Config: the root copies engine.mtvu /
// engine.mtvu_depth into MtvuSettings and engine.vu1_mode into VuSettings.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/dmac/dmac_mtvu_feed.h"
#include "engine/ee/mem/mmio_handler.h"
#include "engine/gif/gif_drain_thread.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_settings.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

class ChainWalker;
class GifArbiter;
class MtvuVifFeed;

class MtvuStack {
public:
    // How long Quiesce waits for the pipeline + arbiter to run empty.
    static constexpr int kQuiesceLaps = 2000;
    static constexpr uint64_t kQuiesceLapNs = 500'000;

    MtvuStack(MtvuVifFeed& vif, ChainWalker& walker, GifArbiter& arbiter, VuTierDispatch& inline_vu1_dispatch,
              const VuSettings& worker_vu_settings, bool xgkick_paced_drain, MtvuSettings settings,
              const std::atomic<bool>* running);
    ~MtvuStack();
    MtvuStack(const MtvuStack&) = delete;
    MtvuStack& operator=(const MtvuStack&) = delete;

    // Spawn the worker (named / pinned by the platform) and, when asked, the drain thread.
    void Start(bool with_drain_thread);
    // worker.Stop() then drain.Stop(); idempotent.
    void Stop();
    bool Running() const { return worker_.IsRunning(); }

    // The DeferOn wrapper (SceDmaSync, SceGifSyncDma, SceGsSyncPath, ...).
    void SiteSync(SyncSite site);
    // The boundary ladder at the frame sync; `seq` = the sync count. Under the
    // kick drop it also pushes the frame mark and takes the next frame's verdict.
    void FrameSync(uint32_t seq, uint64_t now_ns);
    // The clock contract's KICK DROP (play mode, with the GS queue's frame
    // skip): the game thread never parks on the worker; a frame the worker is
    // more than kDropLagFrames behind runs its VIF stream without its VU1
    // programs (MtvuBackpressure drop mode). Off = the bytes/frames park.
    void SetKickDrop(bool on) { backpressure_.SetDropMode(on); }
    bool KickDrop() const { return backpressure_.DropMode(); }
    // A genuine present of the boundary's frame (the runahead's consumer side).
    void NotePresented();
    // Savestate quiesce: Sync(Other), then the pipeline and the arbiter run
    // empty and the drain thread reports two equal idle generations. False
    // with `why` when the pipeline did not settle within the lap budget.
    bool Quiesce(std::string* why);
    // After a restored image applied (the live F8, --load-state): the worker's
    // unit IS the VU1 while the stack runs (VIF1 binds it; EE kicks redirect to
    // it), yet the image's VU1C/VU1D/VU1R sections land on the INLINE unit the
    // sections own. Mirror that unit's micro/data memory and register file into
    // the worker's unit and invalidate its decoded image, so the restored
    // title's first kick runs the restored program -- never the previous
    // session's. The worker must be idle (Quiesce / the joined boundary).
    void MirrorVu1(const VuUnit& restored);
    uint64_t Mirrors() const { return mirrors_; }

    // The VIF1 register + FIFO windows over `inner` with the sync rule.
    MmioHandler& Vif1Window(MmioHandler& inner);

    // The worker's VU1 unit: the composition root installs the title library's
    // consult (a second Vu1HleRuntime, driven from the worker thread) on it.
    VuUnit& WorkerVu1() { return worker_vu1_; }
    MtvuWorker& worker() { return worker_; }
    MtvuSync& sync() { return sync_; }
    GifDrainThread& drain() { return drain_; }
    MtvuBackpressure& backpressure() { return backpressure_; }
    uint64_t Declared() const { return declared_.load(std::memory_order_relaxed); }
    uint64_t Presented() const { return presented_.load(std::memory_order_relaxed); }
    uint64_t DeferredBoundaries() const { return deferred_boundaries_; }
    uint64_t TerminalSyncs() const { return terminal_syncs_; }

    // run.meta `mtvu=`: on,worker=<0|1>,drain=<passes>,redirects=<n>,syncs=<12 counts ':'>,skipped=<12 counts ':'>,...
    std::string Census() const;

private:
    class Vif1Gate final : public MmioHandler {
    public:
        Vif1Gate(MtvuStack& stack, MmioHandler& inner) : stack_(stack), inner_(inner) {}
        uint64_t Read(uint32_t addr, unsigned size) override;
        void Write(uint32_t addr, unsigned size, uint64_t value) override;

    private:
        MtvuStack& stack_;
        MmioHandler& inner_;
    };

    static void OnThreadStart(void* name);

    VuUnit worker_vu1_;
    RunaheadGuard guard_;
    MtvuBackpressure backpressure_;
    MtvuWorker worker_;
    MtvuSync sync_;
    DmacMtvuFeed feed_;
    GifDrainThread drain_;
    GifArbiter& arbiter_;
    VuTierDispatch& inline_dispatch_;
    std::atomic<uint64_t> declared_{0};
    std::atomic<uint64_t> presented_{0};
    uint64_t deferred_boundaries_ = 0;
    uint64_t terminal_syncs_ = 0;
    uint64_t quiesce_laps_ = 0;
    uint64_t mirrors_ = 0;
    std::unique_ptr<Vif1Gate> vif1_gate_;
};
