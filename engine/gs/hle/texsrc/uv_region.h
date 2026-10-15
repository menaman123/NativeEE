// gs/hle/texsrc/uv_region.h -- UvRegion: the texel sub-rect a draw's UV bbox
// lets the decode restrict itself to.
//
// From the vertex bbox (UvBbox) and the read's CLAMP / TEX1 registers, per axis:
// the end-exclusive integer texel range [floor(lo), ceil(hi)) (at least one
// texel), widened by one texel each side when the filter is bilinear (MMAG, or an
// odd MMIN = the linear minification filters), then wrapped per CLAMP.WMS/WMT --
// REPEAT keeps the range when it lies in ONE tile of the texture (rebased to it)
// and takes the full axis otherwise, CLAMP intersects [0, W), REGION_CLAMP
// intersects [MINU, MAXU + 1], REGION_REPEAT takes the full axis -- clamped to
// [0, W), a degenerate result taking the full axis, and finally quantised outward
// to the 64-texel grid (clamped to W). The rect is APPLIED only when it is not
// the whole texture and covers at most half of its area; otherwise the decode
// stays full and the class says why (FullIsFull: the pre-quantised rect was
// already full; FullQuant: quantisation grew it to full; FullAreaRule).
// Eligibility: a format whose decode honours a sub-rect (CT32 / CT24 / CT16 / T8 /
// T4 / T8H), at least three vertices, and no mip chain.
#pragma once

#include <cstdint>

#include "engine/gs/hle/ir/tex_read_view.h"
#include "engine/gs/hle/texsrc/uv_bbox.h"

namespace ps2r {
namespace gs {

class UvRegion {
public:
    enum Class : uint8_t { kBail = 0, kRect = 1, kFullIsFull = 2, kFullQuant = 3, kFullAreaRule = 4 };
    struct Result {
        uint8_t cls = kBail;
        int x0 = 0, y0 = 0, w = 0, h = 0;   // valid iff cls == kRect
    };
    static constexpr int kQuantum = 64;

    static bool RectDecodeCapable(uint32_t psm);
    static bool Eligible(uint32_t psm, size_t vertex_count, bool has_mip_chain);
    static Result Derive(const UvBbox::Result& bb, const TexReadView& v, int tex_w, int tex_h);
    // One axis: [f0, f1] texel span -> quantised [*lo, *hi) and the pre-quantised
    // range; `wm` the CLAMP mode, rmin / rmax the REGION_CLAMP bounds.
    static void DeriveAxis(float f0, float f1, int W, bool linear, uint8_t wm, int rmin, int rmax,
                           int* lo, int* hi, int* pre_lo, int* pre_hi);
};

}  // namespace gs
}  // namespace ps2r
