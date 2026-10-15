#pragma once
// GsSync: a temporary SDK adapter to the generic game boundary. The two sites the
// title reaches -- the libgraph `sceGsSyncV` binding (and the generated-TU
// seam `hle_sceGsSyncV`) and the `WaitForVblank` mirror -- declare the frame
// complete, run BoundarySequencer::RunBoundary once and return the field
// parity the SDK call reports (v0 = 0 even / 1 odd).
//
// The DMAC's two observation points at the sce surface are bound here as
// plain callables (gs/hle knows no DMAC type; the composition root supplies
// the Dmac's methods):
//  * the END-slot tail chase runs at the frame sync BEFORE the boundary
//    sequence (so before the field tick and the present): real DMA tag-chases
//    the display-list ring while the CPU appends tags behind it, and the
//    frame's full packet list must land in the frame it belongs to;
//  * `hle_sceGsSyncPath` (the GIF-path idle wait) flushes the parked VIF1 kick
//    before it reports idle (v0 = 0) -- the game is observing completion.
// Unbound (no DMAC in the rig) both are no-ops and the call counts stay 0.
//
// This file owns the generated TU's row-15 symbols (`g_sceGsSyncV_count`,
// `g_vsync_frame`, the two seam bodies). Phase 6 adds its GS-side steps to
// the sequencer's slots; nothing GS-specific is written in this file.
#include <atomic>
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"

class BoundarySequencer;
class DerivedClocks;
class SdkBindingManifest;

class GsSync {
public:
    using DmacHook = std::function<void()>;

    // Binds the boundary owner to its sequencer and the derived clocks
    // (composition root); the field parity and the boundary count are
    // DerivedClocks accessors (clock 3), never re-derived here.
    static void Bind(BoundarySequencer* sequencer, const DerivedClocks* clocks);
    static bool Bound();
    // The DMAC's observation points: `chase_vif1_tail` runs at every SyncV /
    // WaitForVblank before RunBoundary; `flush_deferred_vif1` runs at every
    // SyncPath before it reports idle. Either may be empty.
    static void BindDmac(DmacHook chase_vif1_tail, DmacHook flush_deferred_vif1);
    // The libgraph rows (sceGsSyncV) on the SDK binding manifest.
    static void Register(SdkBindingManifest& m);

    // Boundary bodies (also the generated-TU seams).
    static void SyncV(CpuContext& ctx);
    static void SyncPath(CpuContext& ctx);
    static void WaitForVblank(CpuContext& ctx) { SyncV(ctx); }

    static uint32_t SyncVCount();
    // Boundaries that ran the bound tail chase / SyncPath calls that ran the bound flush.
    static uint64_t TailChases();
    static uint64_t SyncPathFlushes();
    static void ResetForTest();

private:
    static BoundarySequencer* sequencer_;
    static const DerivedClocks* clocks_;
    static DmacHook chase_vif1_tail_;
    static DmacHook flush_deferred_vif1_;
    static uint64_t tail_chases_;
    static uint64_t syncpath_flushes_;
};
