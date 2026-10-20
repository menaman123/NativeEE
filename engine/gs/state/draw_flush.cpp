// gs/state/draw_flush.cpp -- DrawFlush (see draw_flush.h).
#include "engine/gs/state/draw_flush.h"

#include <cstring>
#include <utility>

#include "engine/core/hash/hash.h"
#include "engine/gs/hle/ir/gsdl_format.h"

namespace ps2r {
namespace gs {

void DrawFlush::SetJobContext(uint32_t* seq_counter, const uint32_t* src_frame, const uint64_t* vram_epoch) {
    seq_ = seq_counter;
    src_frame_ = src_frame;
    vram_epoch_ = vram_epoch;
}

// MIPTBP1 (3.4.11): TBP1 0-13 TBW1 14-19 TBP2 20-33 TBW2 34-39 TBP3 40-53 TBW3 54-59;
// MIPTBP2 the same for levels 4..6. Bases in 64-word blocks, widths in 64 pixels.
void DrawFlush::FillTexture(DrawIr& d, const GsRegisters::Context& c) const {
    TexReadView& t = d.tex;
    t.tbp = c.tex0.tbp; t.tbw = c.tex0.tbw; t.psm = c.tex0.psm; t.tw = c.tex0.tw; t.th = c.tex0.th;
    t.tcc = c.tex0.tcc; t.tfx = c.tex0.tfx; t.cbp = c.tex0.cbp; t.cpsm = c.tex0.cpsm; t.csm = c.tex0.csm;
    t.csa = c.tex0.csa; t.cld = c.tex0.cld;
    t.fst = regs_->attr.uv_written_since_st;
    t.mmag = c.tex1.mmag; t.mmin = c.tex1.mmin; t.mxl = c.tex1.mxl; t.lcm = c.tex1.lcm; t.l = c.tex1.l;
    // TEX1.K (bits 43:32) is a SIGNED 12-bit s7.4 LOD parameter (3.4.12: LOD = (log2(1/|Q|)
    // << L) + K): the register keeps the raw field, the draw carries its value. A title's
    // negative bias read unsigned would be a +128..+255 bias that clamps every mip-mapped
    // read at MXL (the coarsest authored level).
    const uint16_t raw_k = static_cast<uint16_t>(c.tex1.k & 0xFFF);
    t.k = static_cast<int16_t>((raw_k & 0x800) ? static_cast<int>(raw_k) - 0x1000 : static_cast<int>(raw_k));
    t.wms = c.clamp.wms; t.wmt = c.clamp.wmt;
    t.minu = c.clamp.minu; t.maxu = c.clamp.maxu; t.minv = c.clamp.minv; t.maxv = c.clamp.maxv;
    t.texa_ta0 = regs_->env.texa_ta0; t.texa_ta1 = regs_->env.texa_ta1; t.texa_aem = regs_->env.texa_aem;
    const uint64_t m[2] = {c.miptbp1, c.miptbp2};
    for (int lvl = 0; lvl < 6; ++lvl) {
        const uint64_t w = m[lvl / 3];
        const int sh = (lvl % 3) * 20;
        t.mip_tbp[lvl] = static_cast<uint32_t>((w >> sh) & 0x3FFF) * 256;
        t.mip_tbw[lvl] = static_cast<uint32_t>((w >> (sh + 14)) & 0x3F) * 64;
    }
}

void DrawFlush::FillRaster(DrawIr& d, const GsRegisters::Context& c) const {
    const GsRegisters::Env& e = regs_->env;
    DrawIr::Raster& r = d.raster;
    r.colclamp = e.colclamp; r.pabe = e.pabe; r.dthe = e.dthe ? 1 : 0; r.dimx = e.dimx; r.fba = c.fba;
    r.alpha.a = c.alpha.a; r.alpha.b = c.alpha.b; r.alpha.c = c.alpha.c; r.alpha.d = c.alpha.d; r.alpha.fix = c.alpha.fix;
    r.fogcol_r = e.fogcol_r; r.fogcol_g = e.fogcol_g; r.fogcol_b = e.fogcol_b;
    r.frame.fbp = c.frame.fbp; r.frame.fbw = c.frame.fbw; r.frame.psm = static_cast<uint8_t>(c.frame.psm); r.frame.fbmsk = c.frame.fbmsk;
    r.zbuf.zbp = c.zbuf.zbp; r.zbuf.psm = static_cast<uint8_t>(c.zbuf.psm); r.zbuf.zmsk = c.zbuf.zmsk;
    r.scissor.x0 = c.scissor.x0; r.scissor.x1 = c.scissor.x1; r.scissor.y0 = c.scissor.y0; r.scissor.y1 = c.scissor.y1;
    r.test.ate = c.test.ate; r.test.atst = c.test.atst; r.test.aref = c.test.aref; r.test.afail = c.test.afail;
    r.test.date = c.test.date; r.test.datm = c.test.datm; r.test.zte = c.test.zte; r.test.ztst = c.test.ztst;
}

bool DrawFlush::Flush(FlushKind kind) {
    if (!kick_->HasPending()) return false;
    if (before_) before_();
    const GsRegisters& r = *regs_;
    const uint8_t ctx = r.prim.context & 1;
    const GsRegisters::Context& c = r.ctx[ctx];
    DrawIr d;
    d.verts = std::move(kick_->DrawBuffer());
    kick_->DrawBuffer().clear();
    d.vertex_count = static_cast<uint32_t>(d.verts.size());
    d.prim_type = r.prim.type;
    d.ctx = ctx;
    d.tex_enabled = kick_->BatchTextureEnable();
    if (d.tex_enabled) FillTexture(d, c);
    d.tex.fst = r.attr.uv_written_since_st;  // recorded even when untextured
    regs_->env.texture_dirty = d.tex_enabled ? false : regs_->env.texture_dirty;
    FillRaster(d, c);
    d.alpha_blend = r.prim.alpha_blend;
    d.gouraud = r.prim.gouraud;
    d.fog = r.prim.fog;
    d.use_fst = r.prim.use_uv;
    d.scanmsk = r.env.scanmsk;
    d.off_x = c.xyoffset.x;
    d.off_y = c.xyoffset.y;
    d.identity = identity_;
    d.seq = seq_ ? (*seq_)++ : 0;
    d.src_frame = src_frame_ ? *src_frame_ : 0;
    d.vram_epoch = vram_epoch_ ? *vram_epoch_ : 0;
    d.snapshot.verts_incl = 1;
    if (record_snapshot_) {   // recorder extras only (SetRecordSnapshot); nothing downstream reads them
        d.snapshot.vertex_hash = GsdlFormat::VertexHash(d.verts.data(), d.verts.size());
        GsdlFormat::Bbox(d.verts.data(), d.verts.size(), d.bbox);
    }
    emitted_++;
    if (sink_) sink_->OnDrawOwned(std::move(d));
    kick_->DrawBuffer().reserve(512);
    if (kind == FlushKind::StateChange) {
        kick_->ResetStrip();
        regs_->SnapshotState();
    }
    return true;
}

// FNV-1a-64 over the salt and the state fields in a fixed order; an empty vertex
// array keys to 0 (the empty-batch sentinel) and a non-empty one never does (0
// maps to 1). The basis is the original runtime's merge-key seed -- the FNV
// offset basis with its last decimal digit dropped (1469598103934665603, not
// 14695981039346656037) -- kept so the key VALUE equals the original's.
uint64_t DrawFlush::RunKey(const DrawIr& d) {
    if (d.verts.empty()) return 0;
    constexpr uint64_t kMergeKeyBasis = 1469598103934665603ull;
    uint64_t h = kMergeKeyBasis;
    auto put = [&h](const void* p, size_t n) { h = hash::Fnv1a64(p, n, h); };
    static const char kSalt[] = "W14R5AP1";
    put(kSalt, 8);
    const uint32_t a[8] = {d.raster.frame.fbp, d.raster.frame.fbw, d.raster.frame.psm, d.raster.frame.fbmsk,
                           d.raster.zbuf.zbp, d.raster.zbuf.psm, d.raster.zbuf.zmsk ? 1u : 0u, d.prim_type};
    put(a, sizeof(a));
    const uint16_t sc[4] = {d.raster.scissor.x0, d.raster.scissor.y0, d.raster.scissor.x1, d.raster.scissor.y1};
    put(sc, sizeof(sc));
    if (d.tex_enabled) {
        const TexReadView& t = d.tex;
        const uint32_t tx[16] = {1, t.fst ? 1u : 0u, t.tbp, t.tbw, t.psm, t.tw, t.th, t.tcc, t.tfx, t.cbp, t.cpsm,
                                 t.csm, t.csa, t.cld, t.mmag ? 1u : 0u, t.mmin};
        put(tx, sizeof(tx));
        const uint16_t cl[6] = {t.wms, t.wmt, t.minu, t.maxu, t.minv, t.maxv};
        put(cl, sizeof(cl));
        const uint8_t ta[3] = {t.texa_ta0, t.texa_ta1, t.texa_aem ? uint8_t{1} : uint8_t{0}};
        put(ta, sizeof(ta));
    } else {
        const uint32_t zero = 0;
        put(&zero, sizeof(zero));
    }
    const DrawIr::Raster& r = d.raster;
    const uint8_t bl[12] = {d.alpha_blend, r.alpha.a, r.alpha.b, r.alpha.c, r.alpha.d, r.alpha.fix, r.colclamp,
                            r.pabe, r.fba, r.fogcol_r, r.fogcol_g, r.fogcol_b};
    put(bl, sizeof(bl));
    uint8_t dz[9];
    dz[0] = r.dthe;
    std::memcpy(dz + 1, &r.dimx, 8);
    put(dz, sizeof(dz));
    const uint8_t te[8] = {r.test.ate, r.test.atst, r.test.aref, r.test.afail, r.test.date, r.test.datm, r.test.zte, r.test.ztst};
    put(te, sizeof(te));
    const uint8_t misc[5] = {d.gouraud, d.fog, d.use_fst, d.scanmsk, d.shuffle_type};
    put(misc, sizeof(misc));
    return h == 0 ? 1 : h;
}

}  // namespace gs
}  // namespace ps2r
