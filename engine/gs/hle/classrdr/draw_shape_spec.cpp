// gs/hle/classrdr/draw_shape_spec.cpp -- the field-by-field match.
#include "engine/gs/hle/classrdr/draw_shape_spec.h"

#include <algorithm>

namespace ps2r {
namespace gs {

namespace {

bool Want(int spec, int64_t actual) { return spec == DrawShapeSpec::kAny || spec == actual; }
bool WantB(int spec, bool actual) { return Want(spec, actual ? 1 : 0); }

}  // namespace

bool DrawShapeSpec::Matches(const DrawIr& ir) const {
    const DrawIr::Raster& r = ir.raster;
    if (!Want(prim, ir.prim_type & 7)) return false;
    if (!WantB(gouraud, ir.gouraud)) return false;
    if (!WantB(use_fst, ir.use_fst)) return false;
    if (!WantB(fog, ir.fog)) return false;
    if (!WantB(tex_enabled, ir.tex_enabled)) return false;
    if (!Want(shuffle, ir.shuffle_type)) return false;
    if (!WantB(alpha_blend, ir.alpha_blend)) return false;
    if (ir.alpha_blend && !abcd.empty()) {
        const uint32_t t = Abcd(r.alpha.a, r.alpha.b, r.alpha.c, r.alpha.d);
        if (std::find(abcd.begin(), abcd.end(), t) == abcd.end()) return false;
    }
    if (!Want(fix, r.alpha.fix)) return false;
    if (!WantB(colclamp, r.colclamp)) return false;
    if (!WantB(pabe, r.pabe)) return false;
    if (!WantB(fba, r.fba)) return false;
    if (!WantB(ate, r.test.ate)) return false;
    if (r.test.ate) {
        if (!Want(atst_if_ate, r.test.atst)) return false;
        if (!Want(afail_if_ate, r.test.afail)) return false;
    }
    if (!WantB(date, r.test.date)) return false;
    if (!WantB(datm, r.test.datm)) return false;
    if (!WantB(zte, r.test.zte)) return false;
    if (!Want(ztst, r.test.ztst)) return false;
    if (!WantB(zmsk, r.zbuf.zmsk)) return false;
    if (!Want(frame_psm, r.frame.psm)) return false;
    if (!Want(zbuf_psm, r.zbuf.psm)) return false;
    if (fbmsk != kAny && fbmsk != static_cast<int64_t>(r.frame.fbmsk)) return false;
    if (!Want(scanmsk, ir.scanmsk)) return false;
    if (fbw_nonzero && r.frame.fbw == 0) return false;
    if (ir.verts.size() < min_verts) return false;
    if (ir.tex_enabled) {
        const TexReadView& t = ir.tex;
        if (!Want(tpsm, t.psm)) return false;
        if (!Want(tfx, t.tfx)) return false;
        if (!Want(tcc, t.tcc)) return false;
        if (!Want(cpsm, t.cpsm)) return false;
        if (!Want(csm, t.csm)) return false;
        if (!Want(csa, t.csa)) return false;
        if (!Want(tw, t.tw)) return false;
        if (!Want(th, t.th)) return false;
        if (!tbw.empty() && std::find(tbw.begin(), tbw.end(), t.tbw) == tbw.end()) return false;
        if (!Want(wms, t.wms)) return false;
        if (!Want(wmt, t.wmt)) return false;
        if (!WantB(mmag, t.mmag)) return false;
        if (!Want(mmin, t.mmin)) return false;
    }
    return true;
}

}  // namespace gs
}  // namespace ps2r
