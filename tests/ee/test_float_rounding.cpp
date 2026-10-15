// Ps2FloatRounding: the guest EE thread's rounding mode. The EE FPU (COP1)
// rounds add/sub/mul/cvt toward zero (chop), so every host thread that runs
// recompiled code is put in FE_TOWARDZERO by Ps2Float::SetGuestThreadRounding();
// div.s / sqrt.s / rsqrt.s are the exceptions the ps2_* helpers implement
// (nearest, single precision) while the caller's mode is kept. Pins: the
// mode is set and is per-thread; an inexact add/mul on a guest thread
// truncates toward zero where nearest would round away; the helpers answer
// the nearest quotient regardless; the clamp keeps the sign.
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

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

// The operations run at runtime on volatile operands so the compiler cannot
// fold them under its own (nearest) rounding assumption.
static float RuntimeAdd(float a, float b) {
    volatile float va = a, vb = b;
    volatile float r = va + vb;
    return r;
}
static float RuntimeMul(float a, float b) {
    volatile float va = a, vb = b;
    volatile float r = va * vb;
    return r;
}
static float RuntimeDiv(float a, float b) {
    volatile float va = a, vb = b;
    volatile float r = va / vb;
    return r;
}

int main() {
    // Operands whose exact result lies strictly between two singles, off the
    // midpoint, so nearest and toward-zero pick different neighbours:
    //   1 + 1.5*2^-24 = 1 + 0.75 ulp(1)      -> nearest 0x3F800001, toward zero 0x3F800000
    //   1.75 * (1 + 2^-23) = 1.75 + 1.75 ulp  -> nearest 0x3FE00002, toward zero 0x3FE00001
    const float kOne = 1.0f;
    const float kThreeQuarterUlp = FromBits(0x33C00000u);  // 1.5 * 2^-24
    const float kOnePlusUlp = FromBits(0x3F800001u);       // 1 + 2^-23
    const float kSevenQuarters = 1.75f;

    // Control: the host's default mode is nearest and rounds these away from zero.
    fesetround(FE_TONEAREST);
    CHECK(Bits(RuntimeAdd(kOne, kThreeQuarterUlp)) == 0x3F800001u);
    CHECK(Bits(RuntimeAdd(-kOne, -kThreeQuarterUlp)) == 0xBF800001u);
    CHECK(Bits(RuntimeMul(kSevenQuarters, kOnePlusUlp)) == 0x3FE00002u);
    CHECK(Bits(RuntimeDiv(1.0f, 3.0f)) == 0x3EAAAAABu);

    // (a) The guest thread mode is FE_TOWARDZERO after SetGuestThreadRounding().
    Ps2Float::SetGuestThreadRounding();
    CHECK(fegetround() == FE_TOWARDZERO);

    // (b) Under it an inexact add/mul truncates toward zero on both signs
    // (toward zero, not toward -infinity: the negative sum shrinks in magnitude).
    CHECK(Bits(RuntimeAdd(kOne, kThreeQuarterUlp)) == 0x3F800000u);
    CHECK(Bits(RuntimeAdd(-kOne, -kThreeQuarterUlp)) == 0xBF800000u);
    CHECK(Bits(RuntimeMul(kSevenQuarters, kOnePlusUlp)) == 0x3FE00001u);
    CHECK(Bits(RuntimeMul(-kSevenQuarters, kOnePlusUlp)) == 0xBFE00001u);
    // A raw single divide on the guest thread truncates too (0x3EAAAAAA)...
    CHECK(Bits(RuntimeDiv(1.0f, 3.0f)) == 0x3EAAAAAAu);

    // (c) ...but div.s through the helper is the nearest quotient 0x3EAAAAAB,
    // and the thread stays in FE_TOWARDZERO afterwards. Same for sqrt/rsqrt.
    CHECK(Bits(ps2_div_s(1.0f, 3.0f)) == 0x3EAAAAABu);
    CHECK(fegetround() == FE_TOWARDZERO);
    CHECK(Bits(Ps2Float::DivS(1.0f, 3.0f)) == 0x3EAAAAABu);
    CHECK(Bits(ps2_div_s(-1.0f, 3.0f)) == 0xBEAAAAABu);
    CHECK(Bits(ps2_sqrt_s(2.0f)) == 0x3FB504F3u);      // nearest sqrt(2); truncation gives ...F2
    CHECK(Bits(ps2_rsqrt_s(1.0f, 2.0f)) == 0x3F3504F3u);  // nearest 1/sqrt(2)
    CHECK(fegetround() == FE_TOWARDZERO);

    // (d) The clamp maps any exponent-0xFF pattern to +-MAX_FLOAT, sign kept,
    // under the guest mode as well.
    CHECK(Bits(ps2_float_clamp(FromBits(0x7FC00000u))) == 0x7F7FFFFFu);  // +qNaN
    CHECK(Bits(ps2_float_clamp(FromBits(0xFFC00000u))) == 0xFF7FFFFFu);  // -qNaN
    CHECK(Bits(ps2_float_clamp(FromBits(0x7F800001u))) == 0x7F7FFFFFu);  // +sNaN
    CHECK(Bits(ps2_float_clamp(FromBits(0x7F800000u))) == 0x7F7FFFFFu);  // +Inf
    CHECK(Bits(ps2_float_clamp(FromBits(0xFF800000u))) == 0xFF7FFFFFu);  // -Inf
    CHECK(Bits(ps2_float_clamp(FromBits(0x7F7FFFFFu))) == 0x7F7FFFFFu);  // MAX untouched
    CHECK(Bits(ps2_float_clamp(FromBits(0x3F800000u))) == 0x3F800000u);  // 1.0 untouched

    // Leave the process in the host default (the test binary is not a guest thread).
    fesetround(FE_TONEAREST);
    CHECK(fegetround() == FE_TONEAREST);

    // The mode is per host thread: a guest thread that sets it truncates,
    // while this (host) thread stays in nearest.
    int guest_mode = -1;
    uint32_t guest_sum = 0;
    std::thread guest([&] {
        Ps2Float::SetGuestThreadRounding();
        guest_mode = fegetround();
        guest_sum = Bits(RuntimeAdd(kOne, kThreeQuarterUlp));
    });
    guest.join();
    CHECK(guest_mode == FE_TOWARDZERO);
    CHECK(guest_sum == 0x3F800000u);
    CHECK(fegetround() == FE_TONEAREST);
    CHECK(Bits(RuntimeAdd(kOne, kThreeQuarterUlp)) == 0x3F800001u);

    if (g_fail) {
        std::printf("FAIL (%d)\n", g_fail);
        return 1;
    }
    std::printf("PASS Ps2FloatRounding\n");
    return 0;
}
