// gs/hle/classrdr/draw_route_trace.cpp -- DrawRouteTrace implementation.
#include "engine/gs/hle/classrdr/draw_route_trace.h"

namespace ps2r {
namespace gs {

void DrawRouteTrace::Note(const DrawIr& ir, const TextureSource& src, const render::DrawResult& res,
                          uint32_t seq) {
    Line l{};
    l.seq = seq;
    l.src_frame = ir.src_frame;
    l.prim = ir.prim_type;
    l.abe = ir.alpha_blend ? 1 : 0;
    l.tex = ir.tex_enabled ? 1 : 0;
    const auto& r = ir.raster;
    l.a = r.alpha.a; l.b = r.alpha.b; l.c = r.alpha.c; l.d = r.alpha.d; l.fix = r.alpha.fix;
    l.tfx = static_cast<uint8_t>(ir.tex.tfx);
    l.tcc = static_cast<uint8_t>(ir.tex.tcc);
    l.owner_kind = static_cast<uint8_t>(src.owner.kind);
    l.fmt_case = src.owner.fmt_case;
    l.ate = r.test.ate; l.atst = r.test.atst; l.aref = r.test.aref; l.afail = r.test.afail;
    l.zte = r.test.zte; l.ztst = r.test.ztst; l.zmsk = r.zbuf.zmsk; l.date = r.test.date;
    l.colclamp = r.colclamp; l.pabe = r.pabe; l.fba = r.fba;
    l.fbp = r.frame.fbp; l.fbmsk = r.frame.fbmsk;
    l.tbp = ir.tex.tbp; l.tpsm = ir.tex.psm; l.cbp = ir.tex.cbp;
    l.verts = static_cast<uint32_t>(ir.verts.size());
    if (!ir.verts.empty()) {
        const VertexRec& v = ir.verts[0];
        l.rgba0 = (static_cast<uint32_t>(v.r) << 24) | (static_cast<uint32_t>(v.g) << 16) |
                  (static_cast<uint32_t>(v.b) << 8) | v.a;
    }
    l.drew = res.drew;
    l.sw_blend = res.sw_blend_active;
    l.route_rt_blend = res.route_rt_blend;
    l.needs_rt_copy = res.needs_rt_copy;
    l.cd_copied = res.cd_copy_performed;
    l.wrap_route = res.wrap_route;
    l.fbmsk16_exact = res.fbmsk16_exact;
    cur_.push_back(l);
}

void DrawRouteTrace::OnFrameEnd() {
    if (cur_.empty()) return;   // a boundary without draws keeps the last drawn frame
    last_.swap(cur_);
    cur_.clear();
}

void DrawRouteTrace::Write(std::FILE* f) const {
    if (!f) return;
    static const char* const kOwner[] = {"none", "surface", "decoded"};
    for (const Line& l : last_) {
        const char* owner = l.owner_kind < 3 ? kOwner[l.owner_kind] : "?";
        const char* blend = !l.abe ? "off" : (l.sw_blend ? "sw" : (l.route_rt_blend ? "ff-rt" : "ff"));
        std::fprintf(f,
                     "seq=%u vf=%u prim=%u abe=%u tex=%u ALPHA=%u%u%u%u fix=%02x tfx=%u tcc=%u "
                     "TEST(ate=%u atst=%u aref=%02x afail=%u date=%u zte=%u ztst=%u) zmsk=%u cc=%u pabe=%u fba=%u "
                     "FRAME=%06x fbmsk=%08x TEX=%06x psm=%u cbp=%06x owner=%s fmt=%u verts=%u rgba0=%08x "
                     "drew=%u blend=%s rtcopy=%u copied=%u wrap=%u m16=%u\n",
                     l.seq, l.src_frame, l.prim, l.abe, l.tex, l.a, l.b, l.c, l.d, l.fix, l.tfx, l.tcc, l.ate,
                     l.atst, l.aref, l.afail, l.date, l.zte, l.ztst, l.zmsk, l.colclamp, l.pabe, l.fba, l.fbp,
                     l.fbmsk, l.tbp, l.tpsm, l.cbp, owner, l.fmt_case, l.verts, l.rgba0, l.drew ? 1 : 0, blend,
                     l.needs_rt_copy ? 1 : 0, l.cd_copied ? 1 : 0, l.wrap_route ? 1 : 0,
                     l.fbmsk16_exact ? 1 : 0);
    }
}

}  // namespace gs
}  // namespace ps2r
