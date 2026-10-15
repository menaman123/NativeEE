// render/backends/gl/blend_ff_map.h -- BlendFfMap: GS ALPHA (A-B)*C+D -> GL fixed-function
// blend state, and the COLCLAMP=0 wrap route.
//
// GS selectors (GS User's Manual ALPHA_1/2): A, B, D in {0 Cs, 1 Cd, 2 zero}; C in
// {0 As, 1 Ad, 2 FIX}. The table maps every combination GL can express exactly on
// a UNORM8 target; the two full-subtractive forms 2001 / 2021 ((0-Cs)*C+Cd) use
// GL_FUNC_REVERSE_SUBTRACT (Dst*dstFactor - Src*srcFactor). Anything else is
// `matched = false` with the standard alpha blend as the legacy approximation. The
// alpha channel is always source-replace at the call site (ONE, ZERO).
//
// With COLCLAMP=0 the GS masks each blend result channel to its low 8 bits instead
// of saturating; fixed-function blending cannot wrap, so blended COLCLAMP=0 draws are
// routed to the shader-side blend (RouteWrapBlend), whose per-channel reference is
// WrapChannel.
#pragma once

#include <cstdint>

namespace ps2r {
namespace render {

// GL enum values (stable ABI constants, spelled out so the table is usable without
// a GL header in the off-GL unit test).
constexpr uint32_t kGlZero = 0;
constexpr uint32_t kGlOne = 1;
constexpr uint32_t kGlSrcAlpha = 0x0302;
constexpr uint32_t kGlOneMinusSrcAlpha = 0x0303;
constexpr uint32_t kGlConstantAlpha = 0x8003;
constexpr uint32_t kGlOneMinusConstantAlpha = 0x8004;
constexpr uint32_t kGlFuncAdd = 0x8006;
constexpr uint32_t kGlFuncSubtract = 0x800A;
constexpr uint32_t kGlFuncReverseSubtract = 0x800B;

struct FfBlendMap {
    bool matched;       // an exact arm hit; false => approximation fallback
    bool uses_fix;      // caller must BlendColor(0,0,0, fix/128)
    uint32_t eq_rgb;
    uint32_t src_rgb;
    uint32_t dst_rgb;
};

class BlendFfMap {
public:
    static FfBlendMap Map(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    static bool Equal(const FfBlendMap& x, const FfBlendMap& y);
    // Blended draws with COLCLAMP=0 take the shader-side blend (wrap semantics).
    static bool RouteWrapBlend(bool alpha_blend, bool colclamp);
    // The COLCLAMP=0 per-channel result for a raw blend value (two's complement wrap).
    static int WrapChannel(int raw);
};

}  // namespace render
}  // namespace ps2r
