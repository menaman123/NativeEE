// gs/hle/targets/target_refresher.cpp -- uploaded textures, CPU-dirty pages and
// the mirror -> attachment row-band refresh.
#include "engine/gs/hle/targets/target_refresher.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/gs/format/texa_expand.h"
#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"

namespace ps2r {
namespace gs {

TargetRefresher::TargetRefresher(render::Backend& be, VramImage& vram, RenderTargetCache& targets)
    : be_(be), vram_(vram), targets_(targets) {}

TargetRefresher::~TargetRefresher() {
    for (auto& kv : uploaded_) be_.DestroySurface(kv.second.tex);
}

void TargetRefresher::PublishSpans() {
    std::vector<ReadbackEngine::UploadedSpan> spans;
    spans.reserve(uploaded_.size());
    for (const auto& kv : uploaded_) spans.push_back({kv.second.vram_addr, kv.second.size_bytes});
    targets_.Readback().SetUploadedSpans(std::move(spans));
}

uint64_t TargetRefresher::UploadedTexelBytes() const {
    uint64_t s = 0;
    for (const auto& kv : uploaded_) s += uint64_t(kv.second.w) * uint64_t(kv.second.h) * 4u;
    return s;
}

void TargetRefresher::UploadDirect(uint32_t vram_addr, int w, int h, const uint8_t* rgba,
                                   uint32_t footprint) {
    if (w <= 0 || h <= 0 || !rgba) return;
    auto it = uploaded_.find(vram_addr);
    if (it != uploaded_.end()) {
        if (it->second.w == w && it->second.h == h) {
            // Same size: the texels are replaced in place, the record (and its
            // footprint) stands.
            be_.Upload(it->second.tex, render::Rect{0, 0, w, h}, rgba, static_cast<size_t>(w) * 4);
            it->second.last_use = ++tick_;
            return;
        }
        be_.DestroySurface(it->second.tex);
        uploaded_.erase(it);
    }
    EnforceUploadedBudget();
    render::SurfaceDesc d;
    d.width = static_cast<uint32_t>(w);
    d.height = static_cast<uint32_t>(h);
    d.format = render::SurfaceFormat::Rgba8;
    d.renderable = false;
    Uploaded u;
    u.tex = be_.CreateSurface(d);
    be_.Upload(u.tex, render::Rect{0, 0, w, h}, rgba, static_cast<size_t>(w) * 4);
    u.w = w; u.h = h;
    u.vram_addr = vram_addr;
    u.size_bytes = footprint ? footprint : static_cast<uint32_t>(w) * h * 4;
    u.last_use = ++tick_;
    uploaded_[vram_addr] = u;
    PublishSpans();
}

void TargetRefresher::EnforceUploadedBudget() {
    // Room for one more entry: the count bound and the texel-byte bound, LRU out.
    while (uploaded_.size() >= kUploadedMaxEntries ||
           (!uploaded_.empty() && UploadedTexelBytes() > kUploadedMaxBytes)) {
        auto victim = uploaded_.begin();
        for (auto it = uploaded_.begin(); it != uploaded_.end(); ++it)
            if (it->second.last_use < victim->second.last_use) victim = it;
        be_.DestroySurface(victim->second.tex);
        uploaded_.erase(victim);
    }
}

const TargetRefresher::Uploaded* TargetRefresher::FindUploaded(uint32_t tbp) {
    auto it = uploaded_.find(tbp);
    if (it != uploaded_.end()) {
        it->second.last_use = ++tick_;
        return &it->second;
    }
    for (auto& kv : uploaded_) {
        const Uploaded& u = kv.second;
        if (tbp >= u.vram_addr && tbp < u.vram_addr + u.size_bytes) {
            kv.second.last_use = ++tick_;
            return &kv.second;
        }
    }
    return nullptr;
}

void TargetRefresher::EvictUploadedInRange(uint32_t start, uint32_t size) {
    if (size == 0 || uploaded_.empty()) return;
    bool changed = false;
    for (auto it = uploaded_.begin(); it != uploaded_.end();) {
        const Uploaded& u = it->second;
        const bool overlap = u.vram_addr < start + size && start < u.vram_addr + u.size_bytes;
        if (overlap) {
            be_.DestroySurface(u.tex);
            it = uploaded_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) PublishSpans();
}

bool TargetRefresher::EvictRangeUnderDraw(const DrawIr& ir, const render::DrawResult& res,
                                          int render_height, uint32_t* start, uint32_t* size) {
    if (!res.drew || !res.bbox_valid) return false;
    const uint32_t psm = ir.raster.frame.psm;
    // A partial-bit-masked 16-bit draw perturbs at most the masked bits of each
    // texel: the uploaded asset bytes survive, so does the record.
    if (RenderTarget::Is16BitColor(psm) && res.fbmsk16 != 0) return false;
    const int y0 = std::max(0, static_cast<int>(std::floor(res.bbox_tri[1])));
    const int y1 = std::min(render_height, static_cast<int>(std::ceil(res.bbox_tri[3])));
    if (y1 <= y0 || ir.raster.frame.fbw == 0) return false;
    uint32_t bpp = RenderTarget::FrameBitsPerPixel(psm);
    if (bpp == 0) bpp = 32;
    const uint32_t row_bytes = ir.raster.frame.fbw * (bpp / 8);
    // Rounded out to page boundaries: bytes inside a page row are not row-linear
    // under the swizzle, page-row boundaries are.
    const uint32_t ev_start = (ir.raster.frame.fbp + uint32_t(y0) * row_bytes) & ~8191u;
    const uint32_t ev_end = (ir.raster.frame.fbp + uint32_t(y1) * row_bytes + 8191u) & ~8191u;
    *start = ev_start;
    *size = ev_end - ev_start;
    return true;
}

void TargetRefresher::EvictUnderDraw(const DrawIr& ir, const render::DrawResult& res,
                                     int render_height) {
    uint32_t start = 0, size = 0;
    if (EvictRangeUnderDraw(ir, res, render_height, &start, &size)) EvictUploadedInRange(start, size);
}

void TargetRefresher::MarkPagesCpuDirty(uint32_t addr, uint32_t size) {
    targets_.AliasPages().MarkCpuDirty(addr, size);
}

bool TargetRefresher::ArePagesCpuDirty(uint32_t addr, uint32_t size) const {
    return targets_.AliasPages().AnyCpuDirty(addr, size);
}

void TargetRefresher::ClearPagesCpuDirty(uint32_t addr, uint32_t size) {
    targets_.AliasPages().ClearCpuDirty(addr, size);
}

bool TargetRefresher::UploadRowBand(uint32_t addr, uint32_t size, const RenderTarget& t, int* y0,
                                    int* y1) {
    // Page-tile fold (GS User's Manual 8.2): the upload's pages -> the rows of the
    // target those page tiles cover (64-row pages for 16-bit modes).
    int x0, ry0, x1, ry1;
    const uint32_t fbw = t.identity.render_fbw ? t.identity.render_fbw : uint32_t(t.identity.width);
    if (!PageRecord::FoldByteRangeToPageRect(addr, size, t.identity.fbp, fbw, t.identity.render_psm,
                                             t.identity.width, t.identity.height, &x0, &ry0, &x1, &ry1))
        return false;
    return PageRecord::ClampToRowBand(ry0, ry1, t.identity.height, y0, y1);
}

uint32_t TargetRefresher::AliasRegionBytes(const DrawIr& ir, int render_h) {
    const uint32_t psm = ir.raster.frame.psm;
    const bool color = psm == 0x00 || psm == 0x01 || psm == 0x02 || psm == 0x0A;
    if (!color || ir.raster.frame.fbmsk == 0 || ir.raster.frame.fbw == 0 || render_h <= 0) return 0;
    const uint32_t bpp = RenderTarget::Is16BitColor(psm) ? 2u : 4u;
    return ir.raster.frame.fbw * uint32_t(render_h) * bpp;
}

bool TargetRefresher::AliasPreDraw(RenderTarget& t, const DrawIr& ir, int render_h, uint32_t scanout_fbp,
                                   uint64_t job_epoch, uint32_t hold_fbp, bool import) {
    const uint32_t region = AliasRegionBytes(ir, render_h);
    if (region == 0) return false;
    const uint32_t fbp = ir.raster.frame.fbp;
    // Deferred 16-bit writebacks under the frame land first: the import below
    // must see them in the mirror.
    targets_.FlushPending(PendingRtSyncLedger::Reason::PreDraw, fbp, region, false, hold_fbp);
    if (fbp / VramImage::kPageBytes >= VramImage::kPageCount) return false;
    const AliasPageSet& alias = targets_.AliasPages();
    bool demand = alias.AnyAliased(fbp, region);
    // A 16-bit frame also imports when the EE rewrote any page under it: the
    // attachment's 32-bit texels no longer describe the 5551 bytes it will blend with.
    if (!demand && RenderTarget::Is16BitColor(ir.raster.frame.psm) && alias.AnyCpuDirty(fbp, region))
        demand = true;
    if (!demand) return false;
    if (!import) return true;
    // The band: the draw's vertex bbox within the scissor, clamped to the target;
    // a composite into the scanout target (textured from another buffer) imports
    // the whole target, as does a draw without vertices.
    int y0 = 0, y1 = t.identity.height;
    const bool composite = fbp == scanout_fbp && ir.tex_enabled && ir.tex.tbp != fbp;
    if (!composite && !ir.verts.empty()) {
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        for (const VertexRec& v : ir.verts) {
            bx0 = std::min(bx0, v.x); by0 = std::min(by0, v.y);
            bx1 = std::max(bx1, v.x); by1 = std::max(by1, v.y);
        }
        const DrawIr::Scissor& sc = ir.raster.scissor;
        int ax0 = std::max(int(std::floor(bx0)), int(sc.x0));
        int ay0 = std::max(int(std::floor(by0)), int(sc.y0));
        int ax1 = std::min(int(std::ceil(bx1)), int(sc.x1) + 1);
        int ay1 = std::min(int(std::ceil(by1)), int(sc.y1) + 1);
        ax0 = std::max(0, std::min(ax0, t.identity.width));
        ay0 = std::max(0, std::min(ay0, t.identity.height));
        ax1 = std::max(0, std::min(ax1, t.identity.width));
        ay1 = std::max(0, std::min(ay1, t.identity.height));
        if (ax1 > ax0 && ay1 > ay0) { y0 = ay0; y1 = ay1; }
    }
    RefreshFromVram(t, y0, y1, job_epoch);
    return true;
}

void TargetRefresher::UploadRows(RenderTarget& t, int y0, int y1, const uint8_t* rgba) {
    const int w = t.identity.width, h = y1 - y0, s = t.identity.scale;
    if (s == 1) {
        be_.Upload(t.views.color, render::Rect{0, y0, w, h}, rgba, size_t(w) * 4);
        return;
    }
    // Nearest replicate onto the scaled attachment.
    scaled_.assign(static_cast<size_t>(w) * s * h * s * 4, 0);
    for (int y = 0; y < h * s; ++y)
        for (int x = 0; x < w * s; ++x)
            std::memcpy(scaled_.data() + (size_t(y) * w * s + x) * 4, rgba + (size_t(y / s) * w + x / s) * 4, 4);
    be_.Upload(t.views.color, render::Rect{0, y0 * s, w * s, h * s}, scaled_.data(), size_t(w) * s * 4);
}

uint32_t TargetRefresher::WalkBufferWidth(const RenderTarget& t, Psm mode) {
    // The stride each de-swizzle walk addresses the mirror at, in 64-pixel units:
    // RefreshAlphaMerge at the TARGET width (the PSMT8H index lane lives there),
    // RefreshCt16 at the render FBW (the 5551 image as the EE laid it out). The
    // band read (ReadMirrorBand) derives its window from the same value, so a walk
    // never touches a mirror byte this import did not refresh.
    const uint32_t w = uint32_t(t.identity.width);
    const uint32_t px = mode == Psm::CT16 ? (t.identity.render_fbw ? t.identity.render_fbw : w) : w;
    return std::max<uint32_t>(1, px / 64);
}

void TargetRefresher::ReadMirrorBand(uint32_t fbp, Psm mode, uint32_t bw, uint32_t w, int y0, int y1) {
    // The committed mirror's pages the walk of rows [y0, y1) at stride `bw`
    // addresses -- page = (y / page_h) * ppr + x / page_w for every (x, y) of the
    // band, from the band's first page row to the last page its last row reaches
    // (x / page_w runs past ppr - 1 when the width is not a multiple of the page
    // width; the union covers that) -- read into the 4 MiB scratch at their own
    // offsets, wrapped at 4 MiB. The walks touch nothing else (track 1d: the whole
    // image was copied per import).
    mirror_.resize(VramImage::kBytes);
    const PsmDescriptor& d = PsmLayout::Describe(mode);
    const uint32_t ppr = PsmLayout::PagesPerRow(mode, bw);
    const uint32_t first_page = (uint32_t(y0) / d.page_h) * ppr;
    const uint32_t last_page = (uint32_t(y1 - 1) / d.page_h) * ppr + (w - 1u) / d.page_w;
    const uint32_t lo = first_page * VramImage::kPageBytes;
    const uint32_t hi = (last_page + 1u) * VramImage::kPageBytes;
    const uint32_t a = (fbp + lo) % VramImage::kBytes;
    const uint32_t len = std::min<uint32_t>(hi - lo, VramImage::kBytes);
    const uint32_t first = std::min<uint32_t>(len, VramImage::kBytes - a);
    vram_.ReadMirror(a, first, mirror_.data() + a);
    if (first < len) vram_.ReadMirror(0, len - first, mirror_.data());
}

void TargetRefresher::RefreshAlphaMerge(RenderTarget& t, int y0, int y1, uint32_t bw) {
    // CT32 / CT24: RGB from the attachment, alpha from the committed mirror's
    // stored byte VERBATIM (the stored-alpha convention: the attachment holds the
    // GS alpha byte as local memory does, 0..0xFF; a PSMT8H read of the buffer
    // indexes with exactly that byte), addressed at the TARGET width (`bw` =
    // WalkBufferWidth(t, CT32), the stride the band was read at). Pixels the main
    // route drew since this target's bytes last reached the mirror keep the
    // attachment's alpha: local memory is older there (GS User's Manual: one
    // memory, the later write owns the byte).
    const int w = t.identity.width, h = y1 - y0, s = t.identity.scale;
    pixels_.assign(static_cast<size_t>(w) * s * h * s * 4, 0);
    be_.Readback(t.views.color, render::Rect{0, y0 * s, w * s, h * s}, pixels_.data(), size_t(w) * s * 4);
    if (s != 1) {
        // Fold the physical rows onto the logical grid (nearest texel).
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                std::memcpy(pixels_.data() + (size_t(y) * w + x) * 4,
                            pixels_.data() + (size_t(y) * s * w * s + size_t(x) * s) * 4, 4);
    }
    // The band's CT32 words out of the mirror in one swizzle walk (Swizzle::
    // VramToLinear is the per-pixel PsmLayout address, hoisted; the test pins the
    // identity), then the alpha byte of every word.
    linear_.assign(static_cast<size_t>(w) * h * 4, 0);
    Swizzle::VramToLinear(mirror_.data(), Swizzle::Buffer{Psm::CT32, t.identity.fbp / PsmLayout::kBlockBytes, bw},
                          Swizzle::Rect{0, uint32_t(y0), uint32_t(w), uint32_t(h)}, linear_.data(), size_t(w) * 4);
    const LogicalRect& newer = t.coherence.main_drawn_since_deposit;
    for (int y = 0; y < h; ++y) {
        const bool row_newer = !newer.Empty() && y0 + y >= newer.y0 && y0 + y < newer.y1;
        for (int x = 0; x < w; ++x) {
            if (row_newer && x >= newer.x0 && x < newer.x1) continue;
            const size_t i = size_t(y) * w + x;
            pixels_[i * 4 + 3] = linear_[i * 4 + 3];
        }
    }
    UploadRows(t, y0, y1, pixels_.data());
}

void TargetRefresher::RefreshCt16(RenderTarget& t, int y0, int y1, uint32_t bw, uint64_t job_epoch, bool* absorbed) {
    const int w = t.identity.width, h = y1 - y0, s = t.identity.scale;
    // `bw` = WalkBufferWidth(t, CT16): the render FBW in 64-pixel units, the stride
    // the band was read at.
    const uint32_t tbw = t.identity.render_fbw ? t.identity.render_fbw : uint32_t(w);
    pixels_.assign(static_cast<size_t>(w) * h * 4, 0);
    // Keep runs: rows whose linear page span carries an EE upload newer than the
    // job keep the attachment bytes (the EE wrote after this draw was issued).
    std::vector<bool> keep(static_cast<size_t>(h), false);
    bool any_keep = false;
    if (job_epoch != 0) {
        const uint32_t rb = tbw * 2u;
        for (int y = y0; y < y1; ++y) {
            const uint32_t a0 = t.identity.fbp + uint32_t(y) * rb;
            const uint32_t p0 = (a0 / VramImage::kPageBytes) % VramImage::kPageCount;
            const uint32_t p1 = ((a0 + rb - 1) / VramImage::kPageBytes) % VramImage::kPageCount;
            uint64_t me = 0;
            for (uint32_t p = p0;; p = (p + 1) % VramImage::kPageCount) {
                me = std::max(me, vram_.Page(p).upload_epoch);
                if (p == p1) break;
            }
            if (me != 0 && me > job_epoch) { keep[size_t(y - y0)] = true; any_keep = true; }
        }
    }
    if (any_keep) {
        std::vector<uint8_t> att(static_cast<size_t>(w) * s * h * s * 4);
        be_.Readback(t.views.color, render::Rect{0, y0 * s, w * s, h * s}, att.data(), size_t(w) * s * 4);
        for (int y = 0; y < h; ++y) {
            if (!keep[size_t(y)]) continue;
            for (int x = 0; x < w; ++x)
                std::memcpy(pixels_.data() + (size_t(y) * w + x) * 4,
                            att.data() + (size_t(y) * s * w * s + size_t(x) * s) * 4, 4);
        }
    }
    // The band's 5551 halfwords out of the mirror in one swizzle walk, then the
    // widening per pixel of every row that is not kept.
    linear_.assign(static_cast<size_t>(w) * h * 2, 0);
    Swizzle::VramToLinear(mirror_.data(), Swizzle::Buffer{Psm::CT16, t.identity.fbp / PsmLayout::kBlockBytes, bw},
                          Swizzle::Rect{0, uint32_t(y0), uint32_t(w), uint32_t(h)}, linear_.data(), size_t(w) * 2);
    for (int y = y0; y < y1; ++y) {
        if (keep[size_t(y - y0)]) continue;
        const uint8_t* row = linear_.data() + size_t(y - y0) * w * 2;
        uint8_t* out = pixels_.data() + size_t(y - y0) * w * 4;
        for (int x = 0; x < w; ++x) {
            const uint16_t px = uint16_t(row[x * 2] | (row[x * 2 + 1] << 8));
            const uint32_t rgba = TexaExpand::Expand16To32(px);
            std::memcpy(out + size_t(x) * 4, &rgba, 4);
        }
    }
    *absorbed = !any_keep;
    UploadRows(t, y0, y1, pixels_.data());
}

void TargetRefresher::RefreshFromVram(RenderTarget& t, int y0, int y1, uint64_t job_epoch) {
    const int w = t.identity.width, th = t.identity.height;
    if (w <= 0 || th <= 0) return;
    const uint32_t psm = t.identity.render_psm;
    if (psm != 0x00 && psm != 0x01 && psm != 0x02 && psm != 0x0A) return;
    // A mirror -> attachment import authors the main image.
    t.coherence.main_author_seq++;
    const bool scoped = PageRecord::ClampToRowBand(y0, y1, th, &y0, &y1) && !(y0 == 0 && y1 == th);
    if (!scoped) { y0 = 0; y1 = th; }
    const uint32_t bpp = RenderTarget::Is16BitColor(psm) ? 2u : 4u;
    const uint32_t region = uint32_t(w) * uint32_t(th) * bpp;
    // The voucher epoch: the newest EE upload under the whole footprint.
    uint64_t voucher = 0;
    {
        uint32_t first, count;
        VramImage::PagesOf(VramImage::Span{t.identity.fbp, region}, &first, &count);
        for (uint32_t i = 0; i < count; ++i) voucher = std::max(voucher, vram_.Page(first + i).upload_epoch);
    }
    targets_.FlushPending(PendingRtSyncLedger::Reason::Refresh, t.identity.fbp, region);
    bool absorbed16 = false;
    if (psm != 0x0A) {
        // The band read and its de-swizzle walk share one page-row stride
        // (WalkBufferWidth): the mirror window is exactly the pages the walk
        // addresses, whatever the render FBW / width relation.
        const Psm mode = psm == 0x02 ? Psm::CT16 : Psm::CT32;
        const uint32_t bw = WalkBufferWidth(t, mode);
        ReadMirrorBand(t.identity.fbp, mode, bw, uint32_t(w), y0, y1);
        if (psm == 0x02) RefreshCt16(t, y0, y1, bw, job_epoch, &absorbed16);
        else RefreshAlphaMerge(t, y0, y1, bw);
        vram_.MaterializePages(VramImage::Span{t.identity.fbp, region}, VramImage::Reason::Refresh);
    }
    t.coherence.has_content = true;
    t.coherence.valid_rect.Union(0, 0, w, th);
    t.coherence.cd_copy = render::CdCopyState();
    if (!scoped && psm != 0x0A) {
        if (psm == 0x00 || psm == 0x01) {
            t.coherence.vram_coherent_epoch = voucher;
            t.coherence.vram_coherent_class = 1;   // the alpha byte == the T8H index lane
        } else if (absorbed16) {
            t.coherence.vram_coherent_epoch = voucher;
            t.coherence.vram_coherent_class = 2;   // full 5551 image from the mirror
        }
    }
    // The main image was just rewritten from the mirror: a companion's
    // shared-byte view is stale over the whole image (every arm).
    if (t.views.alt16.id) t.coherence.alt16_stale.Union(0, 0, w, th);
    // Aliased pages the import fully covered are coherent again.
    AliasPageSet& alias = targets_.AliasPages();
    if (scoped) {
        const uint32_t fbw = t.identity.render_fbw ? t.identity.render_fbw : uint32_t(w);
        if (fbw != 0 && (fbw % 64) == 0 && w >= int(fbw)) {
            const int ph = RenderTarget::Is16BitColor(psm) ? 64 : 32;
            const uint32_t ppr = fbw / 64;
            const int k0 = (y0 + ph - 1) / ph, k1 = y1 / ph;
            if (k1 > k0) {
                const uint64_t base = t.identity.fbp / VramImage::kPageBytes;
                const uint64_t first = base + uint64_t(k0) * ppr, last = base + uint64_t(k1) * ppr - 1;
                for (uint64_t p = first; p <= last && p < VramImage::kPageCount; ++p) alias.ClearAliasedPage(uint32_t(p));
            }
        }
    } else {
        alias.ClearAliased(t.identity.fbp, region);
    }
}

}  // namespace gs
}  // namespace ps2r
