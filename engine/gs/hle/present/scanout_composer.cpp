// gs/hle/present/scanout_composer.cpp -- scanout state, the target grab and the
// display quad.
#include "engine/gs/hle/present/scanout_composer.h"

#include <algorithm>
#include <cstring>

#include "engine/gs/hle/texsrc/texture_decoder.h"

namespace ps2r {
namespace gs {

ScanoutComposer::ScanoutComposer(render::Backend& be, RenderTargetCache& targets)
    : be_(be), targets_(targets) {}

ScanoutComposer::~ScanoutComposer() {
    if (mirror_tex_.id) be_.DestroySurface(mirror_tex_);
}

void ScanoutComposer::SetDispfb(uint32_t fbw_px, uint32_t psm) {
    dispfb_fbw_ = fbw_px;
    dispfb_psm_ = psm;
}

void ScanoutComposer::SetContentDimensions(int w, int h) {
    if (w <= 0 || h <= 0) return;
    content_w_ = w;
    content_h_ = h;
}

void ScanoutComposer::RestoreView(uint32_t color_key, uint32_t buffer_width, uint32_t format,
                                 int content_width, int content_height) {
    scanout_fbp_ = color_key;
    dispfb_fbw_ = buffer_width;
    dispfb_psm_ = format;
    content_w_ = content_width;
    content_h_ = content_height;
}

const RenderTarget* ScanoutComposer::GrabSource() const {
    const RenderTarget* t = targets_.Find(scanout_fbp_);
    if (t && t->views.HasColor()) return t;
    if (targets_.MostRecentDrawnFbp() != RenderTargetCache::kNoFbp) {
        t = targets_.Find(targets_.MostRecentDrawnFbp());
        if (t && t->views.HasColor()) return t;
    }
    return nullptr;
}

// The local-memory arm: the DISPFB tuple decoded over the content dims (the same
// decode the display quad's mirror arm uses), after the deferred deposits under
// the display footprint landed (the footprint is a VRAM read, as in DrawScanout).
// Rows are flipped into the attachment's bottom-up order. False when the tuple is
// unknown (no DISPFB job yet), the dims are unknown, or the PSM is not displayable.
bool ScanoutComposer::GrabLocalMemory(const VramImage& vram, std::vector<uint8_t>* out, int* w, int* h) {
    if (scanout_fbp_ == RenderTargetCache::kNoFbp || dispfb_fbw_ == 0 || content_w_ <= 0 || content_h_ <= 0)
        return false;
    const uint32_t bpp = RenderTarget::Is16BitColor(dispfb_psm_) ? 2u : 4u;
    targets_.FlushPending(PendingRtSyncLedger::Reason::Scanout, scanout_fbp_,
                          dispfb_fbw_ * static_cast<uint32_t>(content_h_) * bpp);
    view_.resize(VramImage::kBytes);
    vram.ReadView(0, VramImage::kBytes, view_.data());
    if (!TextureDecoder::DecodeScanoutBuffer(view_.data(), scanout_fbp_, dispfb_fbw_, dispfb_psm_, content_w_,
                                             content_h_, &rgba_))
        return false;
    const size_t row = static_cast<size_t>(content_w_) * 4;
    out->resize(row * static_cast<size_t>(content_h_));
    for (int y = 0; y < content_h_; ++y)
        std::memcpy(out->data() + static_cast<size_t>(content_h_ - 1 - y) * row,
                    rgba_.data() + static_cast<size_t>(y) * content_w_, row);
    *w = content_w_;
    *h = content_h_;
    return true;
}

bool ScanoutComposer::GrabDisplay(std::vector<uint8_t>* out, int* w, int* h, const VramImage* vram) {
    *w = 0;
    *h = 0;
    last_grab_ = Source::None;
    last_grab_fbp_ = RenderTargetCache::kNoFbp;
    last_grab_fallback_ = false;
    const RenderTarget* t = targets_.Find(scanout_fbp_);
    if (!(t && t->views.HasColor())) {
        if (grab_policy_ == GrabPolicy::LocalMemory && vram && GrabLocalMemory(*vram, out, w, h)) {
            last_grab_ = Source::Mirror;
            last_grab_fbp_ = scanout_fbp_;
            return true;
        }
        t = GrabSource();   // the most recently drawn target
        last_grab_fallback_ = t != nullptr;
    }
    if (!t) return false;
    const int pw = t->identity.PhysicalW(), ph = t->identity.PhysicalH();
    if (pw <= 0 || ph <= 0) return false;
    out->assign(static_cast<size_t>(pw) * ph * 4, 0);
    be_.Readback(t->views.color, render::Rect{0, 0, pw, ph}, out->data(), static_cast<size_t>(pw) * 4);
    *w = pw;
    *h = ph;
    last_grab_ = Source::Target;
    last_grab_fbp_ = t->identity.fbp;
    return true;
}

void ScanoutComposer::PresentBlank(const render::Rect& window) {
    render::ScanoutDesc sd;
    sd.dst = window;
    be_.Present(sd);
}

uint32_t ScanoutComposer::ScanoutFootprintBytes(const RenderTarget& t) {
    const uint32_t bpp = RenderTarget::Is16BitColor(t.identity.render_psm) ? 2u : 4u;
    return uint32_t(t.identity.width) * uint32_t(t.identity.height) * bpp;
}

// The page-ledger resolve: the head page is GPU-authored BY THIS target and no
// page inside the footprint is GPU-authored by a different one (an alias hazard
// -- presenting this target would miss that target's bytes). Cold / CPU-authored
// pages inside decline nothing by themselves; a cold HEAD declines (right after a
// restore the non-native present's full deposit re-authors it -- one-present
// self-heal).
bool ScanoutComposer::ResolvesToTarget(const VramImage& vram, uint32_t fbp, uint32_t size) {
    if (size == 0) return false;
    uint32_t first, count;
    VramImage::PagesOf(VramImage::Span{fbp, size}, &first, &count);
    const PageRecord& head = vram.Page(first);
    if (head.author != PageRecord::Author::GpuTarget || head.author_fbp != fbp) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const PageRecord& p = vram.Page(first + i);
        if (p.author == PageRecord::Author::GpuTarget && p.author_fbp != fbp) return false;
    }
    return true;
}

// The present source: the target at the scanout base with valid content; else the
// most recently drawn target with valid content; then the stale-display pick.
RenderTarget* ScanoutComposer::PickSource(const VramLayout& layout, uint32_t frame_count,
                                          bool* used_fallback, bool* stale_display) {
    *used_fallback = false;
    *stale_display = false;
    RenderTarget* t = targets_.Find(scanout_fbp_);
    if (!t || !t->coherence.HasValidContent()) {
        t = nullptr;
        const uint32_t recent = targets_.MostRecentDrawnFbp();
        if (recent != RenderTargetCache::kNoFbp && recent != scanout_fbp_) {
            RenderTarget* r = targets_.Find(recent);
            if (r && r->coherence.HasValidContent()) {
                t = r;
                *used_fallback = true;
            }
        }
        return t;
    }
    // A display target the game stopped drawing long ago while the main frame
    // buffer is being drawn: present the main buffer (the format round trip when
    // its class is not DISPFB's).
    if (layout.main_fb_fbp != 0 && scanout_fbp_ != layout.main_fb_fbp) {
        RenderTarget* work = targets_.Find(layout.main_fb_fbp);
        if (work && work->coherence.HasValidContent()) {
            const uint32_t disp_age = frame_count - t->coherence.last_draw_frame;
            const uint32_t work_age = frame_count - work->coherence.last_draw_frame;
            if (disp_age > kStaleDisplayFrames && work_age <= kFreshWorkFrames &&
                work->coherence.last_draw_frame > t->coherence.last_draw_frame) {
                *used_fallback = true;
                *stale_display = work->identity.render_psm != dispfb_psm_;
                return work;
            }
        }
    }
    return t;
}

void ScanoutComposer::PresentMirror(const VramImage& vram, uint32_t fbp, uint32_t fbw, uint32_t psm,
                                    const render::Rect& window) {
    const int w = content_w_, h = content_h_;
    if (w <= 0 || h <= 0) return;
    view_.resize(VramImage::kBytes);
    vram.ReadView(0, VramImage::kBytes, view_.data());
    if (!TextureDecoder::DecodeScanoutBuffer(view_.data(), fbp, fbw, psm, w, h, &rgba_)) return;
    if (!mirror_tex_.id || mirror_w_ != w || mirror_h_ != h) {
        if (mirror_tex_.id) be_.DestroySurface(mirror_tex_);
        render::SurfaceDesc d;
        d.width = static_cast<uint32_t>(w);
        d.height = static_cast<uint32_t>(h);
        d.format = render::SurfaceFormat::Rgba8;
        mirror_tex_ = be_.CreateSurface(d);
        mirror_w_ = w;
        mirror_h_ = h;
    }
    be_.Upload(mirror_tex_, render::Rect{0, 0, w, h}, rgba_.data(), static_cast<size_t>(w) * 4);
    render::ScanoutDesc sd;
    sd.source = mirror_tex_;
    sd.src = render::Rect{0, 0, w, h};
    sd.dst = window;   // row 0 = PS2 top, exactly as a target stores it: the same quad
    be_.Present(sd);
}

ScanoutComposer::DrawResult ScanoutComposer::DrawScanout(VramImage& vram, const VramLayout& layout,
                                                         const CrtcClassifier::Snapshot& crtc,
                                                         uint32_t frame_count, uint64_t gfx_epoch,
                                                         const render::Rect& window) {
    DrawResult r;
    if (scanout_fbp_ == RenderTargetCache::kNoFbp) return r;
    RenderTarget* t = PickSource(layout, frame_count, &r.used_fallback, &r.stale_display);
    if (!t) return r;   // nothing with content to show
    r.fbp = t->identity.fbp;
    const uint32_t footprint = ScanoutFootprintBytes(*t);
    // NATIVE: the merge CLASS from the EE register snapshot, the CONFIG TUPLE from
    // the queue-order DISPFB values (the EE tuple is not cross-checked: a restore
    // can leave it stale while the queue tuple is the presented truth), the true
    // DISPFB target (no pick heuristic engaged), and the ledger resolve.
    const bool native = !r.used_fallback && crtc.valid &&
                        crtc.cls == CrtcClassifier::Class::DualIdentical1px &&
                        layout.IsNativeScanout(scanout_fbp_, dispfb_fbw_, dispfb_psm_) &&
                        ResolvesToTarget(vram, scanout_fbp_, footprint);
    // The display footprint is a mirror READ: deferred deposits of OTHER targets
    // under it land first (deposit order precedes this target's own sync).
    targets_.FlushPending(PendingRtSyncLedger::Reason::Scanout, scanout_fbp_, footprint);
    if (native) {
        // Only the native tier elides the per-present deposit: recorded as a
        // hazard-ledger entry, the rows owed to the pages.
        targets_.Ledger().Record(scanout_fbp_, footprint, gfx_epoch);
        vram.NoteGpuElided(VramImage::Span{scanout_fbp_, footprint}, scanout_fbp_, 0, t->identity.height);
        r.native = true;
    } else {
        // Full mirror deposit at the SCANOUT base (a stand-in target lands where
        // the display reads), in the DISPFB format for the stale-display round trip.
        targets_.SyncDrawnDeferred(*t, gfx_epoch, r.stale_display ? dispfb_psm_ : ReadbackEngine::kNoPsmOverride,
                                   scanout_fbp_);
    }
    if (!r.stale_display && t->views.HasColor() && t->identity.width > 0 && t->identity.height > 0 &&
        content_w_ > 0 && content_h_ > 0) {
        // Direct-RT present: the colour attachment over the content rect (clamped
        // to the grow-only backing store), PS2 top at row 0.
        const int s = t->identity.scale;
        render::ScanoutDesc sd;
        sd.source = t->views.color;
        sd.src = render::Rect{0, 0, std::min(content_w_, t->identity.width) * s,
                              std::min(content_h_, t->identity.height) * s};
        sd.dst = window;
        be_.Present(sd);
        r.source = Source::Target;
        return r;
    }
    PresentMirror(vram, scanout_fbp_, dispfb_fbw_, dispfb_psm_, window);
    r.source = Source::Mirror;
    return r;
}

}  // namespace gs
}  // namespace ps2r
