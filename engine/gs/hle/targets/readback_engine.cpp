// gs/hle/targets/readback_engine.cpp -- colour / Z writeback encode into the
// VramImage shadow.
#include "engine/gs/hle/targets/readback_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"

namespace ps2r {
namespace gs {

namespace {

constexpr uint32_t kVramBytes = VramImage::kBytes;
constexpr uint32_t kPageBytes = VramImage::kPageBytes;

bool PsmOf(uint32_t field, Psm* out) { return PsmLayout::FromField(field, out); }

// Full page rows [y0, y1) of a buffer (fbp bytes, fbw px, psm): the byte range
// a row band can touch (GS User's Manual 8.2: FBW/64 pages per page row).
void PageRowRange(uint32_t fbp, uint32_t fbw, Psm psm, int y0, int y1, uint32_t* lo, uint32_t* hi) {
    const uint32_t ph = PsmLayout::Describe(psm).page_h;
    const uint32_t ppr = std::max<uint32_t>(1, fbw / 64);
    const uint32_t r0 = static_cast<uint32_t>(y0) / ph;
    const uint32_t r1 = static_cast<uint32_t>(y1 - 1) / ph;
    *lo = fbp + r0 * ppr * PsmLayout::kPageBytes;
    *hi = fbp + (r1 + 1) * ppr * PsmLayout::kPageBytes;
    if (*lo > kVramBytes) *lo = kVramBytes;
    if (*hi > kVramBytes) *hi = kVramBytes;
}

// The linear byte span rows [y0, y1) of a (fbp, fbw px, bytes per pixel) buffer
// occupy -- the deposit's touched-page rule (clamped to the 4 MiB).
void LinearSpan(uint32_t fbp, uint32_t fbw, uint32_t bpp, int y0, int y1, uint32_t* lo, uint32_t* hi) {
    const uint64_t b0 = uint64_t(fbp) + uint64_t(std::max(0, y0)) * fbw * bpp;
    const uint64_t b1 = uint64_t(fbp) + uint64_t(std::max(0, y1)) * fbw * bpp;
    *lo = static_cast<uint32_t>(std::min<uint64_t>(b0, kVramBytes));
    *hi = static_cast<uint32_t>(std::min<uint64_t>(b1, kVramBytes));
}

// Mark every page tile the rect (x0,y0)-(x1,y1) of the buffer covers as authored
// by the GPU target `fbp` (one page = one page_w x page_h tile; fbw/page_w pages
// per row).
void MarkAuthorTiles(VramImage& vram, uint32_t base, uint32_t fbp, uint32_t fbw, Psm psm,
                     const LogicalRect& r) {
    const PsmDescriptor& d = PsmLayout::Describe(psm);
    const uint32_t pw = d.page_w, ph = d.page_h;
    const uint32_t ppr = std::max<uint32_t>(1, fbw / pw);
    const uint32_t base_page = (base % kVramBytes) / kPageBytes;
    const uint32_t py0 = uint32_t(r.y0) / ph, py1 = uint32_t(r.y1 - 1) / ph;
    const uint32_t px0 = uint32_t(r.x0) / pw, px1 = uint32_t(r.x1 - 1) / pw;
    for (uint32_t py = py0; py <= py1; ++py)
        for (uint32_t px = px0; px <= px1; ++px)
            vram.NoteGpuAuthor(VramImage::Span{(base_page + py * ppr + px) * kPageBytes, kPageBytes}, fbp);
}

}  // namespace

ReadbackEngine::ReadbackEngine() : scratch_(kVramBytes, 0) {}

bool ReadbackEngine::EncodesColorPsm(uint32_t psm) {
    // PSMCT32, PSMCT24, PSMCT16. A PSMCT16S render target stages nothing (its
    // bytes reach VRAM through the alt16 lane and the stencil materialise path).
    return psm == 0x00 || psm == 0x01 || psm == 0x02;
}

void ReadbackEngine::StageTouched(VramImage& vram, uint32_t lo, uint32_t hi, uint32_t span_lo,
                                  uint32_t span_hi, uint64_t batch_epoch) {
    if (hi <= lo && span_hi <= span_lo) return;
    const uint32_t first = std::min(lo, span_lo) / kPageBytes;
    const uint32_t last = (std::max(hi, span_hi) + kPageBytes - 1) / kPageBytes;   // exclusive
    for (uint32_t pg = first; pg < last && pg < VramImage::kPageCount; ++pg) {
        const uint32_t ps = pg * kPageBytes, pe = ps + kPageBytes;
        const bool in_span = ps < span_hi && span_lo < pe;
        const bool in_rows = ps < hi && lo < pe;
        if (in_span) {
            // A span page outside the encoded rows is bracketed with its current
            // content (seeded and stamped, nothing rewritten).
            if (!in_rows) vram.ReadView(ps, kPageBytes, scratch_.data() + ps);
            vram.StageDeposit(ps, scratch_.data() + ps, kPageBytes, batch_epoch);
        } else if (in_rows && vram.Page(pg).state == PageRecord::State::Pending) {
            // Stray swizzled pixels on an already-pending page land in its shadow
            // without re-stamping it.
            vram.StageDeposit(ps, scratch_.data() + ps, kPageBytes, 0);
        }
    }
}

std::vector<std::pair<uint32_t, uint32_t>> ReadbackEngine::ProtectedRanges(
    const VramImage& vram, uint32_t fbo_start, uint32_t fbo_end, uint64_t batch_epoch) const {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (const UploadedSpan& u : uploaded_) {
        const uint32_t tex_start = u.start, tex_end = u.start + u.size;
        if (!(fbo_start < tex_end && tex_start < fbo_end) || u.size == 0) continue;
        // The texture's pages' newest upload epoch: the GPU is the last author
        // (no protection) when it is older than the batch or never stamped.
        uint64_t up_epoch = 0;
        const uint32_t p0 = (tex_start / kPageBytes) % VramImage::kPageCount;
        const uint32_t p1 = ((tex_end - 1) / kPageBytes) % VramImage::kPageCount;
        for (uint32_t p = p0;; p = (p + 1) % VramImage::kPageCount) {
            up_epoch = std::max(up_epoch, vram.Page(p).upload_epoch);
            if (p == p1) break;
        }
        if (up_epoch < batch_epoch || up_epoch == 0) continue;
        out.emplace_back(std::max(fbo_start, tex_start), std::min(fbo_end, tex_end));
    }
    std::sort(out.begin(), out.end());
    return out;
}

uint32_t ReadbackEngine::SyncRtToVram(render::Backend& be, RenderTarget& t, VramImage& vram,
                                      uint64_t batch_epoch, const LogicalRect* rescue,
                                      uint32_t rescue_fbw, uint32_t enc_psm, uint32_t dest_fbp) {
    CoherenceState& c = t.coherence;
    if (!c.HasValidContent()) return 0;
    if (!c.HasDrawnSinceSync() && rescue == nullptr) return 0;
    if (enc_psm == kNoPsmOverride) enc_psm = t.identity.render_psm;
    const int w = t.identity.width, h = t.identity.height;
    LogicalRect rows;
    if (rescue) {
        rows.x0 = std::max({0, rescue->x0, c.valid_rect.x0});
        rows.y0 = std::max({0, rescue->y0, c.valid_rect.y0});
        rows.x1 = std::min({w, rescue->x1, c.valid_rect.x1});
        rows.y1 = std::min({h, rescue->y1, c.valid_rect.y1});
        if (rescue_fbw != 0 && rows.x1 > static_cast<int>(rescue_fbw))
            rows.x1 = static_cast<int>(rescue_fbw);
    } else {
        rows.x0 = std::max(0, c.drawn_since_sync.x0);
        rows.y0 = std::max(0, c.drawn_since_sync.y0);
        rows.x1 = std::min(w, c.drawn_since_sync.x1);
        rows.y1 = std::min(h, c.drawn_since_sync.y1);
    }
    uint32_t staged = 0;
    if (!rows.Empty() && EncodesColorPsm(enc_psm)) {
        const int s = t.identity.scale;
        render::Rect pr{rows.x0 * s, rows.y0 * s, (rows.x1 - rows.x0) * s, (rows.y1 - rows.y0) * s};
        ReadColorRows(be, t.views.color, pr);
        // The encode stride is the target width; only a rescue may narrow it.
        const uint32_t fbw = (rescue && rescue_fbw) ? rescue_fbw : uint32_t(w);
        staged = EncodeColorRows(t, vram, batch_epoch, rows, fbw, rescue != nullptr, enc_psm,
                                 dest_fbp == kSameFbp ? t.identity.fbp : dest_fbp);
    }
    if (rescue == nullptr) c.ClearDrawnSinceSync();
    return staged;
}

void ReadbackEngine::OfferPixels(render::SurfaceId color, int pw, int ph, const std::vector<uint8_t>* rgba) {
    offer_ = Offer{};
    if (!offer_enabled_ || !rgba || !color.id || pw <= 0 || ph <= 0 || rgba->size() < static_cast<size_t>(pw) * ph * 4) return;
    offer_.color = color;
    offer_.pw = pw;
    offer_.ph = ph;
    offer_.rgba = rgba;
}

void ReadbackEngine::ReadColorRows(render::Backend& be, render::SurfaceId color, const render::Rect& pr) {
    const size_t stride = static_cast<size_t>(pr.w) * 4;
    pixels_.assign(stride * pr.h, 0);
    if (offer_.rgba && offer_.color.id == color.id && pr.x >= 0 && pr.y >= 0 && pr.x + pr.w <= offer_.pw &&
        pr.y + pr.h <= offer_.ph) {
        // The same attachment, unchanged since the grab: its rows (bottom-up, offer stride).
        const size_t src_stride = static_cast<size_t>(offer_.pw) * 4;
        for (int r = 0; r < pr.h; ++r)
            std::memcpy(pixels_.data() + stride * r,
                        offer_.rgba->data() + src_stride * static_cast<size_t>(pr.y + r) + static_cast<size_t>(pr.x) * 4, stride);
        offered_reads_++;
        return;
    }
    be.Readback(color, pr, pixels_.data(), stride);
}

uint32_t ReadbackEngine::EncodeColorRows(RenderTarget& t, VramImage& vram, uint64_t batch_epoch,
                                         const LogicalRect& rows, uint32_t fbw, bool rescue,
                                         uint32_t enc_psm, uint32_t dest_fbp) {
    Psm psm;
    if (!PsmOf(enc_psm, &psm)) return 0;
    const uint32_t fbp = dest_fbp;   // the deposit base (the target's own unless overridden)
    const int s = t.identity.scale;
    const int rw = rows.x1 - rows.x0, rh = rows.y1 - rows.y0;
    const int pw = rw * s;
    const uint32_t bpp = Swizzle::LinearBitsPerPixel(psm) / 8;
    linear_.assign(static_cast<size_t>(rw) * rh * bpp, 0);
    const uint16_t keep16 = (psm == Psm::CT16) ? t.coherence.sync_keep_mask16 : uint16_t(0);
    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            // Nearest physical texel of the logical pixel (identity at scale 1).
            const uint8_t* p = pixels_.data() + (static_cast<size_t>(y) * s * pw + size_t(x) * s) * 4;
            const uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
            uint8_t* o = linear_.data() + (static_cast<size_t>(y) * rw + x) * bpp;
            if (bpp == 4) {
                o[0] = r; o[1] = g; o[2] = b;
                o[3] = a;   // the stored GS alpha byte (the stored-alpha convention)
            } else {
                const uint16_t px = static_cast<uint16_t>(((r >> 3) & 0x1F) | (((g >> 3) & 0x1F) << 5) |
                                                          (((b >> 3) & 0x1F) << 10) | ((a > 127 ? 1 : 0) << 15));
                o[0] = uint8_t(px); o[1] = uint8_t(px >> 8);
            }
        }
    }
    uint32_t lo, hi;
    PageRowRange(fbp, fbw, psm, rows.y0, rows.y1, &lo, &hi);
    if (hi <= lo) return 0;
    vram.ReadView(lo, hi - lo, scratch_.data() + lo);
    before_.assign(scratch_.begin() + lo, scratch_.begin() + hi);
    // Per-page veto: an EE upload newer than the batch owns the page; a rescue
    // also keeps every page this target is not the recorded GPU author of.
    const uint32_t pg_lo = lo / kPageBytes, pg_hi = (hi + kPageBytes - 1) / kPageBytes;
    std::vector<bool> page_newer(pg_hi - pg_lo, false);
    bool any_newer = false;
    for (uint32_t pg = pg_lo; pg < pg_hi; ++pg) {
        const PageRecord& rec = vram.Page(pg);
        bool newer = rec.upload_epoch > batch_epoch;
        if (rescue && !newer)
            newer = !(rec.author == PageRecord::Author::GpuTarget && rec.author_fbp == fbp);
        page_newer[pg - pg_lo] = newer;
        any_newer |= newer;
    }
    // Uploaded textures overlapping the footprint protect their byte ranges
    // (CT16: only when the draw wrote every bit); the footprint is sized by the
    // ENCODE format (a 16-bit encode of a 512x448 buffer is half the bytes).
    std::vector<std::pair<uint32_t, uint32_t>> protected_ranges;
    if (!(psm == Psm::CT16 && keep16 != 0)) {
        uint32_t fbo_bpp = RenderTarget::FrameBitsPerPixel(enc_psm);
        if (fbo_bpp == 0) fbo_bpp = 32;
        const uint32_t fbo_size = uint32_t(t.identity.width) * uint32_t(t.identity.height) * (fbo_bpp / 8);
        protected_ranges = ProtectedRanges(vram, fbp, fbp + fbo_size, batch_epoch);
    }
    // The main body: the linear rows swizzled onto the page rows.
    const uint32_t bw = std::max<uint32_t>(1, fbw / 64);
    const Swizzle::Buffer buf{psm, fbp / PsmLayout::kBlockBytes, bw};
    const Swizzle::Rect rect{uint32_t(rows.x0), uint32_t(rows.y0), uint32_t(rw), uint32_t(rh)};
    const uint32_t mask = Swizzle::ElementWriteMask(psm) & ~uint32_t(keep16);
    Swizzle::LinearToVram(scratch_.data(), buf, rect, linear_.data(), size_t(rw) * bpp, mask);
    if (any_newer) {
        for (uint32_t pg = pg_lo; pg < pg_hi; ++pg) {
            if (!page_newer[pg - pg_lo]) continue;
            const uint32_t ps = std::max(lo, pg * kPageBytes);
            const uint32_t pe = std::min(hi, (pg + 1) * kPageBytes);
            std::memcpy(scratch_.data() + ps, before_.data() + (ps - lo), pe - ps);
        }
    }
    if (!protected_ranges.empty()) {
        // A pixel any byte of which lies in a protected range keeps its bytes.
        for (int y = rows.y0; y < rows.y1; ++y) {
            for (int x = rows.x0; x < rows.x1; ++x) {
                const uint32_t addr = PsmLayout::PixelByteAddress(psm, uint32_t(x), uint32_t(y),
                                                                  fbp / PsmLayout::kBlockBytes, bw);
                if (addr < lo || addr + bpp > hi) continue;
                bool hit = false;
                for (const auto& r : protected_ranges) {
                    if (r.first >= addr + bpp) break;
                    if (addr < r.second) { hit = true; break; }
                }
                if (hit) std::memcpy(scratch_.data() + addr, before_.data() + (addr - lo), bpp);
            }
        }
    }
    // Authorship is recorded over the deposit rect's page tiles before staging.
    MarkAuthorTiles(vram, fbp, fbp, fbw, psm, rows);
    uint32_t span_lo, span_hi;
    LinearSpan(fbp, fbw, bpp, rows.y0, rows.y1, &span_lo, &span_hi);
    StageTouched(vram, lo, hi, span_lo, span_hi, batch_epoch);
    // The encoded rows now differ from a 16-bit view of the attachment and any
    // later frame over these pages must import them: CPU-dirty + aliased.
    if (alias_ && span_hi > span_lo) {
        alias_->MarkCpuDirty(span_lo, span_hi - span_lo);
        alias_->MarkAliased(span_lo, span_hi - span_lo);
    }
    // The target's own bytes reached the mirror: the main image is no newer than
    // local memory until it draws again.
    if (fbp == t.identity.fbp) t.coherence.main_drawn_since_deposit.Reset();
    return static_cast<uint32_t>(rw) * rh;
}

uint32_t ReadbackEngine::DepositAlt16Lane(render::Backend& be, RenderTarget& t, VramImage& vram,
                                          uint16_t keep16, const LogicalRect& rect, uint32_t draw_fbw,
                                          uint64_t batch_epoch) {
    if (!t.views.alt16.id || keep16 == 0) return 0;
    const int w = t.identity.width, h = t.identity.height;
    const int x0 = std::max(0, rect.x0), y0 = std::max(0, rect.y0);
    const int x1 = std::min(w, rect.x1), y1 = std::min(h, rect.y1);
    if (x1 <= x0 || y1 <= y0) return 0;
    const int s = t.identity.scale;
    const int rw = x1 - x0, rh = y1 - y0;
    render::Rect pr{x0 * s, y0 * s, rw * s, rh * s};
    pixels_.assign(static_cast<size_t>(pr.w) * pr.h * 4, 0);
    be.Readback(t.views.alt16, pr, pixels_.data(), static_cast<size_t>(pr.w) * 4);
    const uint32_t fbw = (draw_fbw > 0 && draw_fbw <= 1024)
                             ? draw_fbw
                             : (t.identity.render_fbw ? t.identity.render_fbw : uint32_t(w));
    const uint32_t fbp = t.identity.fbp;
    uint32_t lo, hi;
    PageRowRange(fbp, fbw, Psm::CT16, y0, y1, &lo, &hi);
    if (hi <= lo) return 0;
    vram.ReadView(lo, hi - lo, scratch_.data() + lo);
    const uint32_t bw = std::max<uint32_t>(1, fbw / 64);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const uint8_t* p = pixels_.data() + (static_cast<size_t>(y - y0) * s * (rw * s) + size_t(x - x0) * s) * 4;
            const uint16_t px = static_cast<uint16_t>(((p[0] >> 3) & 0x1F) | (((p[1] >> 3) & 0x1F) << 5) |
                                                      (((p[2] >> 3) & 0x1F) << 10) | ((p[3] > 127 ? 1 : 0) << 15));
            const uint32_t addr = PsmLayout::PixelByteAddress(Psm::CT16, uint32_t(x), uint32_t(y),
                                                              fbp / PsmLayout::kBlockBytes, bw);
            if (addr < lo || addr + 2 > hi) continue;
            uint8_t* o = scratch_.data() + addr;
            if (keep16 == RenderTarget::kIndexLaneKeep16) {
                o[1] = uint8_t(px >> 8);   // the index lane: high byte only
            } else {
                const uint16_t existing = uint16_t(o[0] | (o[1] << 8));
                const uint16_t merged = uint16_t((px & uint16_t(~keep16)) | (existing & keep16));
                o[0] = uint8_t(merged);
                o[1] = uint8_t(merged >> 8);
            }
        }
    }
    uint32_t span_lo, span_hi;
    LinearSpan(fbp, fbw, 2, y0, y1, &span_lo, &span_hi);
    StageTouched(vram, lo, hi, span_lo, span_hi, batch_epoch);
    if (alias_ && span_hi > span_lo) {
        alias_->MarkCpuDirty(span_lo, span_hi - span_lo);
        alias_->MarkAliased(span_lo, span_hi - span_lo);
    }
    // The companion's lane bytes are now the newest in local memory.
    t.coherence.main_drawn_since_deposit.Reset();
    return static_cast<uint32_t>(rw) * rh;
}

uint32_t ReadbackEngine::SyncZToVram(render::Backend& be, RenderTarget& t, VramImage& vram,
                                     uint64_t batch_epoch) {
    CoherenceState& c = t.coherence;
    if (!c.HasValidContent() || !c.HasZDrawnSinceSync() || !t.views.depth.id) return 0;
    if (c.last_zbp == 0xFFFFFFFFu) return 0;
    const int w = t.identity.width, h = t.identity.height;
    LogicalRect rows;
    rows.x0 = std::max(0, c.z_drawn_since_sync.x0);
    rows.y0 = std::max(0, c.z_drawn_since_sync.y0);
    rows.x1 = std::min(w, c.z_drawn_since_sync.x1);
    rows.y1 = std::min(h, c.z_drawn_since_sync.y1);
    uint32_t staged = 0;
    if (!rows.Empty()) {
        const int s = t.identity.scale;
        render::Rect pr{rows.x0 * s, rows.y0 * s, (rows.x1 - rows.x0) * s, (rows.y1 - rows.y0) * s};
        depth_.assign(static_cast<size_t>(pr.w) * pr.h, 0.0f);
        be.Readback(t.views.depth, pr, depth_.data(), static_cast<size_t>(pr.w) * sizeof(float));
        // The whole depth buffer syncs in its NATIVE format at its native address.
        staged = EncodeZRows(t, vram, batch_epoch, rows, 0x30u | (c.last_zpsm & 0xFu));
    }
    c.ClearZDrawnSinceSync();
    return staged;
}

uint32_t ReadbackEngine::EncodeZRows(RenderTarget& t, VramImage& vram, uint64_t batch_epoch,
                                     const LogicalRect& rows, uint32_t zpsm_field) {
    Psm zpsm;
    if (!PsmOf(zpsm_field, &zpsm) || !PixelConvert::IsZ(zpsm)) return 0;
    const uint32_t zbp = t.coherence.last_zbp;
    const int s = t.identity.scale;
    const int rw = rows.x1 - rows.x0, rh = rows.y1 - rows.y0;
    const int pw = rw * s;
    const uint32_t bpp = Swizzle::LinearBitsPerPixel(zpsm) / 8;
    const bool z16 = (zpsm == Psm::Z16 || zpsm == Psm::Z16S);
    linear_.assign(static_cast<size_t>(rw) * rh * bpp, 0);
    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            // Window depth d = z / 2^32 (the vertex shader mapping), inverted exactly;
            // the narrower Z formats SATURATE (a 32-bit z beyond their range clamps).
            const double d = depth_[static_cast<size_t>(y) * s * pw + size_t(x) * s];
            double zf = d * 4294967296.0;
            if (zf < 0.0) zf = 0.0;
            if (zf > 4294967295.0) zf = 4294967295.0;
            uint32_t z = static_cast<uint32_t>(zf);
            if (z16) z = std::min<uint32_t>(z, 0xFFFFu);
            else if (zpsm == Psm::Z24) z = std::min<uint32_t>(z, 0xFFFFFFu);
            const uint32_t el = PixelConvert::PackZ(zpsm, PixelConvert::Depth{z});
            uint8_t* o = linear_.data() + (static_cast<size_t>(y) * rw + x) * bpp;
            for (uint32_t k = 0; k < bpp; ++k) o[k] = uint8_t(el >> (8 * k));
        }
    }
    const uint32_t fbw = t.identity.render_fbw ? t.identity.render_fbw : uint32_t(t.identity.width);
    uint32_t lo, hi;
    PageRowRange(zbp, fbw, zpsm, rows.y0, rows.y1, &lo, &hi);
    if (hi <= lo) return 0;
    vram.ReadView(lo, hi - lo, scratch_.data() + lo);
    const Swizzle::Buffer buf{zpsm, zbp / PsmLayout::kBlockBytes, std::max<uint32_t>(1, fbw / 64)};
    const Swizzle::Rect rect{uint32_t(rows.x0), uint32_t(rows.y0), uint32_t(rw), uint32_t(rh)};
    Swizzle::LinearToVram(scratch_.data(), buf, rect, linear_.data(), size_t(rw) * bpp,
                          Swizzle::ElementWriteMask(zpsm));
    MarkAuthorTiles(vram, zbp, zbp, fbw, zpsm, rows);
    uint32_t span_lo, span_hi;
    LinearSpan(zbp, fbw, bpp, rows.y0, rows.y1, &span_lo, &span_hi);
    StageTouched(vram, lo, hi, span_lo, span_hi, batch_epoch);
    return static_cast<uint32_t>(rw) * rh;
}

}  // namespace gs
}  // namespace ps2r
