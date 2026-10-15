// gs/hle/ir/draw_ir.cpp -- DrawIr <-> DrawRec, field for field.
#include "engine/gs/hle/ir/draw_ir.h"

#include <cstring>

namespace ps2r {
namespace gs {

namespace {

uint8_t B(bool v) { return v ? 1 : 0; }

}  // namespace

DrawIr DrawIr::FromDrawRec(const DrawRec& x, const VertexRec* v, size_t n) {
    DrawIr d;
    d.seq = x.seq;
    d.src_frame = x.src_frame;
    d.vram_epoch = x.vram_epoch;
    d.identity.hle_handler = x.hle_handler;
    d.identity.image_hash = x.hle_image;
    d.identity.frame_inst = x.hle_frame_inst;
    d.identity.kick_pc = x.hle_kick_pc;
    d.identity.kick_ord = x.hle_kick_ord;

    d.prim_type = x.prim_type;
    d.gouraud = x.gouraud != 0;
    d.fog = x.fog_enabled != 0;
    d.use_fst = x.use_fst != 0;
    d.alpha_blend = x.alpha_blend != 0;
    d.scanmsk = x.scanmsk;
    d.shuffle_type = x.shuffle_type;
    d.shuffle_uv_scale_u = x.shuffle_uv_scale_u;

    d.tex_enabled = x.tex_enabled != 0;
    TexReadView& t = d.tex;
    t.fst = x.tex_fst != 0;
    t.tbp = x.tbp; t.tbw = x.tbw; t.psm = x.tpsm; t.tw = x.tw; t.th = x.th;
    t.tcc = x.tcc; t.tfx = x.tfx;
    t.cbp = x.cbp; t.cpsm = x.cpsm; t.csm = x.csm; t.csa = x.csa; t.cld = x.cld;
    t.mmag = x.mmag != 0; t.mmin = x.mmin;
    t.wms = x.wms; t.wmt = x.wmt;
    t.minu = x.minu; t.maxu = x.maxu; t.minv = x.minv; t.maxv = x.maxv;
    t.texa_ta0 = x.texa_ta0; t.texa_ta1 = x.texa_ta1; t.texa_aem = x.texa_aem != 0;

    Raster& r = d.raster;
    r.colclamp = x.colclamp != 0; r.pabe = x.pabe != 0; r.fba = x.fba != 0;
    r.alpha.a = x.alpha_a; r.alpha.b = x.alpha_b; r.alpha.c = x.alpha_c;
    r.alpha.d = x.alpha_d; r.alpha.fix = x.fix_alpha;
    r.fogcol_r = x.fogcol_r; r.fogcol_g = x.fogcol_g; r.fogcol_b = x.fogcol_b;
    r.frame.fbp = x.frame_fbp; r.frame.fbw = x.frame_fbw;
    r.frame.fbmsk = x.frame_fbmsk; r.frame.psm = x.frame_psm;
    r.zbuf.psm = x.zbuf_psm; r.zbuf.zmsk = x.zbuf_zmsk != 0; r.zbuf.zbp = x.zbuf_zbp;
    r.scissor.x0 = x.scissor_x0; r.scissor.y0 = x.scissor_y0;
    r.scissor.x1 = x.scissor_x1; r.scissor.y1 = x.scissor_y1;
    r.test.ate = x.test_ate != 0; r.test.atst = x.test_atst;
    r.test.aref = x.test_aref; r.test.afail = x.test_afail;
    r.test.date = x.test_date != 0; r.test.datm = x.test_datm != 0;
    r.test.zte = x.test_zte != 0; r.test.ztst = x.test_ztst;

    d.ctx = x.gs_context;
    d.off_x = x.off_x;
    d.off_y = x.off_y;

    d.vertex_count = x.vertex_count;
    d.verts.assign(v, v + n);
    std::memcpy(d.bbox, x.bbox, sizeof(d.bbox));
    d.snapshot.vram_hash = x.vram_snapshot_hash;
    d.snapshot.clut_hash = x.clut_snapshot_hash;
    d.snapshot.vram_bytes = x.vram_snapshot_bytes;
    d.snapshot.clut_bytes = x.clut_snapshot_bytes;
    d.snapshot.vertex_hash = x.vertex_hash;
    d.snapshot.verts_incl = x.verts_incl;
    return d;
}

DrawRec DrawIr::ToDrawRec() const {
    DrawRec x;
    std::memset(&x, 0, sizeof(x));
    x.seq = seq;
    x.src_frame = src_frame;
    x.vram_epoch = vram_epoch;
    x.prim_type = prim_type; x.gouraud = B(gouraud); x.fog_enabled = B(fog);
    x.use_fst = B(use_fst); x.alpha_blend = B(alpha_blend); x.scanmsk = scanmsk;
    x.shuffle_type = shuffle_type; x.shuffle_uv_scale_u = shuffle_uv_scale_u;

    const TexReadView& t = tex;
    x.tex_enabled = B(tex_enabled); x.tex_fst = B(t.fst);
    x.tfx = static_cast<uint8_t>(t.tfx); x.tcc = static_cast<uint8_t>(t.tcc);
    x.cpsm = static_cast<uint8_t>(t.cpsm); x.csm = static_cast<uint8_t>(t.csm);
    x.csa = static_cast<uint8_t>(t.csa); x.cld = static_cast<uint8_t>(t.cld);
    x.tbp = t.tbp; x.tbw = t.tbw; x.tpsm = t.psm; x.tw = t.tw; x.th = t.th; x.cbp = t.cbp;
    x.mmag = B(t.mmag); x.mmin = t.mmin; x.wms = t.wms; x.wmt = t.wmt;
    x.minu = t.minu; x.maxu = t.maxu; x.minv = t.minv; x.maxv = t.maxv;
    x.texa_ta0 = t.texa_ta0; x.texa_ta1 = t.texa_ta1; x.texa_aem = B(t.texa_aem);
    x.vram_snapshot_hash = snapshot.vram_hash;
    x.clut_snapshot_hash = snapshot.clut_hash;
    x.vram_snapshot_bytes = snapshot.vram_bytes;
    x.clut_snapshot_bytes = snapshot.clut_bytes;

    const Raster& r = raster;
    x.colclamp = B(r.colclamp); x.pabe = B(r.pabe); x.fba = B(r.fba);
    x.alpha_a = r.alpha.a; x.alpha_b = r.alpha.b; x.alpha_c = r.alpha.c;
    x.alpha_d = r.alpha.d; x.fix_alpha = r.alpha.fix;
    x.fogcol_r = r.fogcol_r; x.fogcol_g = r.fogcol_g; x.fogcol_b = r.fogcol_b;
    x.frame_fbp = r.frame.fbp; x.frame_fbw = r.frame.fbw; x.frame_fbmsk = r.frame.fbmsk;
    x.frame_psm = r.frame.psm; x.zbuf_psm = r.zbuf.psm; x.zbuf_zmsk = B(r.zbuf.zmsk);
    x.zbuf_zbp = r.zbuf.zbp;
    x.scissor_x0 = r.scissor.x0; x.scissor_y0 = r.scissor.y0;
    x.scissor_x1 = r.scissor.x1; x.scissor_y1 = r.scissor.y1;
    x.test_ate = B(r.test.ate); x.test_atst = r.test.atst; x.test_aref = r.test.aref;
    x.test_afail = r.test.afail; x.test_date = B(r.test.date); x.test_datm = B(r.test.datm);
    x.test_zte = B(r.test.zte); x.test_ztst = r.test.ztst;

    x.vertex_count = vertex_count;
    x.verts_incl = snapshot.verts_incl;
    x.vertex_hash = snapshot.vertex_hash;
    std::memcpy(x.bbox, bbox, sizeof(x.bbox));

    x.hle_handler = identity.hle_handler;
    x.hle_frame_inst = identity.frame_inst;
    x.hle_image = identity.image_hash;
    x.hle_kick_pc = identity.kick_pc;
    x.hle_kick_ord = identity.kick_ord;
    x.off_x = off_x;
    x.off_y = off_y;
    x.gs_context = ctx;
    return x;
}

}  // namespace gs
}  // namespace ps2r
