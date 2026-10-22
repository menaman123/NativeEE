// gs/hle/texsrc/texture_resolver.cpp -- TextureResolver: entry, predicates, the
// colour-serve outputs and the four tiers.
#include "engine/gs/hle/texsrc/texture_resolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "engine/gs/hle/texsrc/mip_sampler.h"
#include "engine/gs/hle/texsrc/texture_decoder.h"
#include "engine/gs/hle/texsrc/uv_bbox.h"
#include "engine/gs/hle/texsrc/uv_region.h"

namespace ps2r {
namespace gs {

TextureResolver::TextureResolver(render::Backend& be, VramImage& vram, RenderTargetCache& targets,
                                 TargetRefresher& refresher, const VramLayout& layout)
    : be_(be), vram_(vram), targets_(targets), refresher_(refresher), layout_(layout),
      cache_(be), owned_(be), clut_(be) {}

bool TextureResolver::Is16BitClass(uint32_t psm) {
    return psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A;
}

bool TextureResolver::IsIndexedPsm(uint32_t psm) {
    return psm == 0x13 || psm == 0x14 || psm == 0x1B || psm == 0x24 || psm == 0x2C;
}

bool TextureResolver::IsZPsm(uint32_t psm) {
    return psm == 0x30 || psm == 0x31 || psm == 0x32 || psm == 0x3A;
}

bool TextureResolver::RtKeepServe(bool valid, bool bpp_compatible, uint64_t footprint_upload_epoch,
                                  uint64_t last_draw_epoch) {
    return valid && bpp_compatible && footprint_upload_epoch <= last_draw_epoch;
}

uint64_t TextureResolver::WindowEpoch(uint32_t addr, uint32_t size) const {
    uint32_t first, count;
    VramImage::PagesOf(VramImage::Span{addr, size}, &first, &count);
    uint64_t e = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t pe = vram_.Page(first + i).upload_epoch;
        if (pe > e) e = pe;
    }
    return e;
}

// Read-footprint page-epoch arbitration: the 8 KiB pages the (tbp / tbw page
// grid, tw x th texel rect) read touches, for the colour classes whose page
// geometry is known. UINT64_MAX = outside the provable class.
uint64_t TextureResolver::ReadRectUploadEpoch(const TexReadView& v) const {
    uint32_t pw, ph;
    switch (v.psm) {
        case 0x00: case 0x01: case 0x30: case 0x31: pw = 64; ph = 32; break;
        case 0x02: case 0x0A: case 0x32: case 0x3A: pw = 64; ph = 64; break;
        default: return UINT64_MAX;
    }
    if (v.tw == 0 || v.th == 0 || v.tw > 10 || v.th > 10) return UINT64_MAX;
    if ((v.tbp % VramImage::kPageBytes) != 0) return UINT64_MAX;
    if (v.tbw == 0 || (v.tbw % pw) != 0) return UINT64_MAX;
    const uint32_t tw = 1u << v.tw, th = 1u << v.th;
    if (tw > v.tbw) return UINT64_MAX;   // wrapping row reads
    const uint32_t pages_per_row = v.tbw / pw;
    const uint32_t page_cols = (tw + pw - 1) / pw, page_rows = (th + ph - 1) / ph;
    const uint32_t base_page = v.tbp / VramImage::kPageBytes;
    uint64_t e = 0;
    for (uint32_t r = 0; r < page_rows; r++)
        for (uint32_t c = 0; c < page_cols; c++) {
            const uint32_t p = base_page + r * pages_per_row + c;
            if (p >= VramImage::kPageCount) return UINT64_MAX;
            e = std::max(e, vram_.Page(p).upload_epoch);
        }
    return e;
}

void TextureResolver::ClassifyShuffle(uint32_t render_psm, uint32_t read_psm, uint8_t* type,
                                      float* scale_u) {
    using T = TextureSource::Transform;
    const uint32_t rb = RenderTarget::FrameBitsPerPixel(render_psm);
    const uint32_t tb = RenderTarget::FrameBitsPerPixel(read_psm);
    *type = T::kShuffleNone;
    *scale_u = 1.0f;
    if (rb == 0 || tb == 0 || rb == tb) return;
    if (read_psm == 0x13 && rb >= 24) { *type = T::kShuffleChannel; return; }
    if (render_psm == 0x30 || render_psm == 0x31 || render_psm == 0x32) { *type = T::kShuffleDepthToColor; return; }
    *type = (rb > tb) ? T::kShuffle32to16 : T::kShuffle16to32;
    *scale_u = static_cast<float>(rb) / static_cast<float>(tb);
}

// The CLUT can be served from VRAM only when it is a plain CT32 CLUT at CSA 0 and
// its 1 KiB does not overlap the footprint of any target with valid content (a
// target-authored CLUT would need a deposit first).
bool TextureResolver::ClutServable(const TexReadView& v) const {
    if (v.cpsm != 0x00 || v.csa != 0) return false;
    const uint32_t c0 = v.cbp, c1 = v.cbp + ClutSource::kClutBytes;
    if (c1 <= c0) return false;
    bool ok = true;
    const_cast<RenderTargetCache&>(targets_).ForEach([&](RenderTarget& t) {
        if (!t.coherence.HasValidContent()) return;
        const uint32_t bpp = Is16BitClass(t.identity.render_psm) ? 2u : 4u;
        const uint32_t f0 = t.identity.fbp;
        const uint32_t f1 = f0 + static_cast<uint32_t>(t.identity.width) *
                                     static_cast<uint32_t>(t.identity.height) * bpp;
        if (c0 < f1 && f0 < c1) ok = false;
    });
    return ok;
}

// The indexed-serve voucher: the target's texels are the VRAM content when no EE
// upload over its footprint is newer than the epoch at which it was last made
// coherent in the wanted class (class 1 = 32-bit, 2 = 16-bit); with no voucher
// only a never-uploaded footprint is fresh.
bool TextureResolver::IdxFresh(uint32_t base, const RenderTarget& t, uint8_t want_class) const {
    const uint32_t fbpp = (want_class == 2) ? 2u : 4u;
    const uint32_t foot = static_cast<uint32_t>(t.identity.height) * t.identity.render_fbw * fbpp;
    const uint64_t now = WindowEpoch(base, foot);
    if (t.coherence.vram_coherent_class == want_class) return now <= t.coherence.vram_coherent_epoch;
    return now == 0;
}

void TextureResolver::FillSampler(const TexReadView& v, TextureSource* out) const {
    out->sampler.mag_linear = v.mmag;
    out->sampler.mmin = v.mmin;
    out->sampler.wms = v.wms;
    out->sampler.wmt = v.wmt;
    out->sampler.minu = v.minu;
    out->sampler.maxu = v.maxu;
    out->sampler.minv = v.minv;
    out->sampler.maxv = v.maxv;
}

// Rescue: the target's rows within the read window are deposited into VRAM at the
// target's own last-draw epoch, ENCODED IN THE READ'S FORMAT, before the read
// declines to a decode, so the decode observes the GPU content rather than stale
// mirror bytes.
void TextureResolver::RescueDeposit(const DrawIr& ir, RenderTarget& t) {
    const TexReadView& v = ir.tex;
    LogicalRect rr;
    rr.x0 = 0;
    rr.y0 = 0;
    rr.x1 = (v.tw > 0 && v.tw <= 10) ? (1 << v.tw) : t.identity.width;
    rr.y1 = (v.th > 0 && v.th <= 10) ? (1 << v.th) : t.identity.height;
    const uint32_t efbw = (v.tbw >= 64 && (v.tbw & 63) == 0) ? v.tbw : 0;
    targets_.Readback().SyncRtToVram(be_, t, vram_, t.coherence.last_draw_gfx_epoch, &rr, efbw, v.psm);
}

// The deferred 16-bit writebacks under the SERVING TARGET's footprint (its own
// width x height x render bpp from TBP -- the same span the writeback protects,
// never the TEX0 window) land before the rescue / class-mismatch deposit reads them.
void TextureResolver::FlushPendingUnderRead(const TexReadView& v, const RenderTarget& t) {
    targets_.FlushPending(PendingRtSyncLedger::Reason::Ftt, v.tbp, RenderTargetCache::FootprintBytes(t));
}

void TextureResolver::EvictUnderDraw(const DrawIr& ir, const render::DrawResult& res, int render_h) {
    uint32_t start = 0, size = 0;
    if (TargetRefresher::EvictRangeUnderDraw(ir, res, render_h, &start, &size)) {
        cache_.EvictInRange(start, size);
        owned_.EvictRange(start, size, true);
    }
    refresher_.EvictUnderDraw(ir, res, render_h);
}

void TextureResolver::OnCpuWrite(uint32_t addr, uint32_t size) {
    if (size == 0) return;
    vram_.NoteCpuWrite(VramImage::Span{addr, size});
    cache_.EvictInRange(addr, size);
    owned_.EvictRange(addr, size, false);
    refresher_.MarkPagesCpuDirty(addr, size);
}

// The D1 FMV blit (the title's stage texture drawn onto the display buffer under
// REPEAT): the decoded region beyond the last sampled texel column / row repeats
// that edge, so the wrap never fetches the stale strip past the movie frame.
void TextureResolver::ApplyFmvEdgeClamp(const DrawIr& ir, const ContentKey::Rect& rgn, int dw, int dh) {
    const TexReadView& v = ir.tex;
    if (!layout_.IsFmvBlitSignature(ir.raster.frame.fbp, v.tbp, v.psm, v.tw, v.th)) return;
    if (!(v.wms == 0 || v.wmt == 0) || texels_.empty()) return;
    const UvBbox::Result bb = UvBbox::Scan(ir.verts.data(), ir.verts.size(), v.fst, 1 << v.tw, 1 << v.th);
    if (bb.state != UvBbox::kOk) return;
    long cw = static_cast<long>(std::ceil(double(bb.u1) - 0.5)) - rgn.x0;
    long ch = static_cast<long>(std::ceil(double(bb.v1) - 0.5)) - rgn.y0;
    cw = std::max(1L, std::min(cw, long(dw)));
    ch = std::max(1L, std::min(ch, long(dh)));
    if (v.wms == 0 && cw < dw) {
        for (long y = 0; y < dh; ++y) {
            uint32_t* row = texels_.data() + size_t(y) * dw;
            for (long x = cw; x < dw; ++x) row[x] = row[cw - 1];
        }
    }
    if (v.wmt == 0 && ch < dh) {
        const uint32_t* erow = texels_.data() + size_t(ch - 1) * dw;
        for (long y = ch; y < dh; ++y) std::memcpy(texels_.data() + size_t(y) * dw, erow, size_t(dw) * 4);
    }
}

// A colour-class serve: the texel space is the TARGET's dims; a plain (non-alt16)
// serve classifies the render-vs-read format shuffle and folds its u scale into
// the width; an STQ read of a plain unshuffled serve is prescaled by its declared
// TEX0 window (the target may have outgrown it) while the width still divides by
// the target dims.
void TextureResolver::ServeColor(const DrawIr& ir, const Context& ctx, const RenderTarget& t,
                                 render::SurfaceId surface, bool alt16_serve, TextureSource* out) const {
    (void)ctx;
    const TexReadView& v = ir.tex;
    TextureSource::Transform& tr = out->transform;
    out->owner.kind = TextureSource::OwnerKind::Surface;
    out->owner.surface = surface;
    tr.draw_scale = t.identity.scale;
    tr.uv_tex_w = t.identity.width;
    tr.uv_tex_h = t.identity.height;
    if (!alt16_serve) {
        ClassifyShuffle(t.identity.render_psm, v.psm, &tr.shuffle_type, &tr.shuffle_uv_scale_u);
        tr.is_shuffle = tr.shuffle_type != TextureSource::Transform::kShuffleNone;
        if (tr.shuffle_uv_scale_u != 1.0f) {
            tr.uv_tex_w = static_cast<int32_t>(static_cast<float>(tr.uv_tex_w) * tr.shuffle_uv_scale_u);
            if (tr.uv_tex_w < 1) tr.uv_tex_w = 1;
        }
    }
    tr.uv_pre_w = tr.uv_tex_w;
    tr.uv_pre_h = tr.uv_tex_h;
    const int tex_w = 1 << v.tw, tex_h = 1 << v.th;
    if (!v.fst && !alt16_serve && tr.shuffle_type == TextureSource::Transform::kShuffleNone &&
        tex_w > 0 && tex_h > 0 && (tr.uv_pre_w != tex_w || tr.uv_pre_h != tex_h)) {
        tr.uv_pre_w = tex_w;
        tr.uv_pre_h = tex_h;
    }
}

// A served read (target / depth / self) leaves one trace line naming the tier.
static void TraceServe(TexresTrace& tr, TextureDumpSink& sink, const DrawIr& ir, const TextureResolver::Context& ctx,
                       TexresTrace::Outcome o) {
    if (sink.Armed()) sink.NoteServe(ir.tex, TexresTrace::OutcomeName(o), ctx.frame_count, ctx.draw_seq);
    if (!tr.Wants(ir.tex)) return;
    TexresTrace::Line l;
    l.draw_seq = ctx.draw_seq;
    l.frame_count = ctx.frame_count;
    l.draw_epoch = ir.vram_epoch;
    l.tex = ir.tex;
    l.outcome = o;
    tr.Note(std::move(l));
}

TextureSource TextureResolver::ResolveRead(const DrawIr& ir, const Context& ctx) {
    TextureSource out;   // every per-call result starts from the default
    if (!ir.tex_enabled) {
        out.declined = TextureSource::Declined::NoTexture;
        return out;
    }
    if (touch_) touch_(ir.tex.tbp);
    FillSampler(ir.tex, &out);
    out.transform.uv_tex_w = 1 << ir.tex.tw;
    out.transform.uv_tex_h = 1 << ir.tex.th;
    out.transform.uv_pre_w = out.transform.uv_tex_w;
    out.transform.uv_pre_h = out.transform.uv_tex_h;
    if (RenderTarget* t = targets_.Find(ir.tex.tbp)) {
        if (MatchTarget(ir, ctx, *t, &out)) {
            TraceServe(trace_, dump_sink_, ir, ctx, (out.owner.fmt_case == 0 && ir.tex.tbp == ctx.active_fbp)
                                            ? TexresTrace::Outcome::SelfRead
                                            : TexresTrace::Outcome::TargetServe);
            // A colour-class serve of the draw's own target binds nothing: the read
            // stays in textured UV mode over its TEX0 window with no sampler.
            if (out.owner.fmt_case == 0 && ir.tex.tbp == ctx.active_fbp) {
                const TextureSource::Sampler sampler = out.sampler;
                out = TextureSource();
                out.sampler = sampler;
                out.transform.uv_tex_w = 1 << ir.tex.tw;
                out.transform.uv_tex_h = 1 << ir.tex.th;
                out.transform.uv_pre_w = out.transform.uv_tex_w;
                out.transform.uv_pre_h = out.transform.uv_tex_h;
                out.declined = TextureSource::Declined::SelfRead;
            }
            out.enabled = true;
            RefreshZSource(ir, ctx, out);   // a tier-1 result never carries a depth serve
            return out;
        }
        RefreshZSource(ir, ctx, out);
        DecodeFallback(ir, ctx, &out);   // a target at TBP: every decline decodes
        return out;
    }
    if (MatchDepth(ir, ctx, &out)) {
        TraceServe(trace_, dump_sink_, ir, ctx, TexresTrace::Outcome::DepthServe);
        out.enabled = true;
        return out;
    }
    RefreshZSource(ir, ctx, out);
    DecodeFallback(ir, ctx, &out);
    return out;
}

// The Z-source refresh: every Z-format read that was not served from a depth
// attachment decodes VRAM, so the Z buffer containing TBP is written back first
// -- the writer whose Z write is the most recent, provided it wrote within the
// last two frames (a 30 fps title draws every other vsync) and holds content.
// The deposit lands AFTER any colour deposit the tier-1 decline made over the
// same bytes: the decode reads the Z image, as the hardware read does.
void TextureResolver::RefreshZSource(const DrawIr& ir, const Context& ctx, const TextureSource& out) {
    const TexReadView& v = ir.tex;
    if (!IsZPsm(v.psm) || out.transform.depth_fmt != 0) return;
    RenderTarget* best = nullptr;
    targets_.ForEach([&](RenderTarget& t) {
        const CoherenceState& c = t.coherence;
        if (c.last_zbp == 0xFFFFFFFFu || !c.last_z_wrote || !c.HasValidContent()) return;
        if (ctx.frame_count - c.last_z_write_frame > kRecentFrames) return;
        const uint32_t nz = c.last_zpsm & 0xF;
        const uint32_t native_bpp = (nz == 0x2 || nz == 0xA) ? 2u : 4u;
        const uint32_t zbuf_bytes =
            static_cast<uint32_t>(t.identity.width) * static_cast<uint32_t>(t.identity.height) * native_bpp;
        if (!(v.tbp >= c.last_zbp && v.tbp < c.last_zbp + zbuf_bytes)) return;
        if (!best || c.last_z_seq > best->coherence.last_z_seq) best = &t;
    });
    if (best) targets_.Readback().SyncZToVram(be_, *best, vram_, ir.vram_epoch);
}

bool TextureResolver::MatchTarget(const DrawIr& ir, const Context& ctx, RenderTarget& t,
                                  TextureSource* out) {
    const TexReadView& v = ir.tex;
    // (a) 16-bit read over a buffer that has held 32-bit content: the companion.
    if (RenderTarget::Is16BitColor(v.psm)) {
        if (t.identity.has_32bit_content && t.views.HasColor()) {
            t.EnsureAlt16(be_);
            t.RefreshAlt16FromMain(be_);
            ServeColor(ir, ctx, t, t.views.alt16, true, out);
            out->owner.alt16_serve = true;
            out->owner.alt16_main = t.views.color;
            return true;
        }
        if (t.views.alt16_has_content && t.views.HasAlt16()) {
            ServeColor(ir, ctx, t, t.views.alt16, true, out);
            out->owner.alt16_serve = true;
            out->owner.alt16_main = t.views.HasColor() ? t.views.color : render::SurfaceId{};
            return true;
        }
    }
    // (b) the scene accumulator serves its colour for any plain colour read.
    if (layout_.IsSceneAccum(t.identity.fbp) && t.views.HasColor() && !IsIndexedPsm(v.psm) &&
        !RenderTarget::Is16BitColor(v.psm)) {
        ServeColor(ir, ctx, t, t.views.color, false, out);
        return true;
    }
    if (!t.coherence.HasValidContent()) {
        out->declined = TextureSource::Declined::StaleTarget;
        return false;
    }
    if (IsIndexedPsm(v.psm)) {
        if (MatchIndexed(ir, ctx, t, out)) return true;
        // (c) declined: an indexed read of a recently drawn 32-bit-class target
        // decodes its DEPOSITED bytes (CT32 layout; a CT24 target keeps byte 3 --
        // the alpha lane belongs to whoever else authored it); a 16-bit-class
        // target deposits nothing here. The decode samples native texel centres.
        const uint32_t rpsm = t.identity.render_psm;
        const bool recent = ctx.frame_count - t.coherence.last_draw_frame <= kRecentFrames;
        if (recent && !Is16BitClass(rpsm)) {
            const uint32_t enc = (rpsm == 0x01) ? 0x01u : 0x00u;
            FlushPendingUnderRead(v, t);
            targets_.SyncDrawn(t, ir.vram_epoch, enc);
        }
        out->transform.uv_native = true;
        out->declined = TextureSource::Declined::ClutUnservable;
        return false;
    }
    return MatchColor(ir, ctx, t, out);
}

// (d) colour read of a target with valid content.
bool TextureResolver::MatchColor(const DrawIr& ir, const Context& ctx, RenderTarget& t,
                                 TextureSource* out) {
    const TexReadView& v = ir.tex;
    const uint32_t rpsm = t.identity.render_psm;
    uint32_t bpp = RenderTarget::FrameBitsPerPixel(rpsm);
    if (bpp == 0) bpp = 32;
    const uint32_t tex_size = static_cast<uint32_t>(t.identity.width) * t.identity.height * (bpp / 8);
    const bool recent = ctx.frame_count - t.coherence.last_draw_frame <= kRecentFrames;
    const bool r16 = Is16BitClass(rpsm), t16 = Is16BitClass(v.psm);
    const bool bpp_compatible = r16 == t16;
    // The keep test runs only for a target NOT drawn recently whose pages are
    // CPU-dirty: serve unless a genuine EE upload newer than the last draw overlaps
    // the read -- first over the target's footprint, then over exactly the pages
    // the read touches; a decline rescues the read window first.
    if (!recent && refresher_.ArePagesCpuDirty(v.tbp, tex_size)) {
        bool keep = RtKeepServe(true, bpp_compatible, WindowEpoch(v.tbp, tex_size),
                                t.coherence.last_draw_gfx_epoch);
        if (!keep && bpp_compatible) {
            const uint64_t rr = ReadRectUploadEpoch(v);
            if (rr != UINT64_MAX && rr <= t.coherence.last_draw_gfx_epoch) keep = true;
        }
        if (!keep) {
            FlushPendingUnderRead(v, t);
            RescueDeposit(ir, t);
            out->declined = bpp_compatible ? TextureSource::Declined::StaleTarget
                                           : TextureSource::Declined::ClassMismatch;
            return false;
        }
    }
    // Format mismatch across the 16 / 32 class boundary: the attachment's RGBA8
    // cannot stand for the reinterpreted bytes -- sync the drawn rows in the
    // render format and let the decode read them in the texture format. The one
    // exception: a CT32 read of a CT16S target samples the target's colours.
    if (rpsm != v.psm && !bpp_compatible) {
        FlushPendingUnderRead(v, t);
        targets_.SyncDrawn(t, ir.vram_epoch);
        if (rpsm == 0x0A && v.psm == 0x00) {
            ServeColor(ir, ctx, t, t.views.color, false, out);
            return true;
        }
        out->declined = TextureSource::Declined::ClassMismatch;
        return false;
    }
    ServeColor(ir, ctx, t, t.views.color, false, out);
    return true;
}

// The lane-live rescue: a T8H read the pair chain declined for ANY cause is
// served as fmt 5 when both halves' Z16-view masked lane draws of THIS frame are
// still the last authors of their views; each half binds its LANE surface (the
// alt16 companion when the lane draw routed there, else the main image).
bool TextureResolver::LaneLiveRescue(const DrawIr& ir, const Context& ctx, RenderTarget& t,
                                     render::SurfaceId* lane1, render::SurfaceId* lane2) const {
    const TexReadView& v = ir.tex;
    if (!(v.tbw >= 64 && (v.tbw & 63) == 0 && t.identity.height > 0)) return false;
    auto lane_tex = [&](const RenderTarget& x) -> render::SurfaceId {
        const CoherenceState& c = x.coherence;
        if (!c.HasValidContent() || x.identity.scale != 1) return render::SurfaceId{};
        if (x.identity.width != static_cast<int32_t>(v.tbw)) return render::SurfaceId{};
        if (ctx.frame_count - c.last_draw_frame > kRecentFrames) return render::SurfaceId{};
        // seq != 0 = a real lane-class commit was recorded (author seqs start at 1).
        if (c.lane_alt.seq != 0 && c.lane_alt.frame == ctx.frame_count &&
            c.lane_alt.seq == c.alt_author_seq && x.views.HasAlt16())
            return x.views.alt16;
        if (c.lane_main.seq != 0 && c.lane_main.frame == ctx.frame_count &&
            c.lane_main.seq == c.main_author_seq && x.views.HasColor())
            return x.views.color;
        return render::SurfaceId{};
    };
    const render::SurfaceId t1 = lane_tex(t);
    if (!t1.id) return false;
    const uint32_t partner_fbp = v.tbp + static_cast<uint32_t>(t.identity.height) * v.tbw * 2u;
    const RenderTarget* p = targets_.Find(partner_fbp);
    if (!p || p->identity.height != t.identity.height) return false;
    const render::SurfaceId t2 = lane_tex(*p);
    if (!t2.id || !ClutServable(v)) return false;
    *lane1 = t1;
    *lane2 = t2;
    return true;
}

// (c) indexed read of a target with valid content: only a T8H read serves (the
// T8-of-CT16 view is not live; T4 classes are by-design decodes).
bool TextureResolver::MatchIndexed(const DrawIr& ir, const Context& ctx, RenderTarget& t,
                                   TextureSource* out) {
    const TexReadView& v = ir.tex;
    if (v.psm != 0x1B) return false;
    const bool recent = ctx.frame_count - t.coherence.last_draw_frame <= kRecentFrames;
    const bool selfrd = v.tbp == ctx.active_fbp;
    const uint32_t rpsm = t.identity.render_psm;
    uint8_t fmt = 0;
    render::SurfaceId surface, partner_tex;
    // The pair chain: recent, reader TBW == FBW, servable CLUT, full-width target.
    if (recent && v.tbw != 0 && v.tbw == t.identity.render_fbw && ClutServable(v) &&
        t.views.HasColor() && t.identity.width == static_cast<int32_t>(t.identity.render_fbw)) {
        if (!Is16BitClass(rpsm)) {
            // 32-bit class: the self serve samples the pre-draw destination copy
            // (no VRAM identity claimed); another target must be upload-fresh.
            if (selfrd || IdxFresh(v.tbp, t, 1)) fmt = 4;
        } else if ((rpsm == 0x02 || rpsm == 0x0A) && !selfrd) {
            const uint32_t partner_fbp =
                v.tbp + static_cast<uint32_t>(t.identity.height) * t.identity.render_fbw * 2u;
            RenderTarget* p = targets_.Find(partner_fbp);
            const bool pair_ok = p && p->identity.render_fbw == t.identity.render_fbw &&
                                 p->identity.height == t.identity.height &&
                                 p->identity.width == static_cast<int32_t>(p->identity.render_fbw) &&
                                 p->coherence.HasValidContent() &&
                                 ctx.frame_count - p->coherence.last_draw_frame <= kRecentFrames &&
                                 p->views.HasColor();
            // Admission: BOTH halves' last committed draw recorded the index-lane
            // keep mask (the lane is 100 % draw-written), THEN no EE upload landed
            // over either half after its last draw.
            const bool lane_authored =
                pair_ok && t.coherence.sync_keep_mask16 == RenderTarget::kIndexLaneKeep16 &&
                p->coherence.sync_keep_mask16 == RenderTarget::kIndexLaneKeep16;
            if (lane_authored) {
                const uint32_t foot_t = static_cast<uint32_t>(t.identity.height) * t.identity.render_fbw * 2u;
                const uint32_t foot_p = static_cast<uint32_t>(p->identity.height) * p->identity.render_fbw * 2u;
                if (!refresher_.ArePagesCpuDirty(v.tbp, foot_t) &&
                    !refresher_.ArePagesCpuDirty(partner_fbp, foot_p)) {
                    fmt = 5;
                    partner_tex = p->views.color;
                }
            }
        }
    }
    if (fmt == 0 && !selfrd && LaneLiveRescue(ir, ctx, t, &surface, &partner_tex)) fmt = 5;
    if (fmt == 0) return false;
    // A deferred 16-bit writeback under the CLUT page lands before the palette read.
    targets_.FlushPending(PendingRtSyncLedger::Reason::Ftt, v.cbp % VramImage::kBytes, VramImage::kPageBytes);
    const render::SurfaceId clut = clut_.Ensure(v.cbp, false, vram_);
    if (!clut.id) return false;
    if (fmt == 4 && selfrd && t.identity.scale > 1) return false;   // native-texel self serve only
    out->owner.kind = TextureSource::OwnerKind::Surface;
    out->owner.surface = surface.id ? surface : t.views.color;
    out->owner.fmt_case = fmt;
    out->owner.self_serve = (fmt == 4) && selfrd;
    out->transform.idx_fmt = fmt;
    out->transform.idx_self = out->owner.self_serve ? 1u : 0u;
    out->transform.uv_tex_w = t.identity.width;
    out->transform.uv_tex_h = t.identity.height;
    out->transform.uv_pre_w = t.identity.width;
    out->transform.uv_pre_h = t.identity.height;
    out->transform.draw_scale = t.identity.scale;
    out->owner.clut = clut;
    out->owner.partner = partner_tex;
    return true;
}

// Depth-as-texture: no target is keyed at TBP; the Z buffers are searched.
bool TextureResolver::MatchDepth(const DrawIr& ir, const Context& ctx, TextureSource* out) {
    const TexReadView& v = ir.tex;
    if (!IsZPsm(v.psm)) return false;
    // Containment match over the Z-writing targets (the read may be a window into
    // a larger native-format Z buffer); real GS has ONE z image per address, the
    // latest write -- the most recent Z writer wins.
    RenderTarget* best = nullptr;
    targets_.ForEach([&](RenderTarget& t) {
        const CoherenceState& c = t.coherence;
        if (c.last_zbp == 0xFFFFFFFFu || !c.last_z_wrote || !c.HasValidContent()) return;
        const uint32_t nz = c.last_zpsm & 0xF;
        const uint32_t native_bpp = (nz == 0x2 || nz == 0xA) ? 2u : 4u;
        const uint32_t zbuf_bytes =
            static_cast<uint32_t>(t.identity.width) * static_cast<uint32_t>(t.identity.height) * native_bpp;
        if (!(v.tbp >= c.last_zbp && v.tbp < c.last_zbp + zbuf_bytes)) return;
        if (!best || c.last_z_seq > best->coherence.last_z_seq) best = &t;
    });
    if (!best) {
        out->declined = TextureSource::Declined::NoDepthHistory;
        return false;
    }
    const CoherenceState& c = best->coherence;
    const uint32_t nz = c.last_zpsm & 0xF;
    const uint32_t delta = v.tbp - c.last_zbp;
    // Page geometry from the TARGET's FBW: one 64x64 Z16 page row is one 64x32
    // Z32 page-row pair, so whole-page-row offsets map to multiples of 32 Z32 rows.
    const uint32_t ppr = best->identity.render_fbw / 64;
    const uint32_t page_row_bytes = 8192u * ppr;
    const bool aligned = ppr > 0 && (delta % page_row_bytes) == 0u;
    const uint32_t zoff = aligned ? (delta / page_row_bytes) * 32u : 0u;
    const uint32_t height = static_cast<uint32_t>(best->identity.height);
    uint8_t fmt = 0;
    if (best->identity.fbp == ctx.active_fbp) {
        fmt = 0;   // the draw's own target: never its live depth
    } else if (!(v.tbw != 0 && v.tbw == best->identity.render_fbw) || !best->views.depth.id) {
        fmt = 0;
    } else if (v.psm == 0x30 && nz == 0x0) {
        if (aligned && zoff < height) fmt = 1;
    } else if (v.psm == 0x32 && nz == 0x0) {
        if (aligned && zoff < height) fmt = 3;
    } else if ((v.psm == 0x32 || v.psm == 0x3A) && (nz == 0x2 || nz == 0xA) && delta == 0) {
        fmt = 2;
    }
    if (fmt == 0) {
        // Every other case: the Z buffer is written back in its native format at
        // its native address and the read decodes the aliased bytes.
        targets_.Readback().SyncZToVram(be_, *best, vram_, ir.vram_epoch);
        out->declined = TextureSource::Declined::ClassMismatch;
        return false;
    }
    out->owner.kind = TextureSource::OwnerKind::Surface;
    out->owner.surface = best->views.depth;
    out->owner.fmt_case = fmt;
    out->transform.depth_fmt = fmt;
    out->transform.uv_tex_w = best->identity.width;
    out->transform.uv_tex_h = best->identity.height;
    out->transform.uv_pre_w = best->identity.width;
    out->transform.uv_pre_h = best->identity.height;
    out->transform.draw_scale = best->identity.scale;
    out->transform.z_row_offset = (fmt == 2) ? 0 : static_cast<int32_t>(zoff);
    return true;
}

// The UV-bounded decode rect for this draw (false = full decode). A register
// tuple that fragments into too many distinct rects is sticky-forced to full.
bool TextureResolver::DeriveRegion(const DrawIr& ir, int tex_w, int tex_h, ContentKey::Rect* rgn) {
    const TexReadView& v = ir.tex;
    if (!UvRegion::Eligible(v.psm, ir.verts.size(), MipSampler::Capture(v).levels != 0)) return false;
    const UvBbox::Result bb = UvBbox::Scan(ir.verts.data(), ir.verts.size(), v.fst, tex_w, tex_h);
    const UvRegion::Result rr = UvRegion::Derive(bb, v, tex_w, tex_h);
    if (rr.cls != UvRegion::kRect) return false;
    const uint32_t tup[6] = {v.tbp, v.psm, v.tw, v.th, v.tbw, v.cbp};
    const uint64_t tk = ContentKey::Fnv1aStep(1469598103934665603ull, tup, sizeof(tup));
    RectVariants& rec = rect_variants_[tk];
    if (rec.sticky_full) return false;
    const uint64_t rkey = (static_cast<uint64_t>(static_cast<uint16_t>(rr.x0)) << 48) |
                          (static_cast<uint64_t>(static_cast<uint16_t>(rr.y0)) << 32) |
                          (static_cast<uint64_t>(static_cast<uint16_t>(rr.w)) << 16) |
                          static_cast<uint64_t>(static_cast<uint16_t>(rr.h));
    rec.rects.insert(rkey);
    if (rec.rects.size() > kMaxRectVariants) {
        rec.sticky_full = true;
        rec.rects.clear();
        return false;
    }
    rgn->x0 = rr.x0;
    rgn->y0 = rr.y0;
    rgn->w = rr.w;
    rgn->h = rr.h;
    return true;
}

uint8_t* TextureResolver::Scratch(std::vector<uint8_t>& buf, size_t n) {
    if (buf.size() < n) buf.resize(n);
    return buf.data();
}

render::SurfaceId TextureResolver::DecodeAndUpload(const DrawIr& ir, const Context& ctx, const ContentKey::Rect& rgn,
                                                   bool sub, int dw, int dh, const MipSampler::Chain& mips,
                                                   uint8_t* levels) {
    const TexReadView& v = ir.tex;
    // The decoder addresses texels through the composed 4 MiB view (pending-shadow
    // overlay included); only a miss reads it. Nothing wrote VRAM since the key's
    // windows were read, so the view holds exactly the bytes the key hashed -- and
    // the key's inputs ARE the decode's reach: the level-0 footprint (every page the
    // texture touches, BlockDecode::DecodeFootprint), the CLUT page of an indexed
    // read and the authored mip windows. Those spans are read into the view at their
    // own offsets (a 4 MiB scratch, bytes outside them untouched and unread) instead
    // of the whole image: a decode costs its texture, not 4 MiB (view_bytes).
    Scratch(view_, VramImage::kBytes);
    auto read_span = [this](uint32_t start, uint32_t size) {
        if (size == 0) return;
        if (size > VramImage::kBytes) size = VramImage::kBytes;
        const uint32_t a = start % VramImage::kBytes;
        const uint32_t first = std::min<uint32_t>(size, VramImage::kBytes - a);
        vram_.ReadView(a, first, view_.data() + a);
        if (first < size) vram_.ReadView(0, size - first, view_.data());
        census_.view_bytes += size;
    };
    uint32_t fp_start = 0, fp_size = 0;
    ContentKey::Footprint(v, &fp_start, &fp_size);
    read_span(fp_start, fp_size);
    if (IsIndexedPsm(v.psm)) read_span(v.cbp, VramImage::kPageBytes);
    for (uint8_t i = 0; i < mips.levels && !sub; ++i) read_span(mips.level[i].fp_start, mips.level[i].fp_size);
    census_.decodes++;
    warm_cur_.decodes++;
    auto now_ns = [] {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    };
    const uint64_t t_decode0 = now_ns();
    if (sub) TextureDecoder::DecodeRect(view_.data(), v, rgn.x0, rgn.y0, dw, dh, &heal_, &layout_, &texels_);
    else TextureDecoder::Decode(view_.data(), v, &heal_, &layout_, &texels_);
    uint64_t decode_ns = now_ns() - t_decode0;
    if (dump_sink_.Armed()) {
        // The texture oracle sees the decode as the GPU will sample it (before the FMV
        // edge clamp, which is a sampling workaround, not a decode rule).
        TextureDumpSink::Decode rec;
        rec.tex = &v;
        rec.view = view_.data();
        rec.texels = texels_.data();
        rec.dw = dw;
        rec.dh = dh;
        rec.sub = sub;
        rec.x0 = rgn.x0;
        rec.y0 = rgn.y0;
        rec.fp_start = fp_start;
        rec.fp_size = fp_size;
        rec.frame_count = ctx.frame_count;
        rec.draw_seq = ctx.draw_seq;
        dump_sink_.OnDecode(rec);
    }
    if (sub) ApplyFmvEdgeClamp(ir, rgn, dw, dh);
    render::SurfaceDesc d;
    d.width = static_cast<uint32_t>(dw);
    d.height = static_cast<uint32_t>(dh);
    d.format = render::SurfaceFormat::Rgba8;
    uint64_t t_up0 = now_ns();
    const render::SurfaceId tex = be_.CreateSurface(d);
    render::Rect r;
    r.w = dw;
    r.h = dh;
    be_.Upload(tex, r, texels_.data(), static_cast<size_t>(dw) * 4);
    uint64_t upload_ns = now_ns() - t_up0;
    // Authored mip levels: each decoded from its own (TBP, TBW) window at the
    // halved size; the backend synthesizes the tail past the chain.
    *levels = 0;
    if (mips.levels > 0 && !sub) {
        std::vector<uint32_t> lv;
        for (uint8_t i = 0; i < mips.levels; ++i) {
            const MipSampler::Level& L = mips.level[i];
            const uint64_t t0 = now_ns();
            TextureDecoder::DecodeLevel(view_.data(), v, L.tbp_bytes, L.tbw_px, L.w, L.h, &heal_, &layout_, &lv);
            const uint64_t t1 = now_ns();
            decode_ns += t1 - t0;
            if (lv.size() < size_t(L.w) * L.h) break;
            be_.UploadLevel(tex, i + 1, int(L.w), int(L.h), lv.data());
            upload_ns += now_ns() - t1;
            ++*levels;
        }
        if (*levels > 0) {
            t_up0 = now_ns();
            be_.FinishMipChain(tex, *levels, true);
            upload_ns += now_ns() - t_up0;
        }
    }
    census_.decode_ns += decode_ns;
    census_.upload_ns += upload_ns;
    warm_cur_.decode_us += static_cast<uint32_t>(decode_ns / 1000u);
    warm_cur_.upload_us += static_cast<uint32_t>(upload_ns / 1000u);
    return tex;
}

void TextureResolver::RollWarmWindow(uint64_t now_ns) {
    if (warm_window_start_ns_ == 0) {
        warm_window_start_ns_ = now_ns;
        warm_cur_.frames++;
        return;
    }
    constexpr uint64_t kSecond = 1000000000ull;
    while (now_ns - warm_window_start_ns_ >= kSecond && warm_done_.size() < kWarmWindowsMax) {
        warm_done_.push_back(warm_cur_);
        warm_cur_ = WarmWindow{};
        warm_window_start_ns_ += kSecond;
    }
    warm_cur_.frames++;
}

std::string TextureResolver::WarmSeries() const {
    std::string s;
    for (const WarmWindow& w : warm_done_) {
        if (!s.empty()) s += ',';
        s += std::to_string(w.frames) + '/' + std::to_string(w.lookups) + '/' + std::to_string(w.decodes) + '/' + std::to_string(w.decode_us) + '/' +
             std::to_string(w.upload_us);
    }
    return s;
}

// The trace's first half: the draw identity and the deferred entries the flush
// under the footprint (and the CLUT page) is about to take.
void TextureResolver::TraceBegin(const DrawIr& ir, const Context& ctx, TexresTrace::Line* line) const {
    const TexReadView& v = ir.tex;
    line->draw_seq = ctx.draw_seq;
    line->frame_count = ctx.frame_count;
    line->draw_epoch = ir.vram_epoch;
    line->tex = v;
    uint32_t fp_start = 0, fp_size = 0;
    ContentKey::Footprint(v, &fp_start, &fp_size);
    TexresTrace::NoteOverlapping(targets_.Ledger(), fp_start, fp_size, &line->flushed);
    if (IsIndexedPsm(v.psm))
        TexresTrace::NoteOverlapping(targets_.Ledger(), v.cbp % VramImage::kBytes, VramImage::kPageBytes,
                                     &line->flushed);
}

// The second half, after the key's windows were read: the page composition of the
// level-0 window and the CLUT page, their byte hashes, and the bytes when kept.
void TextureResolver::TraceCompose(const TexReadView& v, uint32_t fp_start, uint32_t fp_size,
                                   uint32_t win_size, TexresTrace::Line* line) const {
    line->fp_start = fp_start;
    line->fp_size = fp_size;
    TexresTrace::FillPages(vram_, fp_start, fp_size, &line->pages);
    line->window_hash = TexresTrace::Hash(window_.data(), win_size);
    if (IsIndexedPsm(v.psm)) {
        TexresTrace::FillPages(vram_, v.cbp % VramImage::kBytes, VramImage::kPageBytes, &line->pages);
        line->clut_hash = TexresTrace::Hash(clut_page_.data(), VramImage::kPageBytes);
    }
    if (trace_.GetFilter().keep_bytes) {
        line->window.assign(window_.data(), window_.data() + win_size);
        if (IsIndexedPsm(v.psm)) line->clut.assign(clut_page_.data(), clut_page_.data() + VramImage::kPageBytes);
    }
}

void TextureResolver::DecodeFallback(const DrawIr& ir, const Context& ctx, TextureSource* out) {
    const TexReadView& v = ir.tex;
    // An owner's slot bind at this read: its converted image IS the texture the
    // transfer would have placed here; nothing is read from VRAM.
    if (owned_.BoundCount()) {
        if (owned_.Serve(v, out)) {
            census_.owned_serves++;
            TraceServe(trace_, dump_sink_, ir, ctx, TexresTrace::Outcome::OwnedServe);
            return;
        }
        if (owned_.LastMiss() != OwnedTextureSlots::Miss::NoBind) census_.owned_misses++;
    }
    if (!ctx.has_snapshot) {
        // No texel source at all: the draw keeps its textured UV conversion over
        // the TEX0 window and samples nothing.
        TraceServe(trace_, dump_sink_, ir, ctx, TexresTrace::Outcome::NoSnapshot);
        out->declined = TextureSource::Declined::NoSnapshot;
        out->enabled = true;
        return;
    }
    uint32_t fp_start = 0, fp_size = 0;
    ContentKey::Footprint(v, &fp_start, &fp_size);
    const bool traced = trace_.Wants(v);
    TexresTrace::Line tl;
    if (traced) TraceBegin(ir, ctx, &tl);
    // Deferred 16-bit writebacks under the footprint (and the CLUT page of an
    // indexed read) land before the bytes are read.
    targets_.FlushPending(PendingRtSyncLedger::Reason::Decode, fp_start, fp_size);
    if (IsIndexedPsm(v.psm))
        targets_.FlushPending(PendingRtSyncLedger::Reason::Decode, v.cbp % VramImage::kBytes, VramImage::kPageBytes);
    vram_.MaterializePages(VramImage::Span{fp_start, fp_size}, VramImage::Reason::Decode);
    if (IsIndexedPsm(v.psm))
        vram_.MaterializePages(VramImage::Span{v.cbp, VramImage::kPageBytes}, VramImage::Reason::Decode);
    const MipSampler::Chain mips = MipSampler::Capture(v);
    const int tex_w = 1 << v.tw, tex_h = 1 << v.th;
    ContentKey::Rect rgn;
    (void)DeriveRegion(ir, tex_w, tex_h, &rgn);
    const bool sub = ContentKey::IsSubRect(v, rgn);
    const bool indexed = IsIndexedPsm(v.psm);
    const size_t nlv = mips.levels;
    uint32_t win_rel = 0, win_size = fp_size;
    if (sub) ContentKey::RowBand(v, rgn.y0, rgn.y0 + rgn.h, &win_rel, &win_size);
    // The register tuple this read was issued under (every TEX0 / TEXA field the
    // key folds in, plus the sub-rect): its last content key tells a miss whether
    // the tuple's bytes MOVED ON while the previous generation is still recorded
    // in L2 -- then both generations are single-use suspects (the old one is
    // demoted, the new one enters L2 in probation) -- and, with the VRAM view
    // signature of the pages the key hashes, whether the key can be REUSED
    // without reading them: the pages' view generations unchanged since the key
    // was computed means ReadView returns the same bytes, hence the same key.
    const uint32_t tup[12] = {v.tbp, v.tbw, v.psm, v.tw, v.th, v.cbp, v.cpsm, v.csm, v.csa,
                              v.texa_ta0, v.texa_ta1, v.texa_aem ? 1u : 0u};
    uint64_t tk = ContentKey::Fnv1aStep(1469598103934665603ull, tup, sizeof(tup));
    if (sub) tk = ContentKey::Fnv1aStep(tk, &rgn, sizeof(rgn));
    uint64_t sig = vram_.ViewSignature(VramImage::Span{fp_start + win_rel, win_size});
    if (indexed) {
        const uint64_t cs = vram_.ViewSignature(VramImage::Span{v.cbp, VramImage::kPageBytes});
        sig = ContentKey::Fnv1aStep(sig, &cs, sizeof(cs));
    }
    for (uint8_t i = 0; i < mips.levels; ++i) {
        const MipSampler::Level& L = mips.level[i];
        const uint64_t ls = vram_.ViewSignature(VramImage::Span{L.fp_start, L.fp_size});
        sig = ContentKey::Fnv1aStep(sig, &ls, sizeof(ls));
    }
    uint64_t key = 0;
    bool key_reused = false;
    auto prev = tuple_last_key_.find(tk);
    if (!traced && prev != tuple_last_key_.end() && prev->second.view_sig == sig) {
        key = prev->second.key;
        key_reused = true;
        census_.sig_hits++;
    } else {
        // Before the lookup only the key's inputs are read -- the hashed window (its
        // row band for a sub-rect), the CLUT page of an indexed read and the authored
        // mip windows -- into scratch that is reused across calls; the composed 4 MiB
        // view the decoder addresses is read by the miss path alone (DecodeAndUpload).
        vram_.ReadView(fp_start + win_rel, win_size, Scratch(window_, win_size));
        census_.prep_bytes += win_size;
        if (indexed) {
            vram_.ReadView(v.cbp, VramImage::kPageBytes, Scratch(clut_page_, VramImage::kPageBytes));
            census_.prep_bytes += VramImage::kPageBytes;
        }
        // Every authored mip level's (TBW, window bytes) is part of the identity.
        if (mip_win_.size() < nlv) mip_win_.resize(nlv);
        if (mip_in_.size() < nlv) mip_in_.resize(nlv);
        for (uint8_t i = 0; i < mips.levels; ++i) {
            const MipSampler::Level& L = mips.level[i];
            vram_.ReadView(L.fp_start, L.fp_size, Scratch(mip_win_[i], L.fp_size));
            census_.prep_bytes += L.fp_size;
            mip_in_[i] = ContentKey::MipInput{L.tbw_px, mip_win_[i].data(), L.fp_size};
        }
        key = ContentKey::Compute(v, rgn, window_.data(), win_size,
                                  indexed ? clut_page_.data() : nullptr,
                                  indexed ? size_t(VramImage::kPageBytes) : size_t(0),
                                  nlv ? mip_in_.data() : nullptr, nlv, ctx.fast_hash);
    }
    census_.lookups++;
    warm_cur_.lookups++;
    if (traced) {
        tl.key = key;
        tl.sub = sub;
        tl.rgn_x0 = rgn.x0; tl.rgn_y0 = rgn.y0; tl.rgn_w = rgn.w; tl.rgn_h = rgn.h;
        TraceCompose(v, fp_start, fp_size, win_size, &tl);
    }
    const int dw = sub ? rgn.w : tex_w, dh = sub ? rgn.h : tex_h;
    render::SurfaceId tex;
    if (const DecodeCache::L1Entry* e = cache_.L1Find(key, dw, dh)) {
        tex = e->tex;
        census_.l1_hits++;
        tl.outcome = TexresTrace::Outcome::L1Hit;
    }
    if (!tex.id) {
        tex = cache_.L2Lookup(key, dw, dh);
        if (tex.id) {
            cache_.L1Insert(key, tex, dw, dh, fp_start, fp_size);   // rebind, no decode
            census_.l2_hits++;
            tl.outcome = TexresTrace::Outcome::L2Hit;
        }
    }
    if (!tex.id) {
        bool suspect = false;
        if (!key_reused && prev != tuple_last_key_.end() && prev->second.key != key &&
            cache_.L2Contains(prev->second.key)) {
            suspect = true;
            cache_.L2MarkProbation(prev->second.key);
        }
        uint8_t levels = 0;
        tex = DecodeAndUpload(ir, ctx, rgn, sub, dw, dh, mips, &levels);
        cache_.L1Insert(key, tex, dw, dh, fp_start, fp_size);
        cache_.L2Insert(key, tex, dw, dh, suspect);
        if (tex_levels_.size() >= 65536) tex_levels_.clear();
        tex_levels_[tex.id] = levels;
        tl.outcome = TexresTrace::Outcome::Decoded;
    }
    if (tuple_last_key_.size() >= 65536) tuple_last_key_.clear();
    tuple_last_key_[tk] = LastKey{key, sig};
    out->owner.kind = TextureSource::OwnerKind::Decoded;
    out->owner.surface = tex;
    auto lv = tex_levels_.find(tex.id);
    out->owner.levels = lv == tex_levels_.end() ? uint8_t(0) : lv->second;
    if (traced) {
        tl.levels = out->owner.levels;
        if (trace_.GetFilter().keep_bytes && tex.id) {
            // The texels the draw will sample: the bound surface's level 0.
            tl.tex_w = dw;
            tl.tex_h = dh;
            tl.texels.assign(static_cast<size_t>(dw) * dh * 4, 0);
            render::Rect rr;
            rr.w = dw;
            rr.h = dh;
            be_.Readback(tex, rr, tl.texels.data(), static_cast<size_t>(dw) * 4);
        }
        trace_.Note(std::move(tl));
    }
    out->transform.mip_levels = out->owner.levels;
    if (out->owner.levels) {
        out->transform.mip_lcm = mips.lcm;
        out->transform.mip_k = mips.k;
        out->transform.mip_l = mips.l;
    }
    if (sub) {
        out->transform.region.applied = true;
        out->transform.region.u0 = rgn.x0;
        out->transform.region.v0 = rgn.y0;
        out->transform.region.w = dw;
        out->transform.region.h = dh;
    }
    out->enabled = true;
}

}  // namespace gs
}  // namespace ps2r
