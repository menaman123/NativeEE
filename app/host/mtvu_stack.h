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
//  * GifWindow(): an MmioHandler over the GIF register window: every access
//    fences the drain thread first while it runs (the parser owns the GIF
//    transfer state the STAT / TAG registers are composed from), then reaches
//    the register file; with the drain inline it is the plain handler.
//
// Thread rule: Start() spawns the worker, and the drain thread only when the
// root asks for it. The drain owner runs the GIF parser and so the GS state
// machine up to the DrawIR chain's head: the root may ask for the thread only
// when that chain hands its records to a consumer on ITS OWN thread (the
// pipelined / lockstep DrawIR queue) -- with a consumer that draws on the
// pushing thread (gs.queue=inline, the GL context's one current thread) the
// drain stays inline on the EE thread. With the thread up the EE sees the GS
// as hardware: the CSR events (SIGNAL / FINISH) rise when the parser reaches
// the register (on the drain thread, through the INTC's foreign-thread
// route), and every EE-side push into the DrawIR chain (the frame boundary's
// VSync job, the privileged display registers' jobs) travels as an ORDERED
// arbiter slot so the chain keeps one producer in stream order. Start() runs
// before any producer traffic (the ELF has not been loaded when the root
// constructs it). Teardown order: Quiesce (savestate), then Stop() =
// worker.Stop() (drains, joins, unbinds VIF1) then drain.Stop(); the
// destructor does the same. Nothing here reads Config: the root copies
// engine.mtvu / engine.mtvu_depth into MtvuSettings and engine.vu1_mode into
// VuSettings.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/dmac/dmac_mtvu_feed.h"
#include "engine/gif/gif_boundary_hook.h"
#include "engine/ee/mem/mmio_handler.h"
#include "engine/gif/gif_drain_thread.h"
#include "engine/vu/tiers/mtvu_backpressure.h"
#include "engine/vu/tiers/mtvu_settings.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"
#include "engine/vu/tiers/runahead_guard.h"
#include "engine/vif/vif_finish_scan.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

class ChainWalker;
class GifArbiter;
class GifRegs;
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
    // Whether FrameSync defers the boundary (an ordered arbiter slot + one
    // drain; the EE starts frame N+1 while the worker finishes N -- the hook
    // the arbiter was built with sees the slot in stream position) or ends the
    // frame with the terminal Sync(SceGsSyncV) (every queued span finished
    // before the boundary runs: the frames lane stays keyed by boundary). On =
    // the first title's play composition; a title whose game waits for its own
    // GS FINISH before the vblank gains nothing from the deferral and keeps
    // its presents complete with it off.
    void SetDeferBoundaries(bool on) { defer_boundaries_ = on; }
    bool DeferBoundaries() const { return defer_boundaries_; }
    // E426: the GIF register window's STAT read answered WITHOUT fencing the
    // drain thread, from the EE's own view (the mode / control bits the EE
    // wrote, the VIF's PATH3 mask, OPH / APATH from the EE-side PATH2 / PATH3
    // transfer scans: a packet the EE submitted and left open); every other
    // register and every write still fences. Off = every access fences (the
    // fence re-serialises the frame once per frame: census gif_fences).
    void SetComposedGifStat(bool on) { composed_gif_stat_ = on; }
    bool ComposedGifStat() const { return composed_gif_stat_; }
    // E427: a VIF1_STAT read while the worker still replays answers the EE's
    // own register file (the inline composition's view: every transfer the EE
    // staged is complete at its observation point, as the early FINISH already
    // says of the GS) instead of the busy view -- the title's sceGsSyncPath
    // poll on the vsync thread (census vif1_stat_poll_site) returns at once and
    // the logic thread runs under the worker. Every other VIF1 register and
    // every write still syncs (VU1 memory / the VIF1 unit are the worker's).
    // Off = the busy view (parked under the early FINISH: ParkVif1StatPoll).
    void SetVif1IdleView(bool on) { vif1_idle_view_ = on; }
    bool Vif1IdleView() const { return vif1_idle_view_; }
    // The poll-site census' function lookup (the title's shadow call stack: the
    // guest function the first busy-view VIF1_STAT poll came from; 0 = unknown).
    // The engine keeps no recompiled-runtime symbol here.
    using PollSiteFn = uint32_t (*)(void* user);
    void SetVif1PollSite(PollSiteFn fn, void* user) { vif1_poll_site_fn_ = fn; vif1_poll_site_user_ = user; }
    // E425 early FINISH (off until enabled; the composition root's knob). The
    // EE raises the GS FINISH when it has STAGED the packet carrying the write:
    // every span staged to the worker is scanned for PATH2 DIRECT A+D FINISH
    // writes (VifFinishScan, on the producer thread), every EE PATH3 submit by
    // the arbiter; each find is counted on the arbiter and `raiser` runs (the
    // GS CSR bit + the INTC edge). The parser's later Finish() consumes one
    // count per write and raises nothing. Inert without a running worker (the
    // inline feed's parser reaches the write in the same call).
    using FinishRaiser = void (*)(void* user);   // = GifArbiter::FinishRaiser
    void EnableEarlyFinish(FinishRaiser raiser, void* user);
    bool EarlyFinishOn() const { return early_finish_; }
    // After a savestate restore: the scan re-seeds VIF1's CL / WL from the
    // restored register file and the outstanding count drops (nothing is in
    // flight after a load; the arbiter was cleared).
    void ResyncEarlyFinish(uint32_t cl, uint32_t wl);
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
    // The GIF register window over `inner` with the drain-fence rule; the
    // GifRegs form also enables the composed STAT view (SetComposedGifStat).
    MmioHandler& GifWindow(MmioHandler& inner);
    MmioHandler& GifWindow(GifRegs& inner);
    // Fences the drain thread (immediate with the drain inline); counted (and
    // booked in the wait ledger as gif_fence).
    void FenceDrain();
    // The boundary's VSync-job fence (the terminal composition's ordered_submit):
    // the same fence booked as vsync_fence.
    void VsyncFence();
    uint64_t GifFences() const { return gif_fences_.load(std::memory_order_relaxed); }
    // run.meta `mtvu_waits=`: every producer-side park on the pipeline by cause --
    // MtvuSync's ledger (syncs by site, the STAT poll park, the kernel-wait idle
    // loop by classification, the fences) plus the backpressure park; then
    // `vif1_writes=<reg>=<n>:...` (which VIF1 registers the EE wrote through the
    // gate: each write is a full sync).
    std::string WaitsCensus() const;
    // The E425 hazard census of the GIF register window: accesses per register
    // slot ((addr - base) / 16: CTRL MODE STAT - TAG0..3 CNT P3CNT P3TAG), and
    // of the STAT reads how many returned OPH (bit 9, a tag in transfer) -- the
    // bits a composed view would have to answer. "gif_reads=" in Census().
    struct GifWindowCensus {
        uint64_t reads[16] = {};
        uint64_t writes[16] = {};
        uint64_t stat_oph = 0;      // STAT reads with OPH set
        uint64_t stat_m3p = 0;      // STAT reads with M3P (the VIF PATH3 mask) set
        uint64_t stat_fqc = 0;      // STAT reads with a nonzero FQC
        uint64_t stat_composed = 0; // STAT reads answered from the composed view (no fence)
        uint64_t stat_composed_busy = 0;   // ... of which reported OPH (an open EE-side packet)
        uint64_t stat_composed_miss = 0;   // fenced reads whose composed view would have differed (knob off: the proof lane)
    };
    const GifWindowCensus& GifWindowReads() const { return gif_window_census_; }

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
    std::string GifReadsCensus() const;
    std::string Vif1PollSiteCensus() const;

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

    class GifGate final : public MmioHandler {
    public:
        GifGate(MtvuStack& stack, MmioHandler& inner) : stack_(stack), inner_(inner) {}
        uint64_t Read(uint32_t addr, unsigned size) override;
        void Write(uint32_t addr, unsigned size, uint64_t value) override;

    private:
        MtvuStack& stack_;
        MmioHandler& inner_;
    };

    static void OnThreadStart(void* name);
    static void OnStagedSpan(void* self, const uint8_t* data, size_t size, bool tte);
    static void OnStagedReset(void* self);
    uint32_t ComposeGifStat() const;

    // The deferred boundary's consumer: a boundary slot delivered by the drain
    // owner means every packet of that frame reached the parser -- the frame is
    // "presented" to the runahead guard (declared - presented = frames the
    // pipeline is behind; the guard parks the EE past depth_frames).
    class BoundaryPresented final : public GifBoundaryHook {
    public:
        explicit BoundaryPresented(MtvuStack& stack) : stack_(stack) {}
        void OnBoundary(uint32_t seq, uint64_t enqueue_ns) override;

    private:
        MtvuStack& stack_;
    };

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
    std::atomic<uint64_t> gif_fences_{0};
    GifWindowCensus gif_window_census_;   // EE thread only (the window's reader)
    uint64_t vif1_reg_writes_[16] = {};   // VIF1 register writes through the gate, by slot ((addr - base) / 16)
    uint64_t vif1_reg_reads_[16] = {};    // ... and the non-STAT reads (each a full sync)
    struct Vif1PollCensus {               // busy-view VIF1_STAT polls: who, from where (EE thread only)
        uint64_t by_tid[8] = {};
        uint64_t idle_views = 0;   // busy-worker STAT reads answered idle (E427)
        uint32_t first_func = 0;
        int first_tid = -1;
    };
    Vif1PollCensus vif1_poll_census_;
    uint64_t deferred_boundaries_ = 0;
    uint64_t terminal_syncs_ = 0;
    uint64_t quiesce_laps_ = 0;
    uint64_t mirrors_ = 0;
    bool defer_boundaries_ = true;
    bool early_finish_ = false;
    bool composed_gif_stat_ = false;
    bool vif1_idle_view_ = false;
    PollSiteFn vif1_poll_site_fn_ = nullptr;
    void* vif1_poll_site_user_ = nullptr;
    VifFinishScan vif1_scan_;   // producer (EE) thread only
    BoundaryPresented boundary_presented_;
    std::unique_ptr<Vif1Gate> vif1_gate_;
    std::unique_ptr<GifGate> gif_gate_;
    GifRegs* gif_inner_ = nullptr;   // the register file under the gate (the composed view's source)
};
