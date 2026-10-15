// render/backends/gl/blend_ff_map.cpp -- BlendFfMap implementation.
#include "engine/render/backends/gl/blend_ff_map.h"

namespace ps2r {
namespace render {

FfBlendMap BlendFfMap::Map(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    FfBlendMap m;
    m.matched = true;
    m.uses_fix = false;
    m.eq_rgb = kGlFuncAdd;
    m.src_rgb = kGlOne;
    m.dst_rgb = kGlZero;
    auto is = [&](uint8_t A, uint8_t B, uint8_t C, uint8_t D) {
        return a == A && b == B && c == C && d == D;
    };
    if (is(0, 1, 0, 1)) {            // (Cs-Cd)*As+Cd = Cs*As + Cd*(1-As)
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlOneMinusSrcAlpha;
    } else if (is(0, 2, 0, 1)) {     // (Cs-0)*As+Cd
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlOne;
    } else if (is(0, 2, 0, 2)) {     // (Cs-0)*As+0
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlZero;
    } else if (is(0, 1, 2, 1)) {     // (Cs-Cd)*FIX+Cd
        m.src_rgb = kGlConstantAlpha;
        m.dst_rgb = kGlOneMinusConstantAlpha;
        m.uses_fix = true;
    } else if (is(0, 2, 2, 1)) {     // (Cs-0)*FIX+Cd
        m.src_rgb = kGlConstantAlpha;
        m.dst_rgb = kGlOne;
        m.uses_fix = true;
    } else if (is(1, 0, 0, 0)) {     // (Cd-Cs)*As+Cs = Cd*As + Cs*(1-As)
        m.eq_rgb = kGlFuncAdd;
        m.src_rgb = kGlOneMinusSrcAlpha;
        m.dst_rgb = kGlSrcAlpha;
    } else if (is(0, 2, 2, 2)) {     // (Cs-0)*FIX+0
        m.src_rgb = kGlConstantAlpha;
        m.dst_rgb = kGlZero;
        m.uses_fix = true;
    } else if (is(0, 1, 0, 0)) {     // (Cs-Cd)*As+0: SUBTRACT = Src*sf - Dst*df
        m.eq_rgb = kGlFuncSubtract;
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlSrcAlpha;
    } else if (is(2, 1, 0, 1)) {     // (0-Cd)*As+Cd = Cd*(1-As)
        m.src_rgb = kGlZero;
        m.dst_rgb = kGlOneMinusSrcAlpha;
    } else if (is(1, 2, 0, 0)) {     // (Cd-0)*As+0 = Cd*As
        m.src_rgb = kGlZero;
        m.dst_rgb = kGlSrcAlpha;
    } else if (is(0, 2, 0, 0)) {     // (Cs-0)*As+0
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlZero;
    } else if (is(2, 0, 0, 1)) {     // (0-Cs)*As+Cd = Cd - Cs*As: REVERSE_SUBTRACT
        m.eq_rgb = kGlFuncReverseSubtract;
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlOne;
    } else if (is(2, 0, 2, 1)) {     // (0-Cs)*FIX+Cd = Cd - Cs*FIX
        m.eq_rgb = kGlFuncReverseSubtract;
        m.src_rgb = kGlConstantAlpha;
        m.dst_rgb = kGlOne;
        m.uses_fix = true;
    } else if (is(0, 1, 2, 2)) {     // (Cs-Cd)*FIX+0: SUBTRACT = Src*FIX - Dst*FIX
        m.eq_rgb = kGlFuncSubtract;
        m.src_rgb = kGlConstantAlpha;
        m.dst_rgb = kGlConstantAlpha;
        m.uses_fix = true;
    } else {                         // inexpressible: standard alpha blend approximation
        m.matched = false;
        m.src_rgb = kGlSrcAlpha;
        m.dst_rgb = kGlOneMinusSrcAlpha;
    }
    return m;
}

bool BlendFfMap::Equal(const FfBlendMap& x, const FfBlendMap& y) {
    return x.matched == y.matched && x.uses_fix == y.uses_fix && x.eq_rgb == y.eq_rgb &&
           x.src_rgb == y.src_rgb && x.dst_rgb == y.dst_rgb;
}

bool BlendFfMap::RouteWrapBlend(bool alpha_blend, bool colclamp) {
    return alpha_blend && !colclamp;
}

int BlendFfMap::WrapChannel(int raw) { return raw & 0xFF; }

}  // namespace render
}  // namespace ps2r
