// gs/hle/targets/render_target.cpp -- RenderTarget surfaces, the alt16
// bifurcation, the per-frame clear decision and the post-draw coherence tail.
#include "engine/gs/hle/targets/render_target.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/gs/format/texa_expand.h"

namespace ps2r {
namespace gs {

namespace {

render::SurfaceDesc ColorDesc(const TargetIdentity& id, bool renderable) {
    render::SurfaceDesc d;
    d.width = static_cast<uint32_t>(id.PhysicalW());
    d.height = static_cast<uint32_t>(id.PhysicalH());
    d.format = render::SurfaceFormat::Rgba8;
    d.renderable = renderable;
    d.scale = id.scale;
    return d;
}

void ExtendCdDirty(render::CdCopyState& cd, int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0) return;
    if (cd.dirty_x0 >= cd.dirty_x1 || cd.dirty_y0 >= cd.dirty_y1) {
        cd.dirty_x0 = x0; cd.dirty_y0 = y0; cd.dirty_x1 = x1; cd.dirty_y1 = y1;
        return;
    }
    cd.dirty_x0 = std::min(cd.dirty_x0, x0);
    cd.dirty_y0 = std::min(cd.dirty_y0, y0);
    cd.dirty_x1 = std::max(cd.dirty_x1, x1);
    cd.dirty_y1 = std::max(cd.dirty_y1, y1);
}

}  // namespace

uint16_t RenderTarget::Fbmsk16From32(uint32_t m) {
    // Fold the 8-bit channel masks onto the 5551 fields: a channel field is
    // masked when its high bits are; alpha keeps bit 15 from bit 31.
    return static_cast<uint16_t>(((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) |
                                 ((m >> 16) & 0x8000));
}

uint32_t RenderTarget::FrameBitsPerPixel(uint32_t psm) {
    switch (psm) {
        case 0x00: case 0x01: case 0x30: case 0x31: return 32;   // CT32 CT24 Z32 Z24
        case 0x02: case 0x0A: case 0x32: case 0x3A: return 16;   // CT16 CT16S Z16 Z16S
        case 0x13: case 0x1B: return 8;                          // T8 T8H
        case 0x14: case 0x24: case 0x2C: return 4;               // T4 T4HL T4HH
        default: return 0;
    }
}

void RenderTarget::Create(render::Backend& be, const TargetIdentity& id) {
    identity = id;
    views = SurfaceViews();
    coherence = CoherenceState();
    views.color = be.CreateSurface(ColorDesc(id, true));
    render::SurfaceDesc dd = ColorDesc(id, true);
    dd.format = render::SurfaceFormat::Depth32f;
    views.depth = be.CreateSurface(dd);
    views.rt_copy = be.CreateSurface(ColorDesc(id, false));
    be.Clear(views.color, views.depth, 0u, 0.0f);
}

void RenderTarget::Destroy(render::Backend& be) {
    if (views.color.id) be.DestroySurface(views.color);
    if (views.depth.id) be.DestroySurface(views.depth);
    if (views.rt_copy.id) be.DestroySurface(views.rt_copy);
    if (views.alt16.id) be.DestroySurface(views.alt16);
    views = SurfaceViews();
}

void RenderTarget::EnsureAlt16(render::Backend& be) {
    if (views.alt16.id) return;
    views.alt16 = be.CreateSurface(ColorDesc(identity, true));
    be.Clear(views.alt16, render::SurfaceId{}, 0u, 0.0f, render::kClearColor);
    views.alt16_has_content = false;
    // A fresh companion mirrors nothing yet: the whole main attachment is stale.
    coherence.alt16_stale.Union(0, 0, identity.width, identity.height);
}

void RenderTarget::RefreshAlt16FromMain(render::Backend& be) {
    if (!views.alt16.id || coherence.alt16_stale.Empty()) return;
    // Clamp the stale rect to the backing store; consume it either way.
    LogicalRect r;
    r.x0 = std::max(0, coherence.alt16_stale.x0);
    r.y0 = std::max(0, coherence.alt16_stale.y0);
    r.x1 = std::min(identity.width, coherence.alt16_stale.x1);
    r.y1 = std::min(identity.height, coherence.alt16_stale.y1);
    coherence.alt16_stale.Reset();
    if (r.Empty()) return;
    // The main -> companion mirror authors companion bytes.
    coherence.alt_author_seq++;
    const int s = identity.scale;
    render::Rect pr{r.x0 * s, r.y0 * s, (r.x1 - r.x0) * s, (r.y1 - r.y0) * s};
    std::vector<uint8_t> px(static_cast<size_t>(pr.w) * pr.h * 4);
    const size_t stride = static_cast<size_t>(pr.w) * 4;
    be.Readback(views.color, pr, px.data(), stride);
    // 5551 pack then unpack: the companion holds what a 16-bit read of the main
    // buffer would see (5-bit channels replicated, alpha bit -> 0 / 255).
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        const uint32_t word = uint32_t(px[i]) | (uint32_t(px[i + 1]) << 8) |
                              (uint32_t(px[i + 2]) << 16) | (uint32_t(px[i + 3]) << 24);
        const uint32_t out = TexaExpand::Expand16To32(TexaExpand::Truncate32To16(word));
        px[i] = uint8_t(out);
        px[i + 1] = uint8_t(out >> 8);
        px[i + 2] = uint8_t(out >> 16);
        px[i + 3] = uint8_t(out >> 24);
    }
    // Readback and Upload both address rows from the attachment's row rect.y
    // upward (GS row y is stored at GL row y), so the rows go back unflipped.
    be.Upload(views.alt16, pr, px.data(), stride);
    views.alt16_has_content = true;
}

bool RenderTarget::WantsFrameClear(uint32_t ee_frame) const {
    return coherence.ever_composited ? coherence.needs_frame_clear
                                     : (coherence.last_clear_frame != ee_frame);
}

void RenderTarget::MarkMaterialized(int x0, int y0, int x1, int y1, bool alt16,
                                    uint32_t frame_count, uint64_t gfx_epoch) {
    CoherenceState& c = coherence;
    if (alt16) {
        c.alt16_drawn.Union(x0, y0, x1, y1);
        ExtendCdDirty(c.cd_copy, x0, y0, x1, y1);
        views.alt16_has_content = true;
    } else {
        c.valid_rect.ExpandToInclude(x0, y0, x1, y1);
        c.drawn_since_sync.Union(x0, y0, x1, y1);
        ExtendCdDirty(c.cd_copy, x0, y0, x1, y1);
        if (views.alt16.id) c.alt16_stale.Union(x0, y0, x1, y1);
        c.has_content = true;
    }
    (alt16 ? c.alt_author_seq : c.main_author_seq)++;
    c.last_draw_frame = frame_count;
    c.last_draw_gfx_epoch = gfx_epoch;
}

void RenderTarget::MarkCompositeConsumed() {
    coherence.needs_frame_clear = true;
    coherence.ever_composited = true;
}

void RenderTarget::PrepareDraw(render::Backend& be, const DrawIr& ir, uint32_t ee_frame,
                               uint32_t scanout_fbp, render::DrawParams* out) {
    out->color = views.color;
    out->depth = views.depth;
    out->rt_copy = views.rt_copy;
    out->rt_copy_available = views.rt_copy.id != 0;
    out->target_w = identity.width;
    out->target_h = identity.height;
    out->scale = identity.scale;
    out->has_32bit_content = identity.has_32bit_content;
    out->cd_copy = &coherence.cd_copy;
    // A 16-bit FRAME over a buffer that has held 32-bit content draws into the
    // companion (the two formats cannot share one RGBA8 attachment).
    out->alt16_route = false;
    if (Is16BitColor(ir.raster.frame.psm) && identity.has_32bit_content) {
        EnsureAlt16(be);
        RefreshAlt16FromMain(be);
        out->alt16 = views.alt16;
        out->alt16_route = true;
    }
    out->do_frame_clear = WantsFrameClear(ee_frame);
    out->clear_color = false;
    if (out->do_frame_clear) {
        coherence.last_clear_frame = ee_frame;
        coherence.needs_frame_clear = false;
        if (ir.raster.frame.fbp == scanout_fbp) {
            // The scanout target clears colour too; the companion then mirrors
            // nothing of the main attachment and the Cd copy is void.
            out->clear_color = true;
            if (!out->alt16_route)
                coherence.alt16_stale.Union(0, 0, identity.width, identity.height);
            coherence.cd_copy = render::CdCopyState();
        }
    }
}

void RenderTarget::AfterDraw(const DrawIr& ir, const render::DrawResult& res,
                             uint32_t frame_count, uint64_t gfx_epoch, uint64_t draw_seq) {
    if (!res.drew) return;
    const uint32_t fpsm = ir.raster.frame.psm;
    const render::Rect& wr = res.write_rect;
    // The triangle-complete bbox, truncated at the low edge and ceiled at the high
    // edge (the raster extent); the write rect stands in when no bbox was formed.
    int bx0, by0, bx1, by1;
    if (res.bbox_valid) {
        bx0 = static_cast<int>(res.bbox_tri[0]);
        by0 = static_cast<int>(res.bbox_tri[1]);
        bx1 = static_cast<int>(std::ceil(res.bbox_tri[2]));
        by1 = static_cast<int>(std::ceil(res.bbox_tri[3]));
    } else {
        bx0 = wr.x; by0 = wr.y; bx1 = wr.x + wr.w; by1 = wr.y + wr.h;
    }
    // ... and within the scissor (inclusive register bounds -> half-open).
    const DrawIr::Scissor& sc = ir.raster.scissor;
    const int dx0 = std::max(bx0, int(sc.x0)), dy0 = std::max(by0, int(sc.y0));
    const int dx1 = std::min(bx1, int(sc.x1) + 1), dy1 = std::min(by1, int(sc.y1) + 1);
    const bool drawn = dx1 > dx0 && dy1 > dy0;
    CoherenceState& c = coherence;
    if (res.alt16_route) {
        // Extent bookkeeping belongs to the companion; the main image's rects,
        // tags and content flag describe the 32-bit image and stay untouched.
        if (drawn) {
            c.alt16_drawn.Union(dx0, dy0, dx1, dy1);
            ExtendCdDirty(c.cd_copy, dx0, dy0, dx1, dy1);
        }
        views.alt16_has_content = true;
    } else {
        // The valid extent follows the raw bbox, degenerate or not; the drawn /
        // companion rects below only grow on a non-empty scissored extent.
        c.valid_rect.ExpandToInclude(bx0, by0, bx1, by1);
        if (drawn) {
            c.drawn_since_sync.Union(dx0, dy0, dx1, dy1);
            ExtendCdDirty(c.cd_copy, dx0, dy0, dx1, dy1);
            if (views.alt16.id) {
                // The companion no longer mirrors rows the main route wrote; a
                // write covering everything the companion drew drops its content
                // (rebuilt purely from the main image on the next use).
                c.alt16_stale.Union(dx0, dy0, dx1, dy1);
                if (c.Alt16HasDrawn() && dx0 <= c.alt16_drawn.x0 && dy0 <= c.alt16_drawn.y0 &&
                    dx1 >= c.alt16_drawn.x1 && dy1 >= c.alt16_drawn.y1) {
                    views.alt16_has_content = false;
                    c.alt16_drawn.Reset();
                }
            }
        }
        c.has_content = true;
        identity.render_psm = fpsm;
        identity.render_fbw = ir.raster.frame.fbw;
        if (Is32BitClass(fpsm)) identity.has_32bit_content = true;
    }
    c.last_draw_frame = frame_count;
    c.last_draw_gfx_epoch = gfx_epoch;
    // Z: every Z-tested draw pairs the target with its ZBUF (earlier draws may
    // have written the depth attachment); the Z-writing extent (all vertices
    // within the scissor) accumulates for both routes -- they share the depth.
    if (ir.raster.test.zte) {
        c.last_zbp = ir.raster.zbuf.zbp;
        c.last_zpsm = ir.raster.zbuf.psm;
        if (!ir.raster.zbuf.zmsk) {
            c.last_z_wrote = true;
            c.last_z_seq = draw_seq;   // the most recent Z writer of an aliased ZBP wins the depth match
            c.last_z_write_frame = frame_count;
            if (wr.w > 0 && wr.h > 0) c.z_drawn_since_sync.Union(wr.x, wr.y, wr.x + wr.w, wr.y + wr.h);
        }
    }
    const uint32_t bpp = FrameBitsPerPixel(fpsm);
    if (bpp >= 24) identity.was_32bit = true;
    if (bpp == 16) identity.was_16bit = true;
    // MASKED-SYNC: the 5551 bits the 16-bit draw left untouched; 0 for a full
    // write or any other FRAME format.
    c.sync_keep_mask16 = Is16BitColor(fpsm) ? uint16_t(res.fbmsk16) : uint16_t(0);
    // Per-view author counter: every committed draw authors exactly one view; a
    // 16-bit fog-lane draw (Z16 texture read, index-lane keep mask) records
    // itself as that view's lane author.
    uint64_t& seq = res.alt16_route ? c.alt_author_seq : c.main_author_seq;
    seq++;
    const bool lane_class = Is16BitColor(fpsm) && ir.tex_enabled &&
                            (ir.tex.psm == 0x32 || ir.tex.psm == 0x3A) &&
                            uint16_t(res.fbmsk16) == kIndexLaneKeep16;
    if (lane_class) {
        CoherenceState::LaneRecord& lane = res.alt16_route ? c.lane_alt : c.lane_main;
        lane.seq = seq;
        lane.frame = frame_count;
    }
}

}  // namespace gs
}  // namespace ps2r
