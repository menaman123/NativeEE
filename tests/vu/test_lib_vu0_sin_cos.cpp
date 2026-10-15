// LibVu0SinCosPin: the SDK libvu0 sin/cos fingerprint (three instruction
// bundles at VU0 micro 0xF0) and its substitution -- VF31 = {sin, cos} of
// VF20.x and VF20.z (clamped) by LibVu0SinCos::SineCosine, the hard-wired pins,
// TPC = 0x1C0, no clock or pipe change; a non-matching image or another entry
// falls through to the normal microprogram path. The values are pinned as
// BITS: SineCosine is the one host sin/cos and must give these on every host
// (CHARTER2_C1.md, the Linux identity leg) -- the correctly rounded sin/cos of
// 0.5 and -2.0 under the VU's chop rounding, and the fixed finite values of
// the clamped Inf argument (MAX_FLOAT: beyond the exact-reduction bound, the
// argument is taken modulo the double 2*pi -- host-identical, not mathematical).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu0/lib_vu0_sin_cos.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"
#include "engine/platform/host/platform.h"

// The expectations run SineCosine under the chop guard in THIS TU: the FP ops must
// not move across the fesetround call (the tier's own TU has the same pragma).
PS2R_FENV_ACCESS_ON

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

void PutBundle(VuUnit& vu, uint32_t pc, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(pc, b, 8);
}

void InstallFingerprint(VuUnit& vu) {
    for (int i = 0; i < 3; ++i)
        PutBundle(vu, LibVu0SinCos::kEntryPc + 8u * static_cast<uint32_t>(i), LibVu0SinCos::kFingerprint[i][0],
                  LibVu0SinCos::kFingerprint[i][1]);
}

}  // namespace

int main() {
    {  // Match + substitution through the interpreter tier.
        VuUnit vu(0);
        InstallFingerprint(vu);
        vu.regs().VF[20].x = 0.5f;
        vu.regs().VF[20].z = -2.0f;
        vu.regs().VI[0] = 7;  // re-pinned by the substitution
        const uint64_t c0 = vu.cycle();
        VuInterpreterTier tier;
        tier.Run(vu, 0xF0);
        float s0, c0f, s1, c1;
        {
            // The same arithmetic under the VU's rounding mode (the tier runs the
            // substitution under its chop guard); volatile arguments keep the
            // compiler from folding the calls at compile time in round-to-nearest.
            VuChopRoundGuard g;
            volatile float a0 = 0.5f, a1 = -2.0f;
            LibVu0SinCos::SineCosine(a0, s0, c0f);
            LibVu0SinCos::SineCosine(a1, s1, c1);
        }
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].x) == VuFloat::F2Bits(s0));
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].y) == VuFloat::F2Bits(c0f));
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].z) == VuFloat::F2Bits(s1));
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].w) == VuFloat::F2Bits(c1));
        // The host-identity pins: sin/cos(0.5) = 0.479425538604203 / 0.8775825618903728,
        // sin/cos(-2) = -0.9092974268256817 / -0.4161468365471424, each truncated to float.
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].x) == 0x3ef57743u);
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].y) == 0x3f60a940u);
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].z) == 0xbf68c7b7u);
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].w) == 0xbed51132u);
        CHECK(vu.regs().TPC == LibVu0SinCos::kExitTpc);
        CHECK(vu.regs().VI[0] == 0 && vu.regs().VF[0].w == 1.0f);
        CHECK(vu.cycle() == c0 && tier.bundles() == 0);
        std::printf("  sincos(0.5, -2.0) = %08x %08x %08x %08x\n", VuFloat::F2Bits(vu.regs().VF[31].x),
                    VuFloat::F2Bits(vu.regs().VF[31].y), VuFloat::F2Bits(vu.regs().VF[31].z),
                    VuFloat::F2Bits(vu.regs().VF[31].w));
    }
    {  // Inputs are clamped: a denormal angle is +/-0, an Inf angle is MAX_FLOAT.
        VuUnit vu(0);
        InstallFingerprint(vu);
        vu.regs().VF[20].I[0] = 0x80000001u;  // -denormal -> -0 -> sin = -0, cos = 1
        vu.regs().VF[20].I[2] = 0x7F800000u;  // +Inf -> MAX_FLOAT
        VuInterpreterTier tier;
        tier.Run(vu, 0xF0);
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].x) == 0x80000000u && vu.regs().VF[31].y == 1.0f);
        float sm, cm;
        {
            // The clamp happened: the lanes are SineCosine of MAX_FLOAT itself.
            VuChopRoundGuard g;
            volatile float src = VuFloat::Bits2F(VuFloat::kMaxFloatBits);
            LibVu0SinCos::SineCosine(src, sm, cm);
        }
        std::printf("  sincos(MAX_FLOAT) = %08x %08x (expect %08x %08x)\n", VuFloat::F2Bits(vu.regs().VF[31].z),
                    VuFloat::F2Bits(vu.regs().VF[31].w), VuFloat::F2Bits(sm), VuFloat::F2Bits(cm));
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].z) == VuFloat::F2Bits(sm));
        CHECK(VuFloat::F2Bits(vu.regs().VF[31].w) == VuFloat::F2Bits(cm));
        // Finite, in [-1, 1], and the same bits on every host (the fmod reduction).
        CHECK(std::fabs(sm) <= 1.0f && std::fabs(cm) <= 1.0f);
        CHECK(VuFloat::F2Bits(sm) == 0x3f7fff82u && VuFloat::F2Bits(cm) == 0xbb7da040u);
    }
    {  // No fingerprint: the micro at 0xF0 runs normally (here: an E-bit pair).
        VuUnit vu(0);
        PutBundle(vu, 0xF0, 0x8000033Cu, 0x000002FFu | 0x40000000u);
        PutBundle(vu, 0xF8, 0x8000033Cu, 0x000002FFu);
        VuInterpreterTier tier;
        tier.Run(vu, 0xF0);
        CHECK(tier.bundles() == 2 && vu.regs().TPC == 0x100u);
        CHECK(!LibVu0SinCos::Matches(vu.state().micro, 0x1000, 0xF0));
    }
    {  // Fingerprint present but another entry: not taken; VU1 never matches.
        VuUnit vu(0);
        InstallFingerprint(vu);
        CHECK(LibVu0SinCos::Matches(vu.state().micro, 0x1000, 0xF0));
        CHECK(!LibVu0SinCos::Matches(vu.state().micro, 0x1000, 0xF8));
        CHECK(!LibVu0SinCos::Matches(vu.state().micro, 0xF0, 0xF0));  // image too short
        VuUnit vu1(1);
        InstallFingerprint(vu1);
        vu1.regs().VF[20].x = 0.5f;
        VuInterpreterTier tier;
        tier.Run(vu1, 0xF0);
        CHECK(tier.bundles() > 0);  // VU1 executed the words as a microprogram
    }
    if (g_fail) {
        std::printf("LibVu0SinCosPin: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("LibVu0SinCosPin: ALL PASS\n");
    return 0;
}
