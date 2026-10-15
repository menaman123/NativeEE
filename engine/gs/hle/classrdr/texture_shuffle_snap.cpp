// gs/hle/classrdr/texture_shuffle_snap.cpp -- detection geometry + snap math.
#include "engine/gs/hle/classrdr/texture_shuffle_snap.h"

#include <cmath>

namespace ps2r {
namespace gs {

bool TextureShuffleSnap::IsPsm16(uint8_t psm) {
    return psm == 0x02 || psm == 0x0A ||   // PSMCT16 / PSMCT16S
           psm == 0x32 || psm == 0x3A;     // PSMZ16 / PSMZ16S
}

float TextureShuffleSnap::SnapToColumn8(float v) { return std::nearbyint(v / 8.0f) * 8.0f; }

float TextureShuffleSnap::SnapRow(float v) {
    const float s = SnapToColumn8(v);
    return s + std::nearbyint(v - s);
}

bool TextureShuffleSnap::DetectColumnShuffle(const VertexRec* verts, size_t count, uint8_t prim,
                                             bool fst, uint8_t frame_psm, uint8_t tex_psm,
                                             bool tex_enabled) {
    if (!tex_enabled || !fst) return false;
    if ((prim & 7) != 6) return false;                          // sprite
    if (frame_psm != 0x02 && frame_psm != 0x0A) return false;   // 16 bpp FRAME
    if (!IsPsm16(tex_psm)) return false;                        // 16 bpp TEX read
    if (count < 6 || (count % 6) != 0) return false;
    for (size_t g = 0; g + 6 <= count; g += 6) {
        float x0 = verts[g].x, x1 = verts[g].x;
        const float u0raw = static_cast<float>(verts[g].uv_u) / 16.0f;
        float u0 = u0raw, u1 = u0raw;
        const float v0raw = static_cast<float>(verts[g].uv_v) / 16.0f;
        float v0 = v0raw, v1 = v0raw;
        for (size_t i = 1; i < 6; ++i) {
            const VertexRec& v = verts[g + i];
            const float u = static_cast<float>(v.uv_u) / 16.0f;
            const float w = static_cast<float>(v.uv_v) / 16.0f;
            x0 = std::fmin(x0, v.x);
            x1 = std::fmax(x1, v.x);
            u0 = std::fmin(u0, u);
            u1 = std::fmax(u1, u);
            v0 = std::fmin(v0, w);
            v1 = std::fmax(v1, w);
        }
        const float xw = x1 - x0;
        const float uw = u1 - u0;
        if (xw <= 0.0f) return false;
        if (std::fabs(xw - uw) > kEps) return false;
        const float xw_snap = SnapToColumn8(xw);
        if (xw_snap < 8.0f || std::fabs(xw - xw_snap) > kEps) return false;
        const float off = std::fabs(x0 - u0);
        const float m16 = std::fmod(off, 16.0f);
        const bool off0 = (m16 <= kEps) || (m16 >= 16.0f - kEps);
        const bool off8 = std::fabs(m16 - 8.0f) <= kEps;
        if (!off0 && !off8) return false;
        if (std::fabs(u0 - SnapToColumn8(u0)) > kEps) return false;
        if (std::fabs(u1 - SnapToColumn8(u1)) > kEps) return false;
        if (std::fabs(v0 - SnapRow(v0)) > kEps) return false;
        if (std::fabs(v1 - SnapRow(v1)) > kEps) return false;
    }
    return true;
}

}  // namespace gs
}  // namespace ps2r
