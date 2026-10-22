// gs/hle/ir/draw_coalescer.cpp -- see draw_coalescer.h.
#include "engine/gs/hle/ir/draw_coalescer.h"

#include <utility>

#include "engine/gs/hle/ir/alpha_fail_mask.h"
#include "engine/gs/hle/ir/gsdl_format.h"

namespace ps2r {
namespace gs {

namespace {

bool SameRaster(const DrawIr::Raster& a, const DrawIr::Raster& b) {
    const DrawIr::Test& ta = a.test;
    const DrawIr::Test& tb = b.test;
    return a.frame.fbp == b.frame.fbp && a.frame.fbw == b.frame.fbw && a.frame.psm == b.frame.psm &&
           a.frame.fbmsk == b.frame.fbmsk && a.zbuf.zbp == b.zbuf.zbp && a.zbuf.psm == b.zbuf.psm &&
           a.zbuf.zmsk == b.zbuf.zmsk && ta.ate == tb.ate && ta.atst == tb.atst && ta.aref == tb.aref &&
           ta.date == tb.date && ta.datm == tb.datm && ta.zte == tb.zte && ta.ztst == tb.ztst &&
           a.alpha.a == b.alpha.a && a.alpha.b == b.alpha.b && a.alpha.c == b.alpha.c && a.alpha.d == b.alpha.d &&
           a.alpha.fix == b.alpha.fix && a.scissor.x0 == b.scissor.x0 && a.scissor.y0 == b.scissor.y0 &&
           a.scissor.x1 == b.scissor.x1 && a.scissor.y1 == b.scissor.y1 && a.colclamp == b.colclamp &&
           a.pabe == b.pabe && a.fba == b.fba && a.fogcol_r == b.fogcol_r && a.fogcol_g == b.fogcol_g &&
           a.fogcol_b == b.fogcol_b && a.dthe == b.dthe && a.dimx == b.dimx;
}

void Positions(const DrawIr& d, float out[6]) {
    out[0] = d.verts[0].x; out[1] = d.verts[0].y; out[2] = d.verts[0].z;
    out[3] = d.verts[1].x; out[4] = d.verts[1].y; out[5] = d.verts[1].z;
}

bool SamePositions(const float a[6], const float b[6]) {
    for (int i = 0; i < 6; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

bool SameColour(const VertexRec& v, const uint8_t c[4]) {
    return v.r == c[0] && v.g == c[1] && v.b == c[2] && v.a == c[3];
}

}  // namespace

bool DrawCoalescer::IdentityBlend(const DrawIr& d) {
    if (!d.alpha_blend) return true;
    const DrawIr::Alpha& a = d.raster.alpha;
    return a.a == 0 && a.b == 1 && a.c == 0 && a.d == 1 && d.verts[0].a == 0x80;
}

bool DrawCoalescer::InkCandidate(const DrawIr& d) {
    if ((d.prim_type & 7u) != 1 || d.verts.size() != 2) return false;
    if (d.tex_enabled || d.fog || d.identity.hle_handler != 0) return false;
    if (d.shuffle_type != 0) return false;
    const DrawIr::Test& t = d.raster.test;
    if (!t.ate || t.atst != AlphaFailMask::kAtstNever || t.date) return false;
    if (t.afail != AlphaFailMask::kAfailRgbOnly && t.afail != AlphaFailMask::kAfailZbOnly) return false;
    const VertexRec& a = d.verts[0];
    const VertexRec& b = d.verts[1];
    if (!(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a)) return false;
    return IdentityBlend(d);
}

bool DrawCoalescer::SameRegisters(const DrawIr& a, const DrawIr& b) {
    // ABE is NOT compared: a candidate's blend is the identity (IdentityBlend), so the
    // game's "ABE on the colour pass, off on the Z pass" pair is one run.
    return a.prim_type == b.prim_type && a.gouraud == b.gouraud && a.fog == b.fog && a.use_fst == b.use_fst &&
           a.scanmsk == b.scanmsk && a.shuffle_type == b.shuffle_type &&
           a.shuffle_uv_scale_u == b.shuffle_uv_scale_u && a.tex_enabled == b.tex_enabled && a.ctx == b.ctx &&
           a.off_x == b.off_x && a.off_y == b.off_y && a.src_frame == b.src_frame && a.vram_epoch == b.vram_epoch &&
           a.identity.hle_handler == b.identity.hle_handler && SameRaster(a.raster, b.raster);
}

void DrawCoalescer::Start(DrawIr&& a) {
    colour_[0] = a.verts[0].r; colour_[1] = a.verts[0].g; colour_[2] = a.verts[0].b; colour_[3] = a.verts[0].a;
    Positions(a, last_a_pos_);
    rgb_ = std::move(a);
    have_run_ = true;
    have_z_ = false;
    expect_z_ = true;
}

void DrawCoalescer::Finalize(DrawIr& d) {
    d.vertex_count = static_cast<uint32_t>(d.verts.size());
    d.snapshot.verts_incl = 1;
    d.snapshot.vertex_hash = 0;
    GsdlFormat::Bbox(d.verts.data(), d.verts.size(), d.bbox);
}

void DrawCoalescer::Flush(std::vector<DrawIr>* out) {
    if (!have_run_) return;
    n_.runs++;
    Finalize(rgb_);
    out->push_back(std::move(rgb_));
    n_.out++;
    if (have_z_) {
        Finalize(z_);
        out->push_back(std::move(z_));
        n_.out++;
    }
    rgb_ = DrawIr{};
    z_ = DrawIr{};
    have_run_ = false;
    have_z_ = false;
    expect_z_ = false;
}

bool DrawCoalescer::Push(DrawIr&& d, std::vector<DrawIr>* out) {
    n_.in++;
    if (!InkCandidate(d)) {
        Flush(out);
        n_.passed++;
        return false;
    }
    const bool is_a = d.raster.test.afail == AlphaFailMask::kAfailRgbOnly;
    if (have_run_ && !SameRegisters(rgb_, d)) {
        n_.reject_state++;
        Flush(out);
    }
    if (!have_run_) {
        if (!is_a) {
            n_.reject_orphan_z++;
            n_.passed++;
            return false;
        }
        Start(std::move(d));
        return true;
    }
    if (is_a) {
        if (!SameColour(d.verts[0], colour_)) {
            n_.reject_colour++;
            Flush(out);
            Start(std::move(d));
            return true;
        }
        Positions(d, last_a_pos_);
        rgb_.verts.push_back(d.verts[0]);
        rgb_.verts.push_back(d.verts[1]);
        expect_z_ = true;
        n_.absorbed++;
        return true;
    }
    // A B: the partner of the A just before it, or an orphan.
    float pos[6];
    Positions(d, pos);
    if (!expect_z_ || !SamePositions(pos, last_a_pos_)) {
        n_.reject_orphan_z++;
        Flush(out);
        n_.passed++;
        return false;
    }
    if (!have_z_) {
        z_ = std::move(d);
        have_z_ = true;
    } else {
        z_.verts.push_back(d.verts[0]);
        z_.verts.push_back(d.verts[1]);
        n_.absorbed++;
    }
    expect_z_ = false;
    n_.pairs++;
    return true;
}

}  // namespace gs
}  // namespace ps2r
