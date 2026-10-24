// gs/hle/ir/halfword_shuffle.cpp -- HalfwordShuffle (see halfword_shuffle.h).
#include "engine/gs/hle/ir/halfword_shuffle.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ps2r {
namespace gs {

uint16_t HalfwordShuffle::Fbmsk16From32(uint32_t m) {
    return static_cast<uint16_t>(((m >> 3) & 0x1F) | ((m >> 6) & 0x3E0) | ((m >> 9) & 0x7C00) |
                                 ((m >> 16) & 0x8000));
}

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 0.6f; }

// The texel function must be the identity on the 16-bit value: DECAL, or MODULATE by a
// vertex colour of exactly 0x80 (x * 128 >> 7 = x).
bool IdentityTexelFunction(const DrawIr& ir) {
    if (ir.tex.tfx == 1) return true;
    if (ir.tex.tfx != 0) return false;
    for (const VertexRec& v : ir.verts)
        if (v.r != 0x80 || v.g != 0x80 || v.b != 0x80 || v.a != 0x80) return false;
    return true;
}

// No blend term: ABE off, or (A - B) * C + D with A = B and D = Cs.
bool NoBlendTerm(const DrawIr& ir) {
    if (!ir.alpha_blend) return true;
    const auto& a = ir.raster.alpha;
    return a.a == a.b && a.d == 0;
}

// Every pixel reaches the frame buffer the same way: the test is off, passes all, or
// fails all with the frame buffer still written (FB_ONLY; RGB_ONLY keeps the A bit,
// folded into the mask by the caller of this predicate).
bool UniformFrameWrite(const DrawIr::Test& t, bool* rgb_only) {
    *rgb_only = false;
    if (t.date) return false;
    if (t.zte && t.ztst != 1) return false;   // a per-pixel Z test cannot be folded
    if (!t.ate || t.atst == 1) return true;
    if (t.atst != 0) return false;
    if (t.afail == 1) return true;
    if (t.afail == 3) { *rgb_only = true; return true; }
    return false;
}

}  // namespace

bool HalfwordShuffle::Detect(const DrawIr& ir, Op* op) {
    const DrawIr::Frame& f = ir.raster.frame;
    const TexReadView& t = ir.tex;
    if (ir.prim_type != 6 || !ir.tex_enabled || !t.fst) return false;
    if (!Is16BitColor(static_cast<uint8_t>(f.psm)) || !Is16BitColor(static_cast<uint8_t>(t.psm))) return false;
    if (f.fbw == 0 || (f.fbw % 16) != 0 || t.tbw != f.fbw) return false;
    if (ir.scanmsk != 0) return false;
    // FBA=1 forces the CT16 A bit to 1 on the frame write; the fast path writes it from TEXA.
    if (ir.raster.fba) return false;
    // TCC=0: the texel function's output alpha is the vertex alpha, not the TEXA expansion.
    if (t.tcc == 0) return false;
    // DTHE=1 dithers the colour fields before the 5-bit truncation: not a clean half-word copy.
    if (ir.raster.dthe != 0) return false;
    if (!IdentityTexelFunction(ir) || !NoBlendTerm(ir)) return false;
    bool rgb_only = false;
    if (!UniformFrameWrite(ir.raster.test, &rgb_only)) return false;
    const size_t n = ir.verts.size();
    if (n < 6 || (n % 6) != 0) return false;
    const size_t sprites = n / 6;
    if (sprites != f.fbw / 16) return false;
    // Every sprite: an 8-wide, 16-aligned column of one parity spanning [0, h16) with
    // the texture column shifted by 8 the other way and v = y.
    int parity = -1;
    float y1_all = 0.f;
    std::vector<int> columns;
    columns.reserve(sprites);
    for (size_t s = 0; s < sprites; ++s) {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
        for (size_t i = s * 6; i < s * 6 + 6; ++i) {
            const VertexRec& v = ir.verts[i];
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
            y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            const float u = static_cast<float>(v.uv_u) / 16.0f, vv = static_cast<float>(v.uv_v) / 16.0f;
            u0 = std::min(u0, u); u1 = std::max(u1, u);
            v0 = std::min(v0, vv); v1 = std::max(v1, vv);
        }
        if (!Near(x1 - x0, 8.f) || !Near(y0, 0.f)) return false;
        const int col = static_cast<int>(std::lround(x0));
        if (!Near(x0, static_cast<float>(col)) || (col % 8) != 0 || col < 0 || col + 8 > static_cast<int>(f.fbw)) return false;
        const int par = (col % 16) ? 1 : 0;
        if (parity < 0) { parity = par; y1_all = y1; }
        if (par != parity || !Near(y1, y1_all)) return false;
        const float src_col = static_cast<float>(par ? col - 8 : col + 8);
        if (!Near(u0 - 0.5f, src_col) || !Near(u1 - u0, 8.f)) return false;
        if (!Near(v0 - 0.5f, y0) || !Near(v1 - v0, y1 - y0)) return false;
        columns.push_back(col);
    }
    std::sort(columns.begin(), columns.end());
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i] != static_cast<int>(i) * 16 + (parity ? 8 : 0)) return false;
    const int h16 = static_cast<int>(std::lround(y1_all));
    if (h16 <= 0 || (h16 % 64) != 0 || !Near(y1_all, static_cast<float>(h16))) return false;
    // The scissor must hold the extent (registers inclusive).
    const DrawIr::Scissor& sc = ir.raster.scissor;
    if (sc.x0 > 0 || sc.y0 > 0 || static_cast<int>(sc.x1) + 1 < static_cast<int>(f.fbw) || static_cast<int>(sc.y1) + 1 < h16)
        return false;
    op->dest_hi = parity == 1;
    op->mask16 = Fbmsk16From32(f.fbmsk);
    if (rgb_only) op->mask16 = static_cast<uint16_t>(op->mask16 | 0x8000);
    op->cols = static_cast<int>(f.fbw);
    op->rows32 = h16 / 2;
    op->self = t.tbp == f.fbp;
    op->ta0 = t.texa_ta0;
    op->ta1 = t.texa_ta1;
    op->aem = t.texa_aem;
    return true;
}

}  // namespace gs
}  // namespace ps2r
