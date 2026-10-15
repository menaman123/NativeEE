// Inline bodies of the PS2 float helpers (sibling of ps2_float.h).
#include <cfenv>
#include <cmath>
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

inline float ps2_div_s(float a, float b) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = ps2_float_clamp(a) / ps2_float_clamp(b);
    fesetround(m);
    return ps2_float_clamp(r);
}

inline float ps2_sqrt_s(float v) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = std::sqrt(std::fabs(ps2_float_clamp(v)));
    fesetround(m);
    return ps2_float_clamp(r);
}

inline float ps2_rsqrt_s(float a, float b) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = ps2_float_clamp(a) / std::sqrt(std::fabs(ps2_float_clamp(b)));
    fesetround(m);
    return ps2_float_clamp(r);
}
