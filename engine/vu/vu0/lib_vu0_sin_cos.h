#pragma once
// The SDK libvu0 sin/cos microprogram, replaced by ONE host polynomial when its
// fingerprint is found at the VU0 entry.
//
// PS2/SDK fact: Sony's libvu0 loads a dual-lane sin/cos microprogram at VU0
// micro address 0xF0: it computes (sin, cos) of VF20.x into VF31.xy and of
// VF20.z into VF31.zw by a polynomial and ends with TPC = 0x1C0. The match is
// keyed on the first three SDK instruction bundles only (no title address),
// so it lives in the engine. The substitution touches no pipe and no clock.
//
// The arithmetic is SineCosine() (lib_vu0_sin_cos.inl), not the host libm:
// Apple's and glibc's sinf/cosf disagree by one code on ordinary arguments
// (the Linux identity leg, CHARTER2_C1.md: the first six differing RAM words
// at boundary 60 were the sine/cosine of one rotation), and a platform-
// identical replay needs the same bits on every host. SineCosine evaluates in
// double with IEEE operations only -- every multiply-add is an explicit
// std::fma, so no -ffp-contract setting of the including TU can fuse or split
// a step -- and rounds to float once, in the caller's rounding mode (the VU
// chop guard / TrigMath's FE_TOWARDZERO), the contract the libm call had.
// Reduction: k = round(x * 2/pi), r = x - k*pi/2 in three parts (exact for
// |k| < 2^20, i.e. |x| < 2^20 -- every title angle: an int32 angle times
// 2*pi/65536 stays below 2.1e5); beyond that the argument is first taken
// modulo the double 2*pi (fmod, exact), a finite host-identical value that is
// no longer the mathematical one. sin/cos of |r| <= pi/4 by their Taylor
// series to r^15 / r^16 (remainders 5e-17 / 2e-18, below a float code's 6e-8;
// 2,000,000 random arguments in [-7, 7] matched the correctly rounded value).
// sin(+-0) = +-0 (the sign of the argument), cos(+-0) = 1.
#include <cstddef>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class LibVu0SinCos {
public:
    static constexpr uint32_t kEntryPc = 0xF0;
    static constexpr uint32_t kExitTpc = 0x1C0;

    // The three SDK bundles at 0xF0 / 0xF8 / 0x100 ({lower, upper} words).
    static constexpr uint32_t kFingerprint[3][2] = {
        {0x8000033Cu, 0x0154A7EBu},
        {0x8000033Cu, 0x003F07DAu},
        {0x3FC90FD8u, 0x809F07C0u},
    };

    // (sin x, cos x) of a float, bit-identical on every host (header comment).
    // The caller's rounding mode applies to the final float rounding.
    static inline void SineCosine(float x, float& sine, float& cosine);

    // True when `pc` is the entry and the micro image carries the fingerprint.
    static bool Matches(const uint8_t* micro, size_t micro_size, uint32_t pc);
    // Perform the substitution on `regs` (VF31, the hard-wired pins, TPC);
    // returns false without touching anything when the fingerprint is absent.
    static bool TryExecute(const uint8_t* micro, size_t micro_size, VUregs& regs, uint32_t pc);

    static constexpr double kTwoOverPi = 6.36619772367581382433e-01;
    static constexpr double kTwoPi = 6.28318530717958647693e+00;
    static constexpr double kLargeArgument = 1048576.0;  // 2^20: the exact-reduction bound
    // pi/2 in three parts with trailing zero bits (k * part is exact for |k| < 2^20).
    static constexpr double kPio2Part1 = 1.57079632673412561417e+00;
    static constexpr double kPio2Part2 = 6.07710050630396597660e-11;
    static constexpr double kPio2Part3 = 2.02226624871116645580e-21;
    // Taylor coefficients: sin from r^3 (-1/3!, 1/5!, ... to r^15), cos from r^4 (1/4!, -1/6!, ... to r^16).
    static constexpr double kSin[7] = {
        -1.0 / 6.0, 1.0 / 120.0, -1.0 / 5040.0, 1.0 / 362880.0,
        -1.0 / 39916800.0, 1.0 / 6227020800.0, -1.0 / 1307674368000.0};
    static constexpr double kCos[7] = {
        1.0 / 24.0, -1.0 / 720.0, 1.0 / 40320.0, -1.0 / 3628800.0,
        1.0 / 479001600.0, -1.0 / 87178291200.0, 1.0 / 20922789888000.0};
};

#include "engine/vu/vu0/lib_vu0_sin_cos.inl"
