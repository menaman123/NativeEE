// tests/render/test_blend_ff_map.cpp -- Render_BlendFfMapTable / Gs_Inv34_ReverseSubtract:
// the ALPHA (A-B)*C+D -> GL fixed-function table, the REVERSE_SUBTRACT arms, the wrap
// route predicate and the COLCLAMP=0 per-channel wrap reference. Off-GL.
#include <cstdio>
#include <cstdint>

#include "engine/render/backends/gl/blend_ff_map.h"

using ps2r::render::BlendFfMap;
using ps2r::render::FfBlendMap;
namespace r = ps2r::render;

static int g_checks = 0, g_fail = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static FfBlendMap Exp(bool fix, uint32_t eq, uint32_t src, uint32_t dst) {
    return FfBlendMap{true, fix, eq, src, dst};
}

int main() {
    struct Row { uint8_t a, b, c, d; FfBlendMap want; };
    const Row rows[] = {
        {0, 1, 0, 1, Exp(false, r::kGlFuncAdd, r::kGlSrcAlpha, r::kGlOneMinusSrcAlpha)},
        {0, 2, 0, 1, Exp(false, r::kGlFuncAdd, r::kGlSrcAlpha, r::kGlOne)},
        {0, 2, 0, 2, Exp(false, r::kGlFuncAdd, r::kGlSrcAlpha, r::kGlZero)},
        {0, 1, 2, 1, Exp(true, r::kGlFuncAdd, r::kGlConstantAlpha, r::kGlOneMinusConstantAlpha)},
        {0, 2, 2, 1, Exp(true, r::kGlFuncAdd, r::kGlConstantAlpha, r::kGlOne)},
        {1, 0, 0, 0, Exp(false, r::kGlFuncAdd, r::kGlOneMinusSrcAlpha, r::kGlSrcAlpha)},
        {0, 2, 2, 2, Exp(true, r::kGlFuncAdd, r::kGlConstantAlpha, r::kGlZero)},
        {0, 1, 0, 0, Exp(false, r::kGlFuncSubtract, r::kGlSrcAlpha, r::kGlSrcAlpha)},
        {2, 1, 0, 1, Exp(false, r::kGlFuncAdd, r::kGlZero, r::kGlOneMinusSrcAlpha)},
        {1, 2, 0, 0, Exp(false, r::kGlFuncAdd, r::kGlZero, r::kGlSrcAlpha)},
        {0, 2, 0, 0, Exp(false, r::kGlFuncAdd, r::kGlSrcAlpha, r::kGlZero)},
        {2, 0, 0, 1, Exp(false, r::kGlFuncReverseSubtract, r::kGlSrcAlpha, r::kGlOne)},
        {2, 0, 2, 1, Exp(true, r::kGlFuncReverseSubtract, r::kGlConstantAlpha, r::kGlOne)},
        {0, 1, 2, 2, Exp(true, r::kGlFuncSubtract, r::kGlConstantAlpha, r::kGlConstantAlpha)},   // (Cs-Cd)*FIX
    };
    for (const Row& row : rows) {
        const FfBlendMap got = BlendFfMap::Map(row.a, row.b, row.c, row.d);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "table arm %d%d%d%d", row.a, row.b, row.c, row.d);
        CHECK(BlendFfMap::Equal(got, row.want), msg);
    }
    // A = B: the product term is zero for every C, so the result is D exactly --
    // Cs (ONE, ZERO), Cd (ZERO, ONE: the frame colour untouched, alpha source-replaced
    // at the call site) or zero (ZERO, ZERO); never the alpha-blend fallback.
    for (int ab = 0; ab < 3; ab++)
        for (int c = 0; c < 3; c++) {
            const FfBlendMap cs = BlendFfMap::Map(ab, ab, c, 0);
            const FfBlendMap cd = BlendFfMap::Map(ab, ab, c, 1);
            const FfBlendMap zero = BlendFfMap::Map(ab, ab, c, 2);
            CHECK(BlendFfMap::Equal(cs, Exp(false, r::kGlFuncAdd, r::kGlOne, r::kGlZero)), "A=B, D=Cs is source replace");
            CHECK(BlendFfMap::Equal(cd, Exp(false, r::kGlFuncAdd, r::kGlZero, r::kGlOne)), "A=B, D=Cd keeps the frame colour");
            CHECK(BlendFfMap::Equal(zero, Exp(false, r::kGlFuncAdd, r::kGlZero, r::kGlZero)), "A=B, D=0 writes zero");
        }
    // Every other combo of the 3x3x3x3 space is the approximation fallback.
    int matched = 0, fallback = 0;
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++)
            for (int c = 0; c < 3; c++)
                for (int d = 0; d < 3; d++) {
                    const FfBlendMap m = BlendFfMap::Map(a, b, c, d);
                    bool in_table = (a == b);
                    for (const Row& row : rows)
                        if (row.a == a && row.b == b && row.c == c && row.d == d) in_table = true;
                    if (in_table) { matched++; CHECK(m.matched, "table arm reports matched"); continue; }
                    fallback++;
                    CHECK(!m.matched, "non-table combo is a fallback");
                    CHECK(m.eq_rgb == r::kGlFuncAdd && m.src_rgb == r::kGlSrcAlpha &&
                          m.dst_rgb == r::kGlOneMinusSrcAlpha && !m.uses_fix,
                          "fallback is the standard alpha blend");
                }
    CHECK(matched == 14 + 27 && fallback == 81 - 14 - 27, "14 table arms + 27 A=B arms, 40 fallbacks");
    // Invariant 34: the full-subtractive portal core (0-Cs)*As+Cd is REVERSE_SUBTRACT,
    // never the fallback that paints out = Cs.
    const FfBlendMap portal = BlendFfMap::Map(2, 0, 0, 1);
    CHECK(portal.matched && portal.eq_rgb == r::kGlFuncReverseSubtract, "2001 is reverse-subtract");
    CHECK(!portal.uses_fix, "2001 uses As, not FIX");
    const FfBlendMap fixsub = BlendFfMap::Map(2, 0, 2, 1);
    CHECK(fixsub.uses_fix && fixsub.src_rgb == r::kGlConstantAlpha, "2021 constant-alpha reverse-subtract");
    // Wrap route: only blended COLCLAMP=0 draws.
    CHECK(BlendFfMap::RouteWrapBlend(true, false), "abe=1 colclamp=0 routes");
    CHECK(!BlendFfMap::RouteWrapBlend(true, true), "colclamp=1 keeps its route");
    CHECK(!BlendFfMap::RouteWrapBlend(false, false), "abe=0 never routes");
    // Wrap reference: the shadow accumulator (8 + 248 => 256 wraps to 0; 2*mask mod 256).
    CHECK(BlendFfMap::WrapChannel(256) == 0, "256 wraps to 0");
    CHECK(BlendFfMap::WrapChannel(300) == 44, "300 wraps to 44");
    CHECK(BlendFfMap::WrapChannel(-1) == 255, "-1 wraps to 255");
    CHECK(BlendFfMap::WrapChannel(255) == 255, "255 unchanged");
    std::printf("test_blend_ff_map: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
