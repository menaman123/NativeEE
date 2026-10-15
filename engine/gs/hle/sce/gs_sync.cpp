#include "engine/gs/hle/sce/gs_sync.h"

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/recomp_abi/host_app/gs_hle.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/ee/timing/derived_clocks.h"

// Row-15 symbols of the generated TU (recomp_abi/host_app/gs_hle.h).
std::atomic<uint32_t> g_sceGsSyncV_count{0};
std::atomic<uint32_t> g_vsync_frame{0};
void hle_sceGsSyncV(CpuContext& ctx) { GsSync::SyncV(ctx); }
void hle_sceGsSyncPath(CpuContext& ctx) { GsSync::SyncPath(ctx); }

BoundarySequencer* GsSync::sequencer_ = nullptr;
const DerivedClocks* GsSync::clocks_ = nullptr;
GsSync::DmacHook GsSync::chase_vif1_tail_;
GsSync::DmacHook GsSync::flush_deferred_vif1_;
uint64_t GsSync::tail_chases_ = 0;
uint64_t GsSync::syncpath_flushes_ = 0;

void GsSync::Bind(BoundarySequencer* sequencer, const DerivedClocks* clocks) {
    sequencer_ = sequencer;
    clocks_ = clocks;
}

bool GsSync::Bound() { return sequencer_ != nullptr && clocks_ != nullptr; }

void GsSync::BindDmac(DmacHook chase_vif1_tail, DmacHook flush_deferred_vif1) {
    chase_vif1_tail_ = std::move(chase_vif1_tail);
    flush_deferred_vif1_ = std::move(flush_deferred_vif1);
}

void GsSync::Register(SdkBindingManifest& m) { m.Register("libgraph.a", "sceGsSyncV", &SyncV); }

void GsSync::SyncV(CpuContext& ctx) {
    if (!Bound()) GuestPark::Forever("gs_sync_unbound", ctx.cpuRegs.pc);
    g_sceGsSyncV_count.fetch_add(1, std::memory_order_relaxed);
    // The frame's VIF1 tail lands in this frame: the parked kick is flushed
    // and every END slot the game rewrote behind the walk is resumed, before
    // the boundary sequence (field tick, present) runs.
    if (chase_vif1_tail_) {
        chase_vif1_tail_();
        ++tail_chases_;
    }
    // A stopped sequence (savestate load) unwinds the boundary thread; the
    // return value below is then never read.
    const bool completed = sequencer_->RunBoundary(ctx);
    // Phase 3: the presented-frame counter follows the boundary count (the
    // presenter of Phase 6 advances it from its own ack).
    g_vsync_frame.store(static_cast<uint32_t>(clocks_->FrameBoundaryCount()), std::memory_order_relaxed);
    ctx.cpuRegs.GPR.r[2].UD[0] = clocks_->SyncVFieldParity();  // field: 0 even, 1 odd
    // Temporary SDK adapter only. The generic game boundary owns capture.
    // Dispatch delivers after the installed service wrapper returns as well.
    if (completed) sequencer_->DeferServiceReturn(ctx, clocks_->FrameBoundaryCount());
}

void GsSync::SyncPath(CpuContext& ctx) {
    // The game observes GIF-path completion: the parked VIF1 kick (PATH2) is
    // consumed before "idle" is reported. Every transfer completes in-line
    // once run, so the wait itself never blocks.
    if (flush_deferred_vif1_) {
        flush_deferred_vif1_();
        ++syncpath_flushes_;
    }
    ctx.cpuRegs.GPR.r[2].UD[0] = 0;  // path idle
}

uint32_t GsSync::SyncVCount() { return g_sceGsSyncV_count.load(std::memory_order_relaxed); }
uint64_t GsSync::TailChases() { return tail_chases_; }
uint64_t GsSync::SyncPathFlushes() { return syncpath_flushes_; }

void GsSync::ResetForTest() {
    sequencer_ = nullptr;
    clocks_ = nullptr;
    chase_vif1_tail_ = nullptr;
    flush_deferred_vif1_ = nullptr;
    tail_chases_ = 0;
    syncpath_flushes_ = 0;
    g_sceGsSyncV_count.store(0);
    g_vsync_frame.store(0);
}
