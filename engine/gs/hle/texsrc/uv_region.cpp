// gs/hle/texsrc/uv_region.cpp -- UvRegion.
#include "engine/gs/hle/texsrc/uv_region.h"

#include <cmath>

namespace ps2r {
namespace gs {

bool UvRegion::RectDecodeCapable(uint32_t psm) {
    return psm == 0x00 || psm == 0x01 || psm == 0x02 || psm == 0x13 || psm == 0x14 || psm == 0x1B;
}

bool UvRegion::Eligible(uint32_t psm, size_t vertex_count, bool has_mip_chain) {
    return RectDecodeCapable(psm) && vertex_count >= 3 && !has_mip_chain;
}

void UvRegion::DeriveAxis(float f0, float f1, int W, bool linear, uint8_t wm, int rmin, int rmax,
                          int* out_lo, int* out_hi, int* pre_lo, int* pre_hi) {
    // Sample range in texels, end-exclusive.
    long lo = static_cast<long>(std::floor(f0));
    long hi = static_cast<long>(std::ceil(f1));
    if (hi <= lo) hi = lo + 1;
    if (linear) { lo -= 1; hi += 1; }   // bilinear taps
    switch (wm) {
        case 0: {   // REPEAT: single-tile shrink else full
            const long t0 = static_cast<long>(std::floor(static_cast<double>(lo) / W));
            const long t1 = static_cast<long>(std::floor(static_cast<double>(hi - 1) / W));
            if (t0 == t1) {
                lo -= t0 * W;
                hi -= t0 * W;
            } else {
                lo = 0;
                hi = W;
            }
            break;
        }
        case 1:     // CLAMP: intersect [0, W) (below)
            break;
        case 2:     // REGION_CLAMP: intersect [minu, maxu + 1]
            if (lo < rmin) lo = rmin;
            if (hi > rmax + 1) hi = rmax + 1;
            break;
        default:    // REGION_REPEAT: full axis
            lo = 0;
            hi = W;
            break;
    }
    if (lo < 0) lo = 0;
    if (hi > W) hi = W;
    if (hi <= lo) { lo = 0; hi = W; }   // degenerate: full
    *pre_lo = static_cast<int>(lo);
    *pre_hi = static_cast<int>(hi);
    // Outward quantisation to the 64-texel grid, clamped to [0, W].
    const long q = kQuantum;
    lo = (lo / q) * q;
    hi = ((hi + q - 1) / q) * q;
    if (hi > W) hi = W;
    *out_lo = static_cast<int>(lo);
    *out_hi = static_cast<int>(hi);
}

UvRegion::Result UvRegion::Derive(const UvBbox::Result& bb, const TexReadView& v, int tex_w, int tex_h) {
    Result out;
    if (bb.state != UvBbox::kOk) return out;   // kBail
    const bool linear = v.mmag || (v.mmin & 1);
    int rx_lo, rx_hi, ry_lo, ry_hi, px_lo, px_hi, py_lo, py_hi;
    DeriveAxis(bb.u0, bb.u1, tex_w, linear, v.wms, v.minu, v.maxu, &rx_lo, &rx_hi, &px_lo, &px_hi);
    DeriveAxis(bb.v0, bb.v1, tex_h, linear, v.wmt, v.minv, v.maxv, &ry_lo, &ry_hi, &py_lo, &py_hi);
    const uint64_t rect_area = static_cast<uint64_t>(rx_hi - rx_lo) * static_cast<uint64_t>(ry_hi - ry_lo);
    const uint64_t full_area = static_cast<uint64_t>(tex_w) * static_cast<uint64_t>(tex_h);
    const bool is_full = rx_lo == 0 && ry_lo == 0 && rx_hi == tex_w && ry_hi == tex_h;
    if (!is_full && 2 * rect_area <= full_area) {
        out.cls = kRect;
        out.x0 = rx_lo;
        out.y0 = ry_lo;
        out.w = rx_hi - rx_lo;
        out.h = ry_hi - ry_lo;
    } else if (is_full) {
        const bool pre_full = px_lo == 0 && py_lo == 0 && px_hi == tex_w && py_hi == tex_h;
        out.cls = pre_full ? kFullIsFull : kFullQuant;
    } else {
        out.cls = kFullAreaRule;
    }
    return out;
}

}  // namespace gs
}  // namespace ps2r
