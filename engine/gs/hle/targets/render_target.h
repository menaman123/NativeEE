// gs/hle/targets/render_target.h -- RenderTarget: one GS frame buffer as the
// backend renders it -- identity (fbp, logical size, PSM/FBW tags, scale), the
// backend surfaces, and the coherence state between them and GS local memory.
//
// Per-draw protocol (the caller is the class renderer / replay driver):
//   PrepareDraw  -- fills the target half of DrawParams: attachments, the alt16
//                   bifurcation (a 16-bit FRAME over a target that has held 32-bit
//                   content draws into the companion, refreshed first), the
//                   per-frame clear decision, the Cd-copy tracker pointer.
//   AfterDraw    -- the coherence tail from DrawResult, by route: the main route
//                   grows valid_rect (unclamped tri bbox), drawn_since_sync (tri
//                   bbox within the scissor), retags PSM / FBW / content and stales
//                   the companion; the companion route grows alt16_drawn only.
//                   Both: Z rect and history, keep mask, per-view author counter,
//                   lane record, Cd-copy dirty rect.
// MarkCompositeConsumed is called on the SOURCE target when a draw into the
// scanout target samples it (clears keyed on composite consumption).
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/coherence_state.h"
#include "engine/gs/hle/targets/surface_views.h"
#include "engine/gs/hle/targets/target_identity.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class RenderTarget {
public:
    RenderTarget() = default;

    // Create the colour / depth / Cd-copy surfaces for `id` and clear them (colour
    // 0, depth 0.0 = far in the z / 2^32 mapping).
    void Create(render::Backend& be, const TargetIdentity& id);
    void Destroy(render::Backend& be);
    // The 16-bit companion attachment (same physical size), created on demand.
    void EnsureAlt16(render::Backend& be);
    // Blit the alt16_stale rect (clamped to the target, consumed either way) main
    // -> companion through the 5551 pack / unpack (CPU route over Readback /
    // Upload); authors the companion (alt_author_seq).
    void RefreshAlt16FromMain(render::Backend& be);

    // Target-side DrawParams for `ir`. `ee_frame` is the EE game-frame counter,
    // `scanout_fbp` the current DISPFB base. May create the companion and refresh it.
    void PrepareDraw(render::Backend& be, const DrawIr& ir, uint32_t ee_frame,
                     uint32_t scanout_fbp, render::DrawParams* out);
    // Coherence tail after Backend::Draw. `frame_count` is the renderer's frame
    // counter (lane records), `gfx_epoch` the EE-order stamp of the draw, `draw_seq`
    // the caller's strictly increasing committed-draw ordinal (a Z write records
    // it as last_z_seq; 0 = unordered).
    void AfterDraw(const DrawIr& ir, const render::DrawResult& res, uint32_t frame_count,
                   uint64_t gfx_epoch, uint64_t draw_seq = 0);
    void MarkCompositeConsumed();
    // The stencil-composite materialise wrote [x0,x1) x [y0,y1) of the routed view
    // with no Backend::Draw: the stamps one covering draw would have left (extent
    // rects, Cd-copy dirt, content flag, the view's author counter, frame / epoch)
    // -- no ledger record (every strip recorded its own).
    void MarkMaterialized(int x0, int y0, int x1, int y1, bool alt16, uint32_t frame_count,
                          uint64_t gfx_epoch);

    // The per-frame clear rule: composite-consumed targets clear when marked,
    // others once per EE frame.
    bool WantsFrameClear(uint32_t ee_frame) const;

    static bool Is16BitColor(uint32_t psm) { return psm == 0x02 || psm == 0x0A; }
    static bool Is32BitClass(uint32_t psm) { return psm == 0x00 || psm == 0x01; }
    // Storage bits of a FRAME / ZBUF PSM field (24-bit modes store as 32); 0 for
    // a field that is not a frame format.
    static uint32_t FrameBitsPerPixel(uint32_t psm);
    // FRAME.FBMSK folded onto the 5551 layout (bit 15 = alpha).
    static uint16_t Fbmsk16From32(uint32_t fbmsk);
    // The 5551 keep mask of an index-lane draw (low byte preserved).
    static constexpr uint16_t kIndexLaneKeep16 = 0x00FF;

    TargetIdentity identity;
    SurfaceViews views;
    CoherenceState coherence;
};

}  // namespace gs
}  // namespace ps2r
