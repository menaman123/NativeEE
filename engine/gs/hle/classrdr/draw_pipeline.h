// gs/hle/classrdr/draw_pipeline.h -- DrawPipeline: the ONE per-draw sequence
// above the backend interface, and the per-job entry points the render-queue
// consumer (or the corpus replay driver) calls in stream order.
//
// Per draw (Draw): the class registry dispatches the draw to its native renderer
// when one accepts it; otherwise -- and for every renderer that is a replica of
// the generic lowering -- DrawGeneric runs, in this order:
//   1. render size (TargetDims)                      6. Backend::Draw
//   2. target acquire (RenderTargetCache)             7. RenderTarget::AfterDraw + NoteDrawn
//   2b. same-buffer pre-draw flush (a 32-bit-class    8. RenderTargetCache::AfterDrawTail
//       FRAME draw repaints the rows a deferred        9. TextureResolver::EvictUnderDraw
//       16-bit writeback of the SAME buffer still
//       owes the mirror: the entry is taken first)
//   3. alias pre-draw refresh (TargetRefresher)
//   4. target DrawParams (RenderTarget::PrepareDraw) + composite-consumption mark
//   5. texture resolve (TextureResolver) + the column-shuffle snap decision
// Before the guards and step 1 the live stencil-composite sequence sees the draw
// (a draw into or out of the armed accumulator materialises it first -- "out of"
// includes leaving it while it is the bound target, the falling-edge capture; an
// RGB write demotes a primed one); the hook runs before the vertex / FBW guards
// as the reference does. Step 4 also arms the colour-authored Z route (a Z-tested
// GEQUAL / GREATER, ZMSK, DATE draw whose ZBUF names ANOTHER live colour target
// compares against that target's colour image in the shader). Prepare / Finish
// split the sequence for the stencil-composite renderer (steps 1-5 / 7-9). The
// bound target (active_target_fbp) is the last target a draw bound. Per job:
// OnInvalidate, OnUpload, OnSetWindow, OnSetFrontBuffer, OnSyncAll, OnFrameEnd (EE
// frame mark -> reader-band present flush -> display grab -> display quad ->
// Backend::Present).
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/gs/hle/classrdr/class_registry.h"
#include "engine/gs/hle/classrdr/draw_route_trace.h"
#include "engine/gs/hle/classrdr/sequence_materializer.h"
#include "engine/gs/hle/classrdr/sequence_recipe.h"
#include "engine/gs/hle/classrdr/target_dump.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/present/presenter.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/hle/texsrc/texture_resolver.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class DrawPipeline {
public:
    struct Deps {
        render::Backend* be = nullptr;
        VramImage* vram = nullptr;
        RenderTargetCache* cache = nullptr;
        TargetRefresher* refresher = nullptr;
        TextureResolver* resolver = nullptr;
        ScanoutComposer* scanout = nullptr;
        Presenter* presenter = nullptr;
        const VramLayout* layout = nullptr;
        ClassRegistry* registry = nullptr;       // optional: no registry = generic only
        SequenceRecipe* sequence = nullptr;      // optional: the stencil-composite FSM
    };
    struct Frame {
        std::vector<uint8_t> rgba;   // the display grab (empty when nothing was readable)
        int w = 0, h = 0;
        bool grabbed = false;
        // The frames-lane hash of `rgba` (FrameHasher::HashNativeScale at the
        // consumer's hash scale), computed where the grab is made when a scale is
        // set (`hashed`), so a concurrent reader (the wall-locked play boundary)
        // needs no pixel copy. 0 when nothing was grabbed.
        uint64_t hash = 0;
        bool hashed = false;
        ScanoutComposer::Source source = ScanoutComposer::Source::None;   // what the grab read
        uint32_t source_fbp = 0xFFFFFFFFu;   // its base (target or DISPFB)
        bool source_fallback = false;        // the most-recently-drawn target stood in
    };

    explicit DrawPipeline(const Deps& d);
    static constexpr uint32_t kNoFbp = 0xFFFFFFFFu;

    // Stream stamps carried by every record.
    void SetGfxEpoch(uint64_t e) { gfx_epoch_ = e; }
    void SetSrcFrame(uint32_t f) { src_frame_ = f; }
    uint64_t GfxEpoch() const { return gfx_epoch_; }

    // Live mode: the VRAM image under the pipeline IS the console's local memory,
    // so the resolver's VRAM-decode tiers serve textured draws. Off (the default)
    // is the corpus replay, which carries no VRAM snapshot: a textured read that
    // no target serves keeps its UV conversion and samples nothing. In live mode a
    // class-routed textured VRAM-decode draw lowers with the FULL uniform set
    // (PrepareOptions::inherited_uniforms forced to 0): the reference's live path
    // sends exactly those draws to its specialised programs, which carry none of
    // the register-program uniforms the generic arms would otherwise inherit.
    void SetLiveSnapshot(bool live) { live_snapshot_ = live; }
    bool LiveSnapshot() const { return live_snapshot_; }

    // One draw in stream order; true when a class renderer drew it natively.
    bool Draw(const DrawIr& ir, const char* name_override);
    // Steps 1-9 (the generic lowering every replica renderer calls).
    render::DrawResult DrawGeneric(const DrawIr& ir);

    // The split form for a renderer that issues its own backend work between the
    // resolve and the tail: Prepare = the hook + guards + steps 1-5 (false when a
    // guard rejected, or when `abort_on_alias` and the alias pre-draw refresh is
    // demanded -- then nothing past step 2 ran and the caller re-runs the generic
    // ladder, which performs the refresh), Finish = steps 7-9 over the caller's
    // DrawResult. The options are the stencil-composite ladder's deviations from
    // the generic sequence: no pre-draw hook on its strips, the armed accumulator's
    // ledger entry held out of the pre-draw flush, the invert's self-read source
    // supplied by the caller instead of the resolver.
    struct Prepared {
        RenderTarget* target = nullptr;
        int render_w = 0, render_h = 0;
        render::DrawParams params;
        TextureSource src;
        bool alias_aborted = false;
    };
    struct PrepareOptions {
        bool run_hook;
        bool resolve_texture;
        bool abort_on_alias;
        uint32_t hold_fbp;
        uint32_t inherited_uniforms;   // render::InheritedUniform bits (DrawParams)
        PrepareOptions()
            : run_hook(true), resolve_texture(true), abort_on_alias(false), hold_fbp(kNoFbp),
              inherited_uniforms(0) {}
    };
    bool Prepare(const DrawIr& ir, Prepared* out, const PrepareOptions& opt = PrepareOptions());
    // DrawGeneric under the options (the steer's colour arm runs without the hook).
    render::DrawResult DrawGeneric(const DrawIr& ir, const PrepareOptions& opt);
    void Finish(const DrawIr& ir, Prepared& pr, const render::DrawResult& res);
    // The colour-authored Z route (OLD ZsrcRoute + the zbp target lookup): fills
    // params.zsrc / zsrc_armed / zsrc_scale; true when armed.
    bool ArmZsrc(const DrawIr& ir, render::DrawParams* params);
    // The same predicate + lookup without arming or counting (the strip steer's pre-check).
    bool ZsrcRouteAvailable(const DrawIr& ir) const;
    // The FSM hooks: the pre-draw decision (with the live leaving-capture predicate)
    // and the touch from a writeback / texture serve; both materialise through the
    // installed materializer.
    void SequencePreDraw(const DrawIr& ir);
    void SequenceTouch(uint32_t fbp);
    void SetMaterializer(SequenceMaterializer* m) { materializer_ = m; }
    uint32_t ActiveTargetFbp() const { return active_target_fbp_; }
    uint64_t ZsrcArms() const { return zsrc_arms_; }
    uint64_t DroppedMaterializes() const { return dropped_materializes_; }

    // Jobs.
    void OnInvalidate(uint32_t addr, uint32_t size);
    void OnUpload(uint32_t addr, int w, int h, const uint8_t* rgba, uint32_t footprint);
    void OnSetWindow(int new_w, int new_h, int content_w, int content_h);
    void OnSetFrontBuffer(uint32_t fbp, uint32_t fbw_px, uint32_t psm);
    void OnSyncAll();
    // Frame boundary: `ee_frame_end` marks an EE game frame; the grab lands in `out`.
    void OnFrameEnd(bool ee_frame_end, const CrtcClassifier::Snapshot& crtc, Frame* out);
    // The DEFERRED grab (perf: the frame-end grab's synchronous readback drained the
    // GPU pipeline every frame -- the consumer thread sat in glReadPixels for the
    // whole frame's GPU work). On: OnFrameEnd QUEUES a Target grab (BeginReadback
    // right after the frame's last draw, so the bytes are this frame's by command
    // order) and leaves `out` with grabbed = false and GrabPending() true; the
    // caller completes it with CompletePendingGrab at its next frame end (one frame
    // late; the GPU has had the whole frame to finish the read, so the poll never
    // waits in the steady state) or drops it (DropPendingGrab). A Mirror grab is a
    // CPU read and lands in `out` at once as before. The grab no longer offers its
    // pixels to the scanout deposit, which takes its own deferred read
    // (RenderTargetCache::SetDeferredDeposit) -- enable both together.
    void SetDeferredGrab(bool on) { deferred_grab_ = on; }
    bool DeferredGrab() const { return deferred_grab_; }
    bool GrabPending() const { return pending_grab_.valid; }
    // Lands the pending grab in `out` (pixels, w, h, source, hash left to the
    // caller); false when none was pending (`out` untouched).
    bool CompletePendingGrab(Frame* out);
    void DropPendingGrab();
    // The corpus replay's frame boundary: the same EE mark, reader-band present
    // flush and grab, then the swap alone (Presenter::PresentBlank) -- no display
    // quad, no scanout-side mirror maintenance (the reference replay tool's
    // present was the pending flush + swap; its scanout composite never ran).
    void OnFrameEndGrabOnly(bool ee_frame_end, Frame* out);
    // Application-owned presentation uses an explicit resource, at the same
    // boundary and cadence. No reader-band resolve, scanout pick or mirror.
    void OnOwnedFrameEnd(bool ee_frame_end, uint32_t source_key);

    int WindowW() const { return window_w_; }
    int WindowH() const { return window_h_; }
    uint64_t DrawSeq() const { return draw_seq_; }
    uint64_t DrawOrder() const { return draw_order_; }
    // Restore depth-writer ordering separately from the actual draw census.
    void RestoreDrawOrder(uint64_t order) { draw_order_ = order; }
    // Retire derived host views from an abandoned timeline. This is not a CPU
    // upload: incoming memory bytes and author epochs must remain untouched.
    void ForgetTextureViewsAfterRestore();
    uint64_t SameBufferFlushes() const { return same_buffer_flushes_; }
    const TextureSource& LastSource() const { return last_source_; }
    ClassRegistry* Registry() { return d_.registry; }
    SequenceRecipe* Sequence() { return d_.sequence; }
    RenderTargetCache& Cache() { return *d_.cache; }
    TextureResolver& Resolver() { return *d_.resolver; }
    ScanoutComposer& Scanout() { return *d_.scanout; }
    Presenter& Present() { return *d_.presenter; }
    const VramLayout& Layout() const { return *d_.layout; }
    render::Backend& Backend() { return *d_.be; }
    const VramImage& Vram() const { return *d_.vram; }
    void EmitCounters(std::FILE* f) const;
    // The per-draw lowering record of the last completed frame (diagnostics only).
    const DrawRouteTrace& RouteTrace() const { return route_trace_; }
    DrawRouteTrace& RouteTrace() { return route_trace_; }
    // The one-target colour-image readback around a frame's draws (diagnostics
    // only; armed at a lockstep frame boundary, see target_dump.h).
    TargetDump& Dump() { return target_dump_; }
    // An owned pass's publication (Finish called by its scene) recorded in the
    // same per-frame route sequence as the pipeline's own draws (owner none: its
    // texture is a private image, not a PS2 source) -- the draw's position
    // relative to the scene's draws is readable from draw_routes.txt.
    void NotePublished(const DrawIr& ir, const render::DrawResult& res) {
        route_trace_.Note(ir, TextureSource{}, res, static_cast<uint32_t>(draw_seq_));
    }

private:
    DrawRouteTrace route_trace_;
    TargetDump target_dump_;
    static uint64_t NowNs();
    void FrameEndGrab(bool ee_frame_end, Frame* out);
    void CloseFrameTrace();
    uint64_t frame_end_t0_ = 0;                 // the frame end's cost bracket (route trace)
    render::CallCensus frame_end_census_;
    uint64_t frame_start_t0_ = 0;               // the whole frame's bracket (previous frame end -> this one)
    render::CallCensus frame_start_census_;
    void SameBufferPreDrawFlush(const DrawIr& ir);
    void MarkCompositeConsumption(const DrawIr& ir, bool alt16_route);

    Deps d_;
    int window_w_ = 640, window_h_ = 448;
    uint64_t gfx_epoch_ = 0;
    uint32_t src_frame_ = 0;
    uint64_t draw_seq_ = 0;
    uint64_t draw_order_ = 0;
    uint64_t same_buffer_flushes_ = 0;
    uint64_t snapped_draws_ = 0;
    uint64_t zsrc_arms_ = 0;
    uint64_t dropped_materializes_ = 0;
    uint32_t active_target_fbp_ = kNoFbp;
    bool live_snapshot_ = false;
    bool deferred_grab_ = false;
    struct PendingGrab {
        bool valid = false;
        render::FenceId fence{};
        int w = 0, h = 0;
        ScanoutComposer::Source source = ScanoutComposer::Source::None;
        uint32_t source_fbp = kNoFbp;
        bool source_fallback = false;
    };
    PendingGrab pending_grab_;
    void ClearFrame(Frame* out);
    SequenceMaterializer* materializer_ = nullptr;
    TextureSource last_source_;
};

}  // namespace gs
}  // namespace ps2r
