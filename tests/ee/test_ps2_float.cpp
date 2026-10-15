// Ps2FloatSemantics: clamp table, div/sqrt/rsqrt in single precision under
// round-to-nearest with the caller's mode restored, no NaN/Inf ever produced.
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/ee/cpu/ps2_float.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
static float FromBits(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

int main() {
    // Clamp: exponent 0xFF (NaN, +-Inf) -> +-MAX_FLOAT with the sign kept;
    // everything else passes through bit-exact.
    CHECK(Bits(ps2_float_clamp(FromBits(0x7F800000u))) == 0x7F7FFFFFu);  // +Inf
    CHECK(Bits(ps2_float_clamp(FromBits(0xFF800000u))) == 0xFF7FFFFFu);  // -Inf
    CHECK(Bits(ps2_float_clamp(FromBits(0x7FC00000u))) == 0x7F7FFFFFu);  // qNaN
    CHECK(Bits(ps2_float_clamp(FromBits(0xFFFFFFFFu))) == 0xFF7FFFFFu);  // -NaN payload
    CHECK(Bits(ps2_float_clamp(FromBits(0x7F7FFFFFu))) == 0x7F7FFFFFu);  // MAX stays
    CHECK(Bits(ps2_float_clamp(FromBits(0x00000001u))) == 0x00000001u);  // denormal stays
    CHECK(Bits(ps2_float_clamp(FromBits(0x80000000u))) == 0x80000000u);  // -0 stays
    CHECK(Bits(ps2_float_clamp(1.0f)) == 0x3F800000u);
    CHECK(Ps2Float::Clamp(FromBits(0x7F800000u)) == ps2_float_clamp(FromBits(0x7F800000u)));

    // div.s rounds to nearest even under a truncating caller: 1/5 must be
    // 0x3E4CCCCD (nearest), not the 1-ULP-low 0x3E4CCCCC of round-toward-zero.
    fesetround(FE_TOWARDZERO);
    CHECK(Bits(ps2_div_s(1.0f, 5.0f)) == 0x3E4CCCCDu);
    CHECK(fegetround() == FE_TOWARDZERO);  // caller's mode restored
    CHECK(Bits(ps2_div_s(2.0f, 3.0f)) == 0x3F2AAAABu);
    // Division by zero: the IEEE Inf is clamped to MAX_FLOAT with the sign.
    CHECK(Bits(ps2_div_s(1.0f, 0.0f)) == 0x7F7FFFFFu);
    CHECK(Bits(ps2_div_s(-1.0f, 0.0f)) == 0xFF7FFFFFu);
    // NaN operands are clamped before the divide, so the result is finite.
    CHECK(Bits(ps2_div_s(FromBits(0x7FC00000u), 2.0f)) == Bits(FromBits(0x7F7FFFFFu) / 2.0f));

    // sqrt.s: |v| (the EE takes the absolute value), nearest rounding.
    CHECK(Bits(ps2_sqrt_s(4.0f)) == 0x40000000u);
    CHECK(Bits(ps2_sqrt_s(-4.0f)) == 0x40000000u);
    CHECK(Bits(ps2_sqrt_s(2.0f)) == 0x3FB504F3u);
    CHECK(Bits(ps2_sqrt_s(FromBits(0x7F800000u))) == Bits(std::sqrt(FromBits(0x7F7FFFFFu))));

    // rsqrt.s = a / sqrt(|b|).
    CHECK(Bits(ps2_rsqrt_s(1.0f, 4.0f)) == 0x3F000000u);
    CHECK(Bits(ps2_rsqrt_s(3.0f, -9.0f)) == 0x3F800000u);
    CHECK(Bits(ps2_rsqrt_s(1.0f, 0.0f)) == 0x7F7FFFFFu);

    fesetround(FE_TONEAREST);
    CHECK(Bits(ps2_div_s(1.0f, 5.0f)) == 0x3E4CCCCDu);
    CHECK(fegetround() == FE_TONEAREST);

    Ps2Float::SetGuestThreadRounding();
    CHECK(fegetround() == FE_TOWARDZERO);
    fesetround(FE_TONEAREST);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS Ps2FloatSemantics\n");
    return 0;
}
