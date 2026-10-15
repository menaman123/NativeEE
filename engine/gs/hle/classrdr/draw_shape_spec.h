// gs/hle/classrdr/draw_shape_spec.h -- DrawShapeSpec: a per-field require / any
// description of a draw's register state, matched against a DrawIr.
//
// Every class renderer accepts exactly the state shapes the corpus census proved
// for its class; those shapes are title data (the renderer profile fills one
// spec per shape) and this type is the vocabulary. A field left at kAny is not
// compared. Blend-function tuples are compared only when the draw blends;
// ATST / AFAIL only when the alpha test is on; texture fields only when the draw
// samples a texture. A spec with min_verts = 3 rejects a draw that cannot form a
// triangle, and fbw_nonzero rejects a FRAME with no buffer width.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"

namespace ps2r {
namespace gs {

class DrawShapeSpec {
public:
    static constexpr int kAny = -1;
    // ALPHA.A/B/C/D packed a<<12 | b<<8 | c<<4 | d.
    static constexpr uint32_t Abcd(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        return (a << 12) | (b << 8) | (c << 4) | d;
    }

    int prim = kAny;              // PRIM.PRIM & 7
    int gouraud = kAny;           // PRIM.IIP
    int use_fst = kAny;           // PRIM.FST
    int fog = kAny;               // PRIM.FGE
    int tex_enabled = kAny;       // PRIM.TME
    int shuffle = kAny;           // shuffle_type
    int alpha_blend = kAny;       // PRIM.ABE
    std::vector<uint32_t> abcd;   // allowed ALPHA tuples when blending (empty = any)
    int fix = kAny;               // ALPHA.FIX
    int colclamp = kAny;
    int pabe = kAny;
    int fba = kAny;
    int ate = kAny;               // TEST.ATE
    int atst_if_ate = kAny;       // TEST.ATST when ATE
    int afail_if_ate = kAny;      // TEST.AFAIL when ATE
    int date = kAny;
    int datm = kAny;
    int zte = kAny;
    int ztst = kAny;
    int zmsk = kAny;              // ZBUF.ZMSK
    int frame_psm = kAny;
    int zbuf_psm = kAny;
    int64_t fbmsk = kAny;         // FRAME.FBMSK
    int scanmsk = kAny;
    bool fbw_nonzero = true;
    uint32_t min_verts = 3;
    // Texture fields (compared when the draw samples a texture).
    int tpsm = kAny;
    int tfx = kAny;
    int tcc = kAny;
    int cpsm = kAny;
    int csm = kAny;
    int csa = kAny;
    int tw = kAny;
    int th = kAny;
    std::vector<uint32_t> tbw;    // allowed TEX0.TBW values (empty = any)
    int wms = kAny;
    int wmt = kAny;
    int mmag = kAny;
    int mmin = kAny;

    bool Matches(const DrawIr& ir) const;
};

}  // namespace gs
}  // namespace ps2r
