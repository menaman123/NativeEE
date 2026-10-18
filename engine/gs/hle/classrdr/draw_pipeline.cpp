// gs/hle/classrdr/draw_pipeline.cpp -- the per-draw order and the job entry points.
#include "engine/gs/hle/classrdr/draw_pipeline.h"

#include <chrono>

#include "engine/gs/hle/classrdr/texture_shuffle_snap.h"
#include "engine/gs/hle/targets/target_dims.h"

namespace ps2r {
namespace gs {

DrawPipeline::DrawPipeline(const Deps& d) : d_(d) {
    // The writeback / texture-serve entry hooks of the stencil sequence.
    if (d_.cache) d_.cache->SetTouchHook([this](uint32_t fbp) { SequenceTouch(fbp); });
    if (d_.resolver) d_.resolver->SetTouchHook([this](uint32_t fbp) { SequenceTouch(fbp); });
}

// ARMED: a draw into the accumulator, a textured read of it, or a draw that leaves
// it while it is the bound target (the falling-edge capture would snapshot the
// mid-sequence stencil fiction) materialises first. PRIMED: an RGB write demotes.
void DrawPipeline::SequencePreDraw(const DrawIr& ir) {
    if (!d_.sequence || !d_.sequence->Live()) return;
    const uint32_t seq_fbp = d_.sequence->Fbp();
    const bool leaving_capture = active_target_fbp_ == seq_fbp && ir.raster.frame.fbp != seq_fbp;
    if (d_.sequence->OnPreDraw(ir, leaving_capture) == SequenceRecipe::Action::kMaterialize) {
        if (materializer_) materializer_->Materialize(*this);
        else dropped_materializes_++;
    }
}

void DrawPipeline::SequenceTouch(uint32_t fbp) {
    if (!d_.sequence || !d_.sequence->Live()) return;
    if (d_.sequence->OnTouch(fbp) == SequenceRecipe::Action::kMaterialize) {
        if (materializer_) materializer_->Materialize(*this);
        else dropped_materializes_++;
    }
}

// ZTE with a real compare (GEQUAL / GREATER), ZMSK (no depth write to suppress),
// DATE (the shadow-volume signature), ZBUF.ZBP != own FRAME and a live colour
// target at ZBP: the compare moves into the shader against that target's colour
// image; the GL depth function is forced ALWAYS by the executor.
bool DrawPipeline::ZsrcRouteAvailable(const DrawIr& ir) const {
    const auto& t = ir.raster.test;
    if (!(t.zte && (t.ztst == 2 || t.ztst == 3) && ir.raster.zbuf.zmsk && t.date &&
          ir.raster.zbuf.zbp != ir.raster.frame.fbp))
        return false;
    const RenderTarget* z = d_.cache->Find(ir.raster.zbuf.zbp);
    return z && z->views.HasColor();
}

bool DrawPipeline::ArmZsrc(const DrawIr& ir, render::DrawParams* params) {
    params->zsrc_armed = false;
    params->zsrc = render::SurfaceId{};
    params->zsrc_scale = 1;
    if (!ZsrcRouteAvailable(ir)) return false;
    RenderTarget* z = d_.cache->Find(ir.raster.zbuf.zbp);
    params->zsrc = z->views.color;
    params->zsrc_scale = z->identity.scale;
    params->zsrc_armed = true;
    zsrc_arms_++;
    return true;
}

bool DrawPipeline::Draw(const DrawIr& ir, const char* name_override) {
    bool native = false;
    const bool dump = target_dump_.Armed();
    if (dump) target_dump_.OnPreDraw(ir, *d_.cache, *d_.be, draw_seq_);
    if (d_.registry) native = d_.registry->Dispatch(*this, ir, name_override);
    if (!native) DrawGeneric(ir);
    if (d_.registry) d_.registry->ClearCurrentClass();
    if (dump) target_dump_.OnPostDraw(ir, *d_.cache, *d_.be, draw_seq_);
    return native;
}

// A FRAME draw whose class is not 16-bit colour repaints rows a deferred 16-bit
// writeback of the SAME buffer still owes the mirror; the deposit is acquired
// before the content dies (only entries recorded by this buffer -- a cross-buffer
// aliased entry keeps its source image and stays on the read-triggered path).
void DrawPipeline::SameBufferPreDrawFlush(const DrawIr& ir) {
    const uint8_t psm = ir.raster.frame.psm;
    PendingRtSyncLedger& ledger = d_.cache->Ledger();
    if (ledger.Empty() || RenderTarget::Is16BitColor(psm)) return;
    if (!ledger.Has(ir.raster.frame.fbp)) return;
    d_.cache->FlushPending(PendingRtSyncLedger::Reason::PreDraw, ir.raster.frame.fbp, 4u);
    same_buffer_flushes_++;
}

// A draw into the scanout target that samples a DIFFERENT live target marks that
// source as composite-consumed: its per-frame clear is keyed on the consumption.
void DrawPipeline::MarkCompositeConsumption(const DrawIr& ir) {
    if (!ir.tex_enabled) return;
    if (ir.raster.frame.fbp != d_.scanout->ScanoutFbp()) return;
    if (ir.tex.tbp == ir.raster.frame.fbp) return;
    if (RenderTarget* src = d_.cache->Find(ir.tex.tbp)) src->MarkCompositeConsumed();
}

render::DrawResult DrawPipeline::DrawGeneric(const DrawIr& ir) { return DrawGeneric(ir, PrepareOptions()); }

uint64_t DrawPipeline::NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

render::DrawResult DrawPipeline::DrawGeneric(const DrawIr& ir, const PrepareOptions& opt) {
    render::DrawResult res;
    Prepared pr;
    // The draw's cost bracket (the route trace's attribution): the census is a
    // snapshot by value (the backend's own counters move under the bracket).
    const uint64_t t0 = NowNs();
    const render::CallCensus* census = d_.be->Census();
    const render::CallCensus c0 = census ? *census : render::CallCensus{};
    if (!Prepare(ir, &pr, opt)) return res;
    // 6. draw.
    res = d_.be->Draw(ir, pr.src, pr.params);
    Finish(ir, pr, res);
    const DrawRouteTrace::Cost cost = DrawRouteTrace::Cost::Delta(census ? &c0 : nullptr, census, NowNs() - t0);
    route_trace_.Note(ir, pr.src, res, static_cast<uint32_t>(draw_seq_), cost);
    return res;
}

bool DrawPipeline::Prepare(const DrawIr& ir, Prepared* pr, const PrepareOptions& opt) {
    // The sequence hook precedes the guards (a 2-vertex or FBW-0 draw into the
    // armed accumulator still materialises it) -- after the reference's vCount < 2.
    if (opt.run_hook && ir.verts.size() >= 2) SequencePreDraw(ir);
    if (ir.verts.size() < 3 || ir.raster.frame.fbw == 0) return false;
    // 1. render size.
    TargetDims::Display disp;
    disp.scanout_fbp = d_.scanout->ScanoutFbp();
    disp.content_w = d_.scanout->ContentW();
    disp.content_h = d_.scanout->ContentH();
    int render_w = 0, render_h = 0;
    TargetDims::Compute(ir, window_w_, window_h_, disp, *d_.layout, &render_w, &render_h);
    // 2. target (bound from here on: the falling-edge notion of "leaving").
    RenderTarget& t = d_.cache->GetOrCreate(ir.raster.frame.fbp, render_w, render_h,
                                            ir.raster.frame.psm, gfx_epoch_);
    active_target_fbp_ = ir.raster.frame.fbp;
    SameBufferPreDrawFlush(ir);
    // 3. alias pre-draw refresh (demand only when the caller aborts on it).
    if (d_.refresher->AliasPreDraw(t, ir, render_h, disp.scanout_fbp, gfx_epoch_, opt.hold_fbp,
                                   /*import=*/!opt.abort_on_alias) &&
        opt.abort_on_alias) {
        pr->target = &t;
        pr->alias_aborted = true;
        return false;
    }
    // 4. target params (+ the colour-authored Z route).
    render::DrawParams params;
    t.PrepareDraw(*d_.be, ir, d_.presenter->EeFrameCount(), disp.scanout_fbp, &params);
    params.render_w = render_w;
    params.render_h = render_h;
    params.inherited_uniforms = opt.inherited_uniforms;
    ArmZsrc(ir, &params);
    MarkCompositeConsumption(ir);
    // 5. texture (the caller's own source when it bypasses the resolver).
    TextureSource src;
    if (opt.resolve_texture) {
        TextureResolver::Context ctx;
        ctx.frame_count = d_.presenter->FrameCount();
        ctx.active_fbp = ir.raster.frame.fbp;
        ctx.has_snapshot = live_snapshot_;
        ctx.draw_seq = static_cast<uint32_t>(draw_seq_);
        src = d_.resolver->ResolveRead(ir, ctx);
        params.clut = src.owner.clut;
        params.idx_partner = src.owner.partner;
        // Live textured VRAM-decode (the reference's specialised-program route:
        // TME on, no target serve, a decoded texture bound): the full uniform set.
        if (live_snapshot_ && src.enabled && src.owner.kind == TextureSource::OwnerKind::Decoded)
            params.inherited_uniforms = 0;
    }
    if (TextureShuffleSnap::DetectColumnShuffle(ir.verts.data(), ir.verts.size(), ir.prim_type,
                                                ir.tex.fst, ir.raster.frame.psm,
                                                static_cast<uint8_t>(ir.tex.psm), ir.tex_enabled)) {
        src.transform.shuffle_snap = true;
        snapped_draws_++;
    }
    last_source_ = src;
    pr->target = &t;
    pr->render_w = render_w;
    pr->render_h = render_h;
    pr->params = params;
    pr->src = src;
    return true;
}

// 7-9. coherence tail.
void DrawPipeline::Finish(const DrawIr& ir, Prepared& pr, const render::DrawResult& res) {
    ++draw_seq_;
    pr.target->AfterDraw(ir, res, d_.presenter->FrameCount(), gfx_epoch_, ++draw_order_);
    d_.cache->NoteDrawn(ir.raster.frame.fbp);
    d_.cache->AfterDrawTail(*pr.target, ir, res, pr.render_h, gfx_epoch_);
    d_.resolver->EvictUnderDraw(ir, res, pr.render_h);
}

void DrawPipeline::OnInvalidate(uint32_t addr, uint32_t size) {
    d_.resolver->OnCpuWrite(addr, size);
}

void DrawPipeline::ForgetTextureViewsAfterRestore() {
    d_.resolver->Cache().DropAll();
    d_.resolver->Clut().DropAll();
    d_.resolver->Heal().Clear();
    d_.refresher->EvictUploadedInRange(0,VramImage::kBytes);
    last_source_ = {};
}

void DrawPipeline::OnUpload(uint32_t addr, int w, int h, const uint8_t* rgba, uint32_t footprint) {
    d_.refresher->UploadDirect(addr, w, h, rgba, footprint);
}

void DrawPipeline::OnSetWindow(int new_w, int new_h, int content_w, int content_h) {
    if (content_w > 0 && content_h > 0) d_.scanout->SetContentDimensions(content_w, content_h);
    if (new_w > 0 && new_h > 0) {
        window_w_ = new_w;
        window_h_ = new_h;
    }
}

void DrawPipeline::OnSetFrontBuffer(uint32_t fbp, uint32_t fbw_px, uint32_t psm) {
    d_.scanout->SetScanoutFbp(fbp);
    d_.scanout->SetDispfb(fbw_px, psm);
}

void DrawPipeline::OnSyncAll() {
    // Every target's writeback passes the cache's touch hook (SyncDrawn).
    d_.cache->SyncAll();
}

void DrawPipeline::FrameEndGrab(bool ee_frame_end, Frame* out) {
    frame_end_t0_ = NowNs();
    const render::CallCensus* census = d_.be->Census();
    frame_end_census_ = census ? *census : render::CallCensus{};
    target_dump_.OnFrameEnd();
    if (d_.resolver) d_.resolver->Trace().OnFrameEnd();
    if (ee_frame_end) d_.presenter->MarkEeFrameEnd();
    d_.cache->FlushPending(PendingRtSyncLedger::Reason::Present, 0, 0, /*reader_pages_only=*/true);
    if (out) {
        out->rgba.clear();
        out->w = out->h = 0;
        out->grabbed = d_.presenter->Grab(&out->rgba, &out->w, &out->h, d_.vram);
        out->source = d_.scanout->LastGrabSource();
        out->source_fbp = d_.scanout->LastGrabFbp();
        out->source_fallback = d_.scanout->LastGrabFallback();
        if (!out->grabbed || out->w <= 0 || out->h <= 0) {
            out->rgba.clear();
            out->w = out->h = 0;
            out->grabbed = false;
            out->source = ScanoutComposer::Source::None;
            out->source_fbp = kNoFbp;
            out->source_fallback = false;
        }
    }
}

// The frame end's cost closes the route trace's frame (the grab, the reader-band
// flush, the display deposit and the present all land in cost_frame_end).
void DrawPipeline::CloseFrameTrace() {
    const render::CallCensus* census = d_.be->Census();
    const uint64_t now = NowNs();
    route_trace_.NotePresent(d_.presenter->LastPresent(), d_.cache->Ledger());
    route_trace_.OnFrameEnd(
        DrawRouteTrace::Cost::Delta(census ? &frame_end_census_ : nullptr, census, now - frame_end_t0_),
        DrawRouteTrace::Cost::Delta(census ? &frame_start_census_ : nullptr, census, now - frame_start_t0_));
    frame_start_t0_ = now;
    if (census) frame_start_census_ = *census;
}

void DrawPipeline::OnFrameEnd(bool ee_frame_end, const CrtcClassifier::Snapshot& crtc, Frame* out) {
    FrameEndGrab(ee_frame_end, out);
    d_.presenter->Present(*d_.vram, *d_.layout, crtc, gfx_epoch_, window_w_, window_h_);
    CloseFrameTrace();
}

void DrawPipeline::OnFrameEndGrabOnly(bool ee_frame_end, Frame* out) {
    FrameEndGrab(ee_frame_end, out);
    d_.presenter->PresentBlank(window_w_, window_h_);
    CloseFrameTrace();
}

void DrawPipeline::OnOwnedFrameEnd(bool ee_frame_end, uint32_t source_key) {
    route_trace_.OnFrameEnd();
    target_dump_.OnFrameEnd();
    if (d_.resolver) d_.resolver->Trace().OnFrameEnd();
    if (ee_frame_end) d_.presenter->MarkEeFrameEnd();
    d_.presenter->RecordOwnedPresent(source_key);
}

void DrawPipeline::EmitCounters(std::FILE* f) const {
    if (!f) return;
    std::fprintf(f, "[GS-PIPELINE] draws=%llu same_buffer_flushes=%llu shuffle_snaps=%llu zsrc_arms=%llu"
                    " dropped_materializes=%llu\n",
                 (unsigned long long)draw_seq_, (unsigned long long)same_buffer_flushes_,
                 (unsigned long long)snapped_draws_, (unsigned long long)zsrc_arms_,
                 (unsigned long long)dropped_materializes_);
}

}  // namespace gs
}  // namespace ps2r
