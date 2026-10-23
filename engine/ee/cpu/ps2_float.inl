// Inline bodies of the PS2 float helpers (sibling of ps2_float.h).
//
// div.s / sqrt.s / rsqrt.s round to NEAREST while the guest EE thread runs
// FE_TOWARDZERO. The round-3 form switched the FPU mode around every division
// (fegetround, fesetround(FE_TONEAREST), the divide, fesetround back): two libc
// calls and two FPCR writes per div.s -- 2.5 % of the EE logic thread's CPU in
// Paris (eecode round 4, E3). This form never touches the mode: the quotient
// (or root) is computed in DOUBLE under whatever mode is current and rounded to
// a float to-nearest-even in integer arithmetic. Why that is exact:
//  * a float quotient a/b or root sqrt(a) is never exactly a float midpoint
//    (a 25-bit odd significand m with a = m*b or a = m*m would give a an odd
//    part above 2^24), so the to-nearest float of the exact value x is unique;
//  * the double d = round_mode(x) is within one double ulp of x, and the only
//    double inside that radius that is a float midpoint would be d itself: so
//    when d's low 29 bits are not the midpoint pattern, RN_float(d) == RN_float(x)
//    under ANY mode;
//  * when d IS a midpoint pattern: under FE_TOWARDZERO (the guest thread's mode)
//    x lies strictly beyond it (|x| > |d|, x != d), so the result is the float
//    away from zero; under another mode the side is unknowable from d and the
//    mode-switching form answers (a 2^-29 event, never seen on a guest thread).
// Non-finite doubles (b == 0, 0/0: inf / NaN, whose clamp depends on the host's
// default-NaN sign) also take the mode-switching form, so the bits are the old
// bits in every case. tests/ee/test_ps2_float_rn.cpp holds the two forms
// bit-equal over the random grid under all four modes.
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>

inline float ps2_float_clamp(float v) {
    u32 bits;
    std::memcpy(&bits, &v, 4);
    const u32 exp = (bits >> 23) & 0xFFu;
    if (exp == 0xFFu) {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&v, &bits, 4);
    }
    return v;
}

namespace ps2_float_detail {

// The mode-switching forms (the round-3 bodies verbatim): the reference, and
// the fallback of the arithmetic forms on the cases they do not decide.
inline float DivModeSwitch(float a, float b) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = a / b;
    fesetround(m);
    return r;
}
inline float SqrtModeSwitch(float v) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = std::sqrt(v);
    fesetround(m);
    return r;
}

// To-nearest-even float of a finite double, in integer arithmetic (the FPU's
// conversion would use the current mode). Overflow yields +-inf (the caller
// clamps); subnormal and zero results take the shifted rounding position.
inline float RoundToFloatNearestEven(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    const uint32_t sign = static_cast<uint32_t>(bits >> 63) << 31;
    const int e = static_cast<int>((bits >> 52) & 0x7FFu) - 1023;
    uint64_t sig = (bits & ((1ull << 52) - 1)) | (1ull << 52);   // 53 bits, normal (never a double subnormal here)
    if (e > 127) {
        const uint32_t inf = sign | 0x7F800000u;
        float f;
        std::memcpy(&f, &inf, 4);
        return f;
    }
    int shift = 29;              // 53 -> 24 bits
    int fexp = e + 127;          // biased float exponent of a normal result
    if (fexp <= 0) {             // subnormal (or below): the leading bit lands below the 24-bit window
        shift += 1 - fexp;
        fexp = 0;
        if (shift > 63) { sig = 0; shift = 63; }
    }
    const uint64_t half = 1ull << (shift - 1);
    const uint64_t low = sig & ((1ull << shift) - 1);
    uint64_t q = sig >> shift;
    if (low > half || (low == half && (q & 1))) ++q;
    // q holds the (implicit bit included) 24-bit significand, or less for a
    // subnormal; a carry out of bit 23 is the next binade (or the first
    // normal), which the exponent field absorbs.
    uint32_t out;
    if (fexp == 0) {
        out = static_cast<uint32_t>(q);                       // bit 23 set = became the smallest normal
    } else {
        if (q >> 24) { q >>= 1; ++fexp; }
        out = (static_cast<uint32_t>(fexp) << 23) | (static_cast<uint32_t>(q) & 0x7FFFFFu);
        if (fexp >= 255) out = 0x7F800000u;
    }
    out |= sign;
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}

// d = round_current_mode(x) for an x that is never a float midpoint; see the
// header comment. Returns false when d does not decide the answer.
inline bool NearestFromDouble(double d, float* out) {
    if (!std::isfinite(d)) return false;
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    if ((bits & 0x1FFFFFFFull) == 0x10000000ull) {
        // A float midpoint pattern (only when the double has a float's range;
        // a subnormal-range result rounds at another position: fall back).
        const int e = static_cast<int>((bits >> 52) & 0x7FFu) - 1023;
        if (e < -126 || fegetround() != FE_TOWARDZERO) return false;
        // Toward zero: |x| > |d|, the float away from zero.
        bits += 0x10000000ull;   // past the midpoint: rounds up to the next float
    }
    double dd;
    std::memcpy(&dd, &bits, 8);
    *out = RoundToFloatNearestEven(dd);
    return true;
}

}  // namespace ps2_float_detail

inline float ps2_div_s(float a, float b) {
    const float ca = ps2_float_clamp(a), cb = ps2_float_clamp(b);
    const volatile double x = static_cast<double>(ca) / static_cast<double>(cb);   // volatile: no folding across modes
    float r;
    if (!ps2_float_detail::NearestFromDouble(x, &r)) r = ps2_float_detail::DivModeSwitch(ca, cb);
    return ps2_float_clamp(r);
}

inline float ps2_sqrt_s(float v) {
    const float cv = std::fabs(ps2_float_clamp(v));
    const volatile double x = std::sqrt(static_cast<double>(cv));
    float r;
    if (!ps2_float_detail::NearestFromDouble(x, &r)) r = ps2_float_detail::SqrtModeSwitch(cv);
    return ps2_float_clamp(r);
}

inline float ps2_rsqrt_s(float a, float b) {
    // The round-3 body: RN(a / RN(sqrt(|b|))) -- two float roundings.
    const float ca = ps2_float_clamp(a);
    const float s = ps2_sqrt_s(b);   // clamp(RN(sqrt(|clamp(b)|))): the clamp is the identity on a root
    const volatile double x = static_cast<double>(ca) / static_cast<double>(s);
    float r;
    if (!ps2_float_detail::NearestFromDouble(x, &r)) r = ps2_float_detail::DivModeSwitch(ca, s);
    return ps2_float_clamp(r);
}
