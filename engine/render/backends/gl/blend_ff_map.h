// render/backends/gl/blend_ff_map.h -- BlendFfMap: GS ALPHA (A-B)*C+D -> GL fixed-function
// blend state, and the COLCLAMP=0 wrap route.
//
// GS selectors (GS User's Manual ALPHA_1/2): A, B, D in {0 Cs, 1 Cd, 2 zero}; C in
// {0 As, 1 Ad, 2 FIX}. The table maps every combination GL can express exactly on
// a UNORM8 target: A = B makes the product term zero for any C, so Cv = D (Cs, Cd
// or 0 -- the "Cd" form is a game's write-nothing-but-alpha / Z-only-through-blend
// idiom); the two full-subtractive forms 2001 / 2021 ((0-Cs)*C+Cd) use
// GL_FUNC_REVERSE_SUBTRACT (Dst*dstFactor - Src*srcFactor). Anything else is
// `matched = false` with the standard alpha blend as the legacy approximation. The
// alpha channel is always source-replace at the call site (ONE, ZERO).
//
// The unified blend state (BlendFfMap::Unified, GlDevice::UnifiedBlend): the GL-on-
// Metal driver turns every glBlendFuncSeparate / glBlendColor into a pipeline-state
// switch, and 98 % of consecutive Sly 2 draws alternate ALPHA 0101 / 0201 (~1900
// switches a frame). Under one state, func(SRC1_COLOR, ONE_MINUS_SRC1_ALPHA) -- the
// factor paths of the 0101 state -- the fragment program writes the draw's own
// factors to the second output: .rgb = the source factor (As, 1 or 0), .a = x with
// the destination factor 1 - x (x = As, 0 or 1). The values are the same ones the
// per-tuple states read from the second output and the hardware still forms the
// 1 - x itself on the same path.
//
// The stored-alpha convention: the As factor is the fragment program's SECOND output (dual-source
// blending, GL_SRC1_ALPHA / GL_ONE_MINUS_SRC1_ALPHA): frag_factor.a = min(2 As, 255)
// / 255 is the GS As / 0x80 ratio, while the first output's alpha is the stored GS
// byte. Ad (C = 1) has no fixed-function form in that convention (GL_DST_ALPHA would
// read the byte, not the ratio): it stays an unmatched approximation here, exact on
// the shader-side blend route.
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
constexpr uint32_t kGlSrc1Alpha = 0x88F9;           // GL_SRC1_COLOR (0x88F9; GL_SRC1_ALPHA is 0x8589): the same
                                                    // factor on the per-tuple states, where every channel of the
                                                    // second output carries As
constexpr uint32_t kGlOneMinusSrc1Alpha = 0x88FB;   // GL_ONE_MINUS_SRC1_ALPHA
constexpr uint32_t kGlOneMinusSrc1Color = 0x88FA;   // GL_ONE_MINUS_SRC1_COLOR
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
    // The unified state -- func(SRC1_COLOR, ONE_MINUS_SRC1_ALPHA), the fragment
    // program's second output carrying (sf, sf, sf, x) with the destination factor
    // 1 - x -- expresses a matched FUNC_ADD arm exactly when its source factor is
    // one of As / ONE / ZERO and its destination factor one of 1-As / ONE / ZERO
    // (then x = As / 0 / 1): 0101, 0202, 0200, 2101 and the 27 A = B arms. Kept on
    // their own state (the identity oracle, gs_replay hash-equality on the three
    // captures, rejected them: one pixel by one LSB on 39 of 301 Paris frames): the
    // FIX arms (the blend CONSTANT is not quantised like a fragment output) and 0201
    // (a constant ONE destination factor rounds differently from 1 - 0 on the SRC1
    // path); also 1000 (src 1-As), 1200 (dst As), the SUBTRACT / REVERSE_SUBTRACT arms
    // and the unmatched approximation.
    static bool Unified(const FfBlendMap& m);
    // Blended draws with COLCLAMP=0 take the shader-side blend (wrap semantics).
    static bool RouteWrapBlend(bool alpha_blend, bool colclamp);
    // The COLCLAMP=0 per-channel result for a raw blend value (two's complement wrap).
    static int WrapChannel(int raw);
};

}  // namespace render
}  // namespace ps2r
