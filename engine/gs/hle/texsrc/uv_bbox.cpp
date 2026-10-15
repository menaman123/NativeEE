// gs/hle/texsrc/uv_bbox.cpp -- UvBbox::Scan.
#include "engine/gs/hle/texsrc/uv_bbox.h"

#include <cfenv>
#include <cfloat>
#include <cmath>

namespace ps2r {
namespace gs {

UvBbox::Result UvBbox::Scan(const VertexRec* v, size_t n, bool fst, int tex_w, int tex_h) {
    Result out;
    // Only the FST=0 divide / multiply are rounding-mode sensitive; the FST=1 ops
    // ((float)uint16, / 16.0f, compares) are exact under every mode.
    int rm_saved = FE_TONEAREST;
    bool rm_switched = false;
    if (!fst) {
        rm_saved = fegetround();
        if (rm_saved != FE_TONEAREST) {
            fesetround(FE_TONEAREST);
            rm_switched = true;
        }
    }
    float u0 = FLT_MAX, v0 = FLT_MAX, u1 = -FLT_MAX, v1 = -FLT_MAX;
    bool bail = false;
    for (size_t i = 0; i < n && !bail; i++) {
        const VertexRec& sv = v[i];
        float tu, tv;
        if (fst) {
            tu = static_cast<float>(sv.uv_u) / 16.0f;
            tv = static_cast<float>(sv.uv_v) / 16.0f;
        } else {
            const float q = sv.q;
            if (!(q > 0.0f) || !std::isfinite(q)) { bail = true; break; }
            tu = (sv.s / q) * static_cast<float>(tex_w);
            tv = (sv.t / q) * static_cast<float>(tex_h);
        }
        if (!std::isfinite(tu) || !std::isfinite(tv) || std::fabs(tu) > 1e7f || std::fabs(tv) > 1e7f) {
            bail = true;
            break;
        }
        if (tu < u0) u0 = tu;
        if (tv < v0) v0 = tv;
        if (tu > u1) u1 = tu;
        if (tv > v1) v1 = tv;
    }
    if (rm_switched) fesetround(rm_saved);
    if (bail || u0 > u1 || v0 > v1) {
        out.state = kBail;
        return out;
    }
    out.state = kOk;
    out.u0 = u0;
    out.v0 = v0;
    out.u1 = u1;
    out.v1 = v1;
    return out;
}

}  // namespace gs
}  // namespace ps2r
